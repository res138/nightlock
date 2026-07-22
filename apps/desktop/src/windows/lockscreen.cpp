#include "lockscreen.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <cmath>

namespace {

// The demo vault has no real encryption yet; the lock screen accepts
// this password.
constexpr char kDemoPassword[] = "nightlock";

constexpr int kFieldWidth = 300;
constexpr int kFieldHeight = 42;
constexpr int kRowSpacing = 8;
constexpr int kShakeReach = 14;  // widest shake swing, plus a hair

}  // namespace

LockScreen::LockScreen(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("lockScreen"));
    setAttribute(Qt::WA_StyledBackground);  // opaque white over the vault

    auto* icon = new QLabel;
    icon->setAlignment(Qt::AlignHCenter);
    // The art ships huge; render it at 96pt, crisp on retina.
    const QPixmap art(QStringLiteral(NIGHTLOCK_ICONS_DIR "/lock.png"));
    if (!art.isNull()) {
        QPixmap scaled =
            art.scaled(QSize(192, 192), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(2.0);
        icon->setPixmap(scaled);
    }

    auto* title = new QLabel(tr("Nightlock Vault is Locked"));
    title->setObjectName(QStringLiteral("lockTitle"));
    title->setAlignment(Qt::AlignHCenter);

    field_ = new QLineEdit;
    field_->setObjectName(QStringLiteral("lockField"));
    field_->setPlaceholderText(tr("Enter the password"));
    field_->setEchoMode(QLineEdit::Password);
    field_->setAttribute(Qt::WA_MacShowFocusRect, false);
    field_->setFixedSize(kFieldWidth, kFieldHeight);
    connect(field_, &QLineEdit::returnPressed, this, &LockScreen::tryUnlock);
    connect(field_, &QLineEdit::textEdited, this, [this] { setError(false); });

    auto* unlock = new QToolButton;
    unlock->setObjectName(QStringLiteral("lockUnlock"));
    unlock->setText(QStringLiteral("→"));
    unlock->setToolTip(tr("Unlock"));
    unlock->setFixedSize(kFieldHeight, kFieldHeight);
    unlock->setCursor(Qt::PointingHandCursor);
    connect(unlock, &QToolButton::clicked, this, &LockScreen::tryUnlock);

    // The row sits inside a fixed-size holder outside any layout, so
    // the shake can move it freely without the layout fighting back.
    // The holder is wider than the row by the swing on both flanks —
    // otherwise its bounds would clip the row at full amplitude.
    const QSize rowSize(kFieldWidth + kRowSpacing + kFieldHeight, kFieldHeight);
    auto* holder = new QWidget;
    holder->setFixedSize(rowSize.width() + 2 * kShakeReach, rowSize.height());
    row_ = new QWidget(holder);
    row_->setFixedSize(rowSize);
    row_->move(kShakeReach, 0);
    auto* rowLayout = new QHBoxLayout(row_);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(kRowSpacing);
    rowLayout->addWidget(field_);
    rowLayout->addWidget(unlock);

    error_ = new QLabel(QStringLiteral(" "));
    error_->setObjectName(QStringLiteral("lockError"));
    error_->setAlignment(Qt::AlignHCenter);
    error_->setFixedHeight(18);  // reserved, so nothing jumps on error

    auto* link = new QLabel(QStringLiteral(
        "<a style=\"color:#6E6A75;\" href=\"https://github.com/rodukov/nightlock\">"
        "Read more about nightlock encryption and security.</a>"));
    link->setObjectName(QStringLiteral("lockLink"));
    link->setAlignment(Qt::AlignHCenter);
    link->setOpenExternalLinks(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 18);
    layout->addStretch(5);
    layout->addWidget(icon);
    layout->addSpacing(18);
    layout->addWidget(title);
    layout->addSpacing(16);
    layout->addWidget(holder, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    layout->addWidget(error_);
    layout->addStretch(6);
    layout->addWidget(link);
}

void LockScreen::reset() {
    field_->clear();
    setError(false);
    field_->setFocus();
}

void LockScreen::debugFail() {
    field_->setText(QStringLiteral("wrong-password"));
    tryUnlock();
}

void LockScreen::tryUnlock() {
    if (field_->text() == QLatin1String(kDemoPassword)) {
        emit unlocked();
        return;
    }
    setError(true);
    shake();
    field_->selectAll();
    field_->setFocus();
}

void LockScreen::setError(bool on) {
    if (field_->property("error").toBool() != on) {
        field_->setProperty("error", on);
        field_->style()->unpolish(field_);
        field_->style()->polish(field_);
    }
    error_->setText(on ? tr("Invalid password. Try again.") : QStringLiteral(" "));
}

// Damped horizontal wiggle of the field row.
void LockScreen::shake() {
    auto* anim = new QVariantAnimation(this);
    anim->setDuration(360);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    connect(anim, &QVariantAnimation::valueChanged, row_, [this](const QVariant& value) {
        const qreal t = value.toReal();
        const qreal offset = std::sin(t * M_PI * 5) * 12.0 * (1.0 - t);
        row_->move(kShakeReach + qRound(offset), 0);
    });
    connect(anim, &QVariantAnimation::finished, this, [this, anim] {
        row_->move(kShakeReach, 0);
        anim->deleteLater();
    });
    anim->start();
}
