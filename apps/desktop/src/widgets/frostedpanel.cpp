#include "frostedpanel.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>

namespace frosted {

namespace {
constexpr qreal kVeilOpacity = 0.85;  // solid color share over the blurred backdrop
}

QRect panelRect(const QWidget* widget) {
    return widget->rect().marginsRemoved(QMargins(kShadow, kShadow, kShadow, kShadow));
}

void paintPanel(QWidget* widget, const QRectF& panel, qreal reveal,
                const QPixmap& backdrop, const QPoint& backdropOffset) {
    QPainter painter(widget);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    for (int i = kShadow; i >= 1; --i) {
        const qreal t = 1.0 - static_cast<qreal>(i) / kShadow;
        const qreal alpha = (1 + 4 * t * t) * reveal;
        painter.setBrush(QColor(0, 0, 0, static_cast<int>(alpha)));
        painter.drawRoundedRect(panel.adjusted(-i, -i + 2, i, i + 2),
                                kRadius + i * 0.6, kRadius + i * 0.6);
    }

    // Frosted glass: blurred backdrop showing through a white veil.
    QPainterPath path;
    path.addRoundedRect(panel, kRadius, kRadius);
    painter.save();
    painter.setClipPath(path);
    painter.fillRect(panel, Qt::white);
    if (!backdrop.isNull())
        painter.drawPixmap(panel.topLeft() + backdropOffset, backdrop);
    painter.fillRect(panel, QColor(255, 255, 255, qRound(kVeilOpacity * 255)));
    painter.restore();
}

QPixmap captureBackdrop(QWidget* widget, QPoint* offsetOut) {
    *offsetOut = QPoint();

    // Walk up to the first non-popup window (popups may be parented to
    // other popups, e.g. submenus to their parent menu).
    QWidget* source = widget->parentWidget();
    while (source && (!source->isWindow() || source->windowType() == Qt::Popup))
        source = source->parentWidget();
    if (!source)
        return {};

    const QRect panel = panelRect(widget);
    const QRect inSource(source->mapFromGlobal(widget->mapToGlobal(panel.topLeft())),
                         panel.size());
    const QRect clipped = inSource.intersected(source->rect());
    // A partial grab would sit on the panel as a hard-edged patch (the
    // popup sticks out of its window); a plain panel looks better.
    if (clipped != inSource || clipped.isEmpty())
        return {};

    const QPixmap grabbed = source->grab(clipped);
    if (grabbed.isNull())
        return {};

    // Cheap blur: heavy downscale, then smooth upscale.
    const QSize coarse(qMax(1, grabbed.width() / 12), qMax(1, grabbed.height() / 12));
    const QImage blurred = grabbed.toImage()
                               .scaled(coarse, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .scaled(grabbed.size(), Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
    QPixmap result = QPixmap::fromImage(blurred);
    result.setDevicePixelRatio(grabbed.devicePixelRatio());
    *offsetOut = clipped.topLeft() - inSource.topLeft();
    return result;
}

}  // namespace frosted
