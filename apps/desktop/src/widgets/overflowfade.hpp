#pragma once

#include <QColor>
#include <QLinearGradient>
#include <QtGlobal>

namespace overflowfade {

enum class Edge { Left, Right, Both };

inline qreal extent(qreal width, Edge edge) {
    const qreal maximum = edge == Edge::Both ? width / 3.0 : width;
    return qMin<qreal>(24.0, maximum);
}

inline QLinearGradient gradient(qreal width, const QColor& opaque, Edge edge) {
    const qreal safeWidth = qMax<qreal>(1.0, width);
    const qreal stop = extent(safeWidth, edge) / safeWidth;
    QColor clear = opaque;
    clear.setAlpha(0);

    QLinearGradient result(0, 0, safeWidth, 0);
    if (edge == Edge::Left || edge == Edge::Both) {
        result.setColorAt(0.0, clear);
        result.setColorAt(stop, opaque);
    } else {
        result.setColorAt(0.0, opaque);
    }
    if (edge == Edge::Right || edge == Edge::Both) {
        result.setColorAt(1.0 - stop, opaque);
        result.setColorAt(1.0, clear);
    } else {
        result.setColorAt(1.0, opaque);
    }
    return result;
}

inline qreal opacityAt(qreal x, qreal width, Edge edge) {
    const qreal fade = extent(width, edge);
    if (fade <= 0)
        return 1.0;
    qreal opacity = 1.0;
    if (edge == Edge::Left || edge == Edge::Both)
        opacity = qMin(opacity, qBound<qreal>(0.0, x / fade, 1.0));
    if (edge == Edge::Right || edge == Edge::Both)
        opacity =
            qMin(opacity, qBound<qreal>(0.0, (width - x) / fade, 1.0));
    return opacity;
}

}  // namespace overflowfade
