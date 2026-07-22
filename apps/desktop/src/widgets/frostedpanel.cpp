#include "frostedpanel.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>

namespace frosted {


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
    painter.fillRect(panel, QColor(255, 255, 255, qRound(kVeil * 255)));
    painter.restore();
}

QPixmap captureBackdrop(QWidget* widget, QPoint* offsetOut) {
    *offsetOut = QPoint();

    // The popup may overhang its nearest window (a gallery over the
    // edit dialog, a menu at the window edge), so compose the grab
    // from the whole chain of non-popup ancestor windows: the main
    // window first, closer windows (e.g. the dialog) painted over it.
    QList<QWidget*> sources;
    for (QWidget* w = widget->parentWidget(); w;) {
        QWidget* window = w->window();
        if (!window)
            break;
        if (window->windowType() != Qt::Popup && !sources.contains(window))
            sources.append(window);
        w = window->parentWidget();
    }
    if (sources.isEmpty())
        return {};

    const QRect panel = panelRect(widget);
    const QRect panelGlobal(widget->mapToGlobal(panel.topLeft()), panel.size());
    const qreal dpr = widget->devicePixelRatioF();

    QPixmap canvas(panel.size() * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::white);
    bool captured = false;
    {
        QPainter compose(&canvas);
        for (auto it = sources.rbegin(); it != sources.rend(); ++it) {
            QWidget* source = *it;
            const QRect sourceGlobal(source->mapToGlobal(QPoint(0, 0)), source->size());
            const QRect overlap = panelGlobal.intersected(sourceGlobal);
            if (overlap.isEmpty())
                continue;
            const QPixmap grabbed =
                source->grab(QRect(source->mapFromGlobal(overlap.topLeft()), overlap.size()));
            if (grabbed.isNull())
                continue;
            compose.drawPixmap(overlap.topLeft() - panelGlobal.topLeft(), grabbed);
            captured = true;
        }
    }
    if (!captured)
        return {};

    // Cheap blur: heavy downscale, then smooth upscale.
    const QSize coarse(qMax(1, canvas.width() / 16), qMax(1, canvas.height() / 16));
    const QImage blurred = canvas.toImage()
                               .scaled(coarse, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .scaled(canvas.size(), Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
    QPixmap result = QPixmap::fromImage(blurred);
    result.setDevicePixelRatio(dpr);
    return result;
}

}  // namespace frosted
