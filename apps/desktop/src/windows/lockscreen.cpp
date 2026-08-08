#include "lockscreen.hpp"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <cmath>

#include "respaths.hpp"

namespace {

constexpr int kFieldWidth = 300;
constexpr int kFieldHeight = 42;
constexpr int kRowSpacing = 8;
constexpr int kShakeReach = 14;  // widest shake swing, plus a hair
constexpr int kSelectFolderWidth = 118;

}  // namespace

LockScreen::LockScreen(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("lockScreen"));
    setAttribute(Qt::WA_StyledBackground);  // opaque white over the vault

    auto* icon = new QLabel;
    icon->setAlignment(Qt::AlignHCenter);
    // The art ships huge; render it at 96pt, crisp on retina.
    const QPixmap art(respaths::icon(QStringLiteral("lock.png")));
    if (!art.isNull()) {
        QPixmap scaled =
            art.scaled(QSize(192, 192), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(2.0);
        icon->setPixmap(scaled);
    }

    title_ = new QLabel(tr("Nightlock Vault is Locked"));
    title_->setObjectName(QStringLiteral("lockTitle"));
    title_->setAlignment(Qt::AlignHCenter);

    field_ = new QLineEdit;
    field_->setObjectName(QStringLiteral("lockField"));
    field_->setPlaceholderText(tr("Enter the password"));
    field_->setEchoMode(QLineEdit::Password);
    field_->setAttribute(Qt::WA_MacShowFocusRect, false);
    field_->setFixedSize(kFieldWidth, kFieldHeight);
    connect(field_, &QLineEdit::returnPressed, this, &LockScreen::submit);
    connect(field_, &QLineEdit::textEdited, this, [this] { setError(false); });

    auto* unlock = new QToolButton;
    unlock->setObjectName(QStringLiteral("lockUnlock"));
    unlock->setText(QStringLiteral("→"));
    unlock->setToolTip(tr("Unlock"));
    unlock->setFixedSize(kFieldHeight, kFieldHeight);
    unlock->setCursor(Qt::PointingHandCursor);
    connect(unlock, &QToolButton::clicked, this, &LockScreen::submit);

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

    // Create mode's confirm field, aligned with the field above (the
    // arrow button's width stays empty on the right flank).
    confirm_ = new QLineEdit;
    confirm_->setObjectName(QStringLiteral("lockField"));
    confirm_->setPlaceholderText(tr("Repeat the password"));
    confirm_->setEchoMode(QLineEdit::Password);
    confirm_->setAttribute(Qt::WA_MacShowFocusRect, false);
    confirm_->setFixedSize(kFieldWidth, kFieldHeight);
    connect(confirm_, &QLineEdit::returnPressed, this, &LockScreen::submit);
    connect(confirm_, &QLineEdit::textEdited, this, [this] { setError(false); });

    confirmHolder_ = new QWidget;
    confirmHolder_->setFixedSize(holder->width(), kFieldHeight + 10);
    auto* confirmLayout = new QHBoxLayout(confirmHolder_);
    confirmLayout->setContentsMargins(kShakeReach, 10,
                                      kShakeReach + kRowSpacing + kFieldHeight, 0);
    confirmLayout->addWidget(confirm_);
    confirmHolder_->hide();

    // Create mode's location row: where the new vault file will land.
    // The default is fine to keep — "Select Folder" is the opt-out.
    locationLabel_ = new QLabel;
    locationLabel_->setObjectName(QStringLiteral("lockPath"));

    auto* selectFolder = new QPushButton(tr("Select Folder…"));
    selectFolder->setObjectName(QStringLiteral("lockSelectFolder"));
    selectFolder->setCursor(Qt::PointingHandCursor);
    selectFolder->setFixedSize(kSelectFolderWidth, 30);
    connect(selectFolder, &QPushButton::clicked, this, &LockScreen::selectFolder);

    locationHolder_ = new QWidget;
    locationHolder_->setFixedSize(holder->width(), 42);
    auto* locationLayout = new QHBoxLayout(locationHolder_);
    locationLayout->setContentsMargins(kShakeReach, 12, kShakeReach, 0);
    locationLayout->setSpacing(kRowSpacing);
    locationLayout->addWidget(locationLabel_, 1);
    locationLayout->addWidget(selectFolder);
    locationHolder_->hide();

    error_ = new QLabel(QStringLiteral(" "));
    error_->setObjectName(QStringLiteral("lockError"));
    error_->setAlignment(Qt::AlignHCenter);
    error_->setFixedHeight(18);  // reserved, so nothing jumps on error

    touchId_ = new QPushButton(tr("Unlock with Touch ID"));
    touchId_->setObjectName(QStringLiteral("lockTouchId"));
    touchId_->setCursor(Qt::PointingHandCursor);
    touchId_->setFixedSize(180, 34);
    connect(touchId_, &QPushButton::clicked, this,
            &LockScreen::touchIdRequested);
    touchId_->hide();

    // Unlock mode's escape hatch: no password means this vault is a
    // dead end — forget it and start over on the first-run screen (the
    // file itself stays on disk). Shares the bottom slot with Create
    // mode's openExisting_ — the modes never show both.
    forgotPassword_ = new QLabel(
        QStringLiteral("<a style=\"color:#6E6A75;\" href=\"#forgot\">") +
        tr("Forgot a password?") + QStringLiteral("</a>"));
    forgotPassword_->setObjectName(QStringLiteral("lockLink"));
    forgotPassword_->setAlignment(Qt::AlignHCenter);
    connect(forgotPassword_, &QLabel::linkActivated, this,
            [this] { emit forgotPasswordRequested(); });

    // Create mode's escape hatch: point Nightlock at a vault file that
    // already exists instead of creating one.
    openExisting_ = new QLabel(
        QStringLiteral("<a style=\"color:#6E6A75;\" href=\"#open\">") +
        tr("Open an existing vault instead.") + QStringLiteral("</a>"));
    openExisting_->setObjectName(QStringLiteral("lockLink"));
    openExisting_->setAlignment(Qt::AlignHCenter);
    connect(openExisting_, &QLabel::linkActivated, this,
            [this] { emit openExistingRequested(); });
    openExisting_->hide();

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
    layout->addWidget(title_);
    layout->addSpacing(16);
    layout->addWidget(holder, 0, Qt::AlignHCenter);
    layout->addWidget(confirmHolder_, 0, Qt::AlignHCenter);
    layout->addWidget(locationHolder_, 0, Qt::AlignHCenter);
    layout->addSpacing(10);
    layout->addWidget(error_);
    layout->addWidget(touchId_, 0, Qt::AlignHCenter);
    layout->addStretch(6);
    layout->addWidget(openExisting_);
    layout->addWidget(forgotPassword_);
    layout->addSpacing(6);
    layout->addWidget(link);
}

