#include "copylabel.hpp"

#include <QClipboard>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVariantAnimation>

#include "appearancesettings.hpp"

namespace {

constexpr int kFlashMs = 160;
constexpr int kHoldMs = 900;
constexpr int kHeight = 16;

QColor textColor() { return appearancesettings::palette().value; }

}  // namespace

CopyLabel::CopyLabel(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    QFont f = font();
    f.setPixelSize(12);
    setFont(f);

    flash_ = new QVariantAnimation(this);
    flash_->setDuration(kFlashMs);
    connect(flash_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        copied_ = value.toReal();
        update();
    });

    hold_ = new QTimer(this);
    hold_->setSingleShot(true);
    hold_->setInterval(kHoldMs);
    connect(hold_, &QTimer::timeout, this, [this] {
        flash_->stop();
        flash_->setStartValue(copied_);
        flash_->setEndValue(0.0);
        flash_->start();
    });
}

void CopyLabel::setText(const QString& text) {
    text_ = text;
    hold_->stop();
    flash_->stop();
    copied_ = 0;
    updateGeometry();
    update();
}

void CopyLabel::copyAndFlash() {
    QGuiApplication::clipboard()->setText(text_);
    flash_->stop();
    flash_->setStartValue(copied_);
    flash_->setEndValue(1.0);
    flash_->start();
    hold_->start();
}

QSize CopyLabel::sizeHint() const {
    const int text = QFontMetrics(font()).horizontalAdvance(text_);
    return {qBound(70, text + 8, 260), kHeight};
}

void CopyLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (copied_ < 1.0) {
        painter.setOpacity(1.0 - copied_);
        painter.setPen(textColor());
        painter.drawText(rect(), Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine,
                         text_);
    }

    if (copied_ > 0.0) {
        painter.setOpacity(copied_);
        const QString label = QStringLiteral("Copied");
        const QFontMetrics metrics(font());
        constexpr int kIconSize = 13;
        constexpr int kGap = 5;
        const int textWidth = metrics.horizontalAdvance(label);
        const qreal slide = (1.0 - copied_) * 4.0;  // gentle rise-in
        const int x = width() - textWidth - kGap - kIconSize;
        const QRectF iconRect(x, (height() - kIconSize) / 2.0 + slide, kIconSize, kIconSize);
        static const QIcon copyIcon = appearancesettings::themedMenuIcon(QStringLiteral("copy"));
        copyIcon.paint(&painter, iconRect.toRect());
        painter.setPen(textColor());
        painter.drawText(QRectF(x + kIconSize + kGap, slide, textWidth, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void CopyLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        copyAndFlash();
}
