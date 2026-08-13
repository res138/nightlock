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

QColor textColor(bool primary) {
    return primary ? appearancesettings::palette().ink
                   : appearancesettings::palette().value;
}

// Best effort for the common uniquely-owned case.  QString may detach
// when another Qt object still shares the buffer, so this does not turn
// ordinary Qt text storage into secure memory.
void discardText(QString& text) {
    if (!text.isEmpty())
        text.fill(QChar(u'\0'));
    text.clear();
    text.squeeze();
}

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
    discardText(text_);
    discardText(clipboardText_);
    text_ = text;
    clipboardText_ = text;
    hold_->stop();
    flash_->stop();
    copied_ = 0;
    updateGeometry();
    update();
}

void CopyLabel::setClipboardText(const QString& text) {
    discardText(clipboardText_);
    clipboardText_ = text;
}

void CopyLabel::clear() {
    hold_->stop();
    flash_->stop();
    copied_ = 0;
    discardText(text_);
    discardText(clipboardText_);
    updateGeometry();
    update();
}

void CopyLabel::setLeadingIconVisible(bool visible) {
    if (leadingIconVisible_ == visible)
        return;
    leadingIconVisible_ = visible;
    updateGeometry();
    update();
}

void CopyLabel::setContentAlignment(Qt::Alignment alignment) {
    contentAlignment_ = alignment;
    update();
}

void CopyLabel::setPrimaryTextColor(bool enabled) {
    if (primaryTextColor_ == enabled)
        return;
    primaryTextColor_ = enabled;
    update();
}

void CopyLabel::setTextElideMode(Qt::TextElideMode mode) {
    if (elideMode_ == mode)
        return;
    elideMode_ = mode;
    update();
}

void CopyLabel::copyAndFlash() {
    QGuiApplication::clipboard()->setText(clipboardText_);
    flash_->stop();
    flash_->setStartValue(copied_);
    flash_->setEndValue(1.0);
    flash_->start();
    hold_->start();
}

QSize CopyLabel::sizeHint() const {
    const int text = QFontMetrics(font()).horizontalAdvance(text_);
    constexpr int kIconSize = 13;
    constexpr int kGap = 5;
    const int iconWidth = leadingIconVisible_ ? kIconSize + kGap : 0;
    return {qBound(70, text + iconWidth + 8, 260), kHeight};
}

void CopyLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    constexpr int kIconSize = 13;
    constexpr int kGap = 5;
    static const QIcon copyIcon =
        appearancesettings::themedMenuIcon(QStringLiteral("copy"));
    const auto alignedX = [this](int contentWidth) {
        if (contentAlignment_.testFlag(Qt::AlignHCenter))
            return (width() - contentWidth) / 2;
        if (contentAlignment_.testFlag(Qt::AlignLeft))
            return 0;
        return width() - contentWidth;
    };

    if (copied_ < 1.0) {
        painter.setOpacity(1.0 - copied_);
        painter.setPen(textColor(primaryTextColor_));
        const QFontMetrics metrics(font());
        if (leadingIconVisible_) {
            const int available = qMax(0, width() - kIconSize - kGap);
            const QString visible = metrics.elidedText(text_, elideMode_, available);
            const int textWidth = metrics.horizontalAdvance(visible);
            const int x = alignedX(textWidth + kGap + kIconSize);
            copyIcon.paint(&painter,
                           QRect(x, (height() - kIconSize) / 2, kIconSize, kIconSize));
            painter.drawText(QRect(x + kIconSize + kGap, 0, textWidth, height()),
                             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             visible);
        } else {
            const QString visible = metrics.elidedText(text_, elideMode_, width());
            painter.drawText(rect(), contentAlignment_ | Qt::AlignVCenter |
                                         Qt::TextSingleLine,
                             visible);
        }
    }

    if (copied_ > 0.0) {
        painter.setOpacity(copied_);
        const QString label = QStringLiteral("Copied");
        const QFontMetrics metrics(font());
        const int textWidth = metrics.horizontalAdvance(label);
        const qreal slide = (1.0 - copied_) * 4.0;  // gentle rise-in
        const int x = alignedX(textWidth + kGap + kIconSize);
        const QRectF iconRect(x, (height() - kIconSize) / 2.0 + slide, kIconSize, kIconSize);
        copyIcon.paint(&painter, iconRect.toRect());
        painter.setPen(textColor(primaryTextColor_));
        painter.drawText(QRectF(x + kIconSize + kGap, slide, textWidth, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void CopyLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        copyAndFlash();
}
