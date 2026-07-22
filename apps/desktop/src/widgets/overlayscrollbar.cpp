#include "overlayscrollbar.hpp"

#include <QAbstractScrollArea>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>

namespace {
constexpr int kWidth = 8;          // strip width; the handle sits inset
constexpr int kInset = 2;          // margin around the handle
constexpr qreal kMinHandle = 22.0; // shortest handle
}  // namespace

OverlayScrollBar::OverlayScrollBar(QAbstractScrollArea* area)
    : QWidget(area), area_(area), inner_(area->verticalScrollBar()) {
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    hide();

    idle_ = new QTimer(this);
    idle_->setSingleShot(true);
    idle_->setInterval(900);
    connect(idle_, &QTimer::timeout, this, [this] {
        // Not while the cursor is on the bar (hovering or mid-drag).
        if (dragging_ || underMouse())
            idle_->start();
        else
            setShown(false);
    });

    connect(inner_, &QAbstractSlider::valueChanged, this, [this] {
        activity();
        update();
    });
    connect(inner_, &QAbstractSlider::rangeChanged, this, [this](int min, int max) {
        if (min == max)
            setShown(false);
        place();
        update();
    });

    area->installEventFilter(this);
    place();
}

bool OverlayScrollBar::eventFilter(QObject* object, QEvent* event) {
    if (object == area_ &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        place();
    return QWidget::eventFilter(object, event);
}

void OverlayScrollBar::place() {
    const QRect viewport = area_->viewport()->geometry();
    setGeometry(viewport.right() + 1 - kWidth, viewport.top(), kWidth, viewport.height());
    raise();
}

QRectF OverlayScrollBar::handleRect() const {
    const qreal min = inner_->minimum();
    const qreal max = inner_->maximum();
    if (max <= min)
        return {};
    const qreal page = std::max<qreal>(1.0, inner_->pageStep());
    const qreal track = height() - 2.0 * kInset;
    const qreal handle = std::max(kMinHandle, track * page / (max - min + page));
    const qreal y = kInset + (inner_->value() - min) / (max - min) * (track - handle);
    return QRectF(kInset, y, kWidth - 2.0 * kInset, handle);
}

void OverlayScrollBar::paintEvent(QPaintEvent*) {
    if (opacity_ < 0.01)
        return;
    const QRectF handle = handleRect();
    if (handle.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor color = dragging_ || underMouse() ? QColor(0xA9, 0xA9, 0xA9)
                                             : QColor(0xCF, 0xCF, 0xCF);
    color.setAlphaF(color.alphaF() * opacity_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(handle, 2, 2);
}

void OverlayScrollBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const QRectF handle = handleRect();
    if (handle.isEmpty())
        return;
    dragging_ = true;
    if (handle.contains(event->position())) {
        dragOffset_ = event->position().y() - handle.top();
    } else {
        // Jump: center the handle on the click, then drag from there.
        dragOffset_ = handle.height() / 2.0;
        mouseMoveEvent(event);
    }
    activity();
    update();
}

void OverlayScrollBar::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        update();  // hover shade
        return;
    }
    const qreal min = inner_->minimum();
    const qreal max = inner_->maximum();
    const qreal track = height() - 2.0 * kInset;
    const qreal handle = handleRect().height();
    if (max <= min || track <= handle)
        return;
    const qreal ratio = (event->position().y() - dragOffset_ - kInset) / (track - handle);
    inner_->setValue(qRound(min + std::clamp(ratio, 0.0, 1.0) * (max - min)));
}

void OverlayScrollBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    dragging_ = false;
    idle_->start();
    update();
}

void OverlayScrollBar::activity() {
    if (inner_->minimum() == inner_->maximum())
        return;
    setShown(true);
    idle_->start();
}

// The fade the user perceives: opacity ramps quickly on show, drains
// slowly on hide.
void OverlayScrollBar::setShown(bool shown) {
    if (shown_ == shown)
        return;
    shown_ = shown;
    if (fade_) {
        fade_->stop();
        fade_->deleteLater();
    }
    if (shown) {
        place();
        show();
    }
    fade_ = new QVariantAnimation(this);
    fade_->setDuration(shown ? 120 : 300);
    fade_->setStartValue(opacity_);
    fade_->setEndValue(shown ? 1.0 : 0.0);
    connect(fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        opacity_ = value.toReal();
        update();
    });
    connect(fade_, &QVariantAnimation::finished, this, [this, shown] {
        fade_ = nullptr;
        if (!shown)
            hide();
    });
    fade_->start();
}
