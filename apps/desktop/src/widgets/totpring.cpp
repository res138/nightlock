#include "totpring.hpp"

#include <QDateTime>
#include <QPainter>
#include <QTimer>

#include <cmath>

namespace {
constexpr int kPeriodSeconds = 30;
}

TotpRing::TotpRing(QWidget* parent) : QWidget(parent) {
    setFixedSize(16, 16);
    auto* timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    timer->start();
}

void TotpRing::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    const double remaining = kPeriodSeconds - std::fmod(now, kPeriodSeconds);

    const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    painter.setPen(QPen(QColor(0xE3, 0xE3, 0xE3), 2.5));
    painter.drawEllipse(r);
    painter.setPen(QPen(QColor(0x22, 0xA0, 0x45), 2.5));
    painter.drawArc(r, 90 * 16, static_cast<int>(-(remaining / kPeriodSeconds) * 360 * 16));
}
