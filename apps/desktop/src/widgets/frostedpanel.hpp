#pragma once

#include <QPixmap>
#include <QRect>

class QWidget;

// Shared frosted-glass panel look for popup surfaces (context menus,
// the icon gallery): rounded panel with a soft shadow, a blurred
// snapshot of the window behind it under a translucent white veil.
namespace frosted {

inline constexpr int kShadow = 12;  // translucent margin reserved for the shadow
inline constexpr int kRadius = 10;

// The visible panel area: widget rect minus the shadow margin.
QRect panelRect(const QWidget* widget);

// Paints the shadow and the frosted panel onto `widget`. `panel` may be
// smaller than panelRect() during a reveal animation; `reveal` scales
// the shadow strength along with it.
void paintPanel(QWidget* widget, const QRectF& panel, qreal reveal,
                const QPixmap& backdrop, const QPoint& backdropOffset);

// Blurred grab of the first non-popup ancestor window behind `widget`'s
// panel. Returns a null pixmap when there is nothing to capture;
// `offsetOut` receives the offset of the grab inside the panel.
QPixmap captureBackdrop(QWidget* widget, QPoint* offsetOut);

}  // namespace frosted
