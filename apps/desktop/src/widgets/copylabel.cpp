#include "copylabel.hpp"

#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QVariantAnimation>

#include "appearancesettings.hpp"
#include "nlmenu.hpp"
#include "overflowfade.hpp"

namespace {

constexpr int kFlashMs = 160;
constexpr int kHoldMs = 900;
constexpr int kHeight = 16;

QColor textColor() { return appearancesettings::palette().value; }

}  // namespace

CopyLabel::CopyLabel(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
    clipboardText_ = text;
    hold_->stop();
    flash_->stop();
    copied_ = 0;
    updateGeometry();
    update();
}

void CopyLabel::setClipboardText(const QString& text) {
    clipboardText_ = text;
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

void CopyLabel::copyAndFlash() {
    QGuiApplication::clipboard()->setText(clipboardText_);
    flash_->stop();
    flash_->setStartValue(copied_);
    flash_->setEndValue(1.0);
    flash_->start();
    hold_->start();
}

QSize CopyLabel::sizeHint() const {
    return {qBound(70, naturalTextWidth() + 8, 260), kHeight};
}

int CopyLabel::naturalTextWidth() const {
    constexpr int kIconSize = 13;
    constexpr int kGap = 5;
    const int iconWidth = leadingIconVisible_ ? kIconSize + kGap : 0;
    return QFontMetrics(font()).horizontalAdvance(text_) + iconWidth;
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
    const bool overflow = naturalTextWidth() > width();
    const auto textPen = [this, overflow](const QColor& color) {
        if (!overflow)
            return QPen(color);
        overflowfade::Edge edge = overflowfade::Edge::Left;
        if (contentAlignment_.testFlag(Qt::AlignHCenter))
            edge = overflowfade::Edge::Both;
        else if (contentAlignment_.testFlag(Qt::AlignLeft))
            edge = overflowfade::Edge::Right;
        return QPen(QBrush(overflowfade::gradient(width(), color, edge)), 1.0);
    };

    if (copied_ < 1.0) {
        painter.setOpacity(1.0 - copied_);
        painter.setPen(textPen(textColor()));
        if (leadingIconVisible_) {
            const QFontMetrics metrics(font());
            const int textWidth = metrics.horizontalAdvance(text_);
            const int x = alignedX(textWidth + kGap + kIconSize);
            copyIcon.paint(&painter,
                           QRect(x, (height() - kIconSize) / 2, kIconSize, kIconSize));
            painter.drawText(QRect(x + kIconSize + kGap, 0, textWidth, height()),
                             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             text_);
        } else {
            painter.drawText(rect(), contentAlignment_ | Qt::AlignVCenter |
                                         Qt::TextSingleLine,
                             text_);
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
        painter.setPen(textColor());
        painter.drawText(QRectF(x + kIconSize + kGap, slide, textWidth, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void CopyLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        copyAndFlash();
}

void CopyLabel::contextMenuEvent(QContextMenuEvent* event) {
    auto* menu = new NlMenu(this);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    QAction* copy = menu->addAction(
        appearancesettings::themedMenuIcon(QStringLiteral("copy")), tr("Copy"),
        this, &CopyLabel::copyAndFlash);
    copy->setEnabled(!clipboardText_.isEmpty());
    menu->popupAt(event->globalPos());
    event->accept();
}
