#include "scrollbarfader.hpp"

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>

ScrollBarFader::ScrollBarFader(QAbstractScrollArea* area) : QObject(area) {
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(900);
    const auto bars = {area->verticalScrollBar(), area->horizontalScrollBar()};
    for (QScrollBar* bar : bars)
        connect(bar, &QAbstractSlider::valueChanged, this, [this, area] {
            setActive(area, true);
            timer_->start();
        });
    connect(timer_, &QTimer::timeout, this, [this, area] { setActive(area, false); });
}

void ScrollBarFader::setActive(QAbstractScrollArea* area, bool active) {
    const auto bars = {area->verticalScrollBar(), area->horizontalScrollBar()};
    for (QScrollBar* bar : bars) {
        if (bar->property("scrolling").toBool() == active)
            continue;
        bar->setProperty("scrolling", active);
        bar->style()->unpolish(bar);
        bar->style()->polish(bar);
    }
}