void LockScreen::setMode(Mode mode) {
    mode_ = mode;
    const bool create = mode == Mode::Create;
    title_->setText(create ? tr("Create Your Nightlock Vault")
                           : tr("Nightlock Vault is Locked"));
    field_->setPlaceholderText(create ? tr("Choose a master password")
                                      : tr("Enter the password"));
    confirmHolder_->setVisible(create);
    locationHolder_->setVisible(create);
    openExisting_->setVisible(create);
    forgotPassword_->setVisible(!create);
    touchId_->setVisible(!create && touchIdAvailable_);
}

void LockScreen::setVaultTarget(const QString& path) {
    vaultTarget_ = QDir::cleanPath(path);
    const QString shown = QDir::toNativeSeparators(vaultTarget_);
    locationLabel_->ensurePolished();  // elide with the styled font
    locationLabel_->setText(locationLabel_->fontMetrics().elidedText(
        shown, Qt::ElideMiddle, kFieldWidth + kFieldHeight - kSelectFolderWidth));
    locationLabel_->setToolTip(shown);
}

void LockScreen::selectFolder() {
    const QFileInfo current(vaultTarget_);
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder"),
                                                          current.absolutePath());
    if (dir.isEmpty())
        return;
    setVaultTarget(QDir(dir).filePath(current.fileName()));
    setError(false);
}

void LockScreen::reset() {
    field_->clear();
    confirm_->clear();
    setError(false);
    setTouchIdBusy(false);
    field_->setFocus();
}

void LockScreen::setTouchIdAvailable(bool available) {
    touchIdAvailable_ = available;
    touchId_->setVisible(available && mode_ == Mode::Unlock);
}

void LockScreen::setTouchIdBusy(bool busy) {
    touchId_->setEnabled(!busy);
    touchId_->setText(busy ? tr("Waiting for Touch ID…")
                           : tr("Unlock with Touch ID"));
}

void LockScreen::showTouchIdError(const QString& message) {
    setTouchIdBusy(false);
    error_->setText(message);
    field_->setFocus();
}

void LockScreen::rejectPassword(const QString& message) {
    setError(true, message);
    shake();
    field_->selectAll();
    field_->setFocus();
}

void LockScreen::debugFail() {
    field_->setText(QStringLiteral("wrong-password"));
    submit();
}

void LockScreen::submit() {
    const QString password = field_->text();
    if (password.isEmpty()) {
        rejectPassword(tr("Enter a password."));
        return;
    }
    if (mode_ == Mode::Create && password != confirm_->text()) {
        setError(true, tr("Passwords do not match."));
        shake();
        confirm_->selectAll();
        confirm_->setFocus();
        return;
    }
    emit passwordSubmitted(password);
}

void LockScreen::setError(bool on, const QString& message) {
    if (field_->property("error").toBool() != on) {
        field_->setProperty("error", on);
        field_->style()->unpolish(field_);
        field_->style()->polish(field_);
    }
    error_->setText(on ? (message.isEmpty() ? tr("Invalid password. Try again.")
                                            : message)
                       : QStringLiteral(" "));
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
