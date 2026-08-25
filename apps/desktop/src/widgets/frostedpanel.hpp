#pragma once

#include <QPixmap>
#include <QRect>

class QWidget;

// Shared frosted-glass panel look for popup surfaces (context menus,
// the icon gallery): rounded panel with a soft shadow, a blurred
// snapshot of the window behind it under a translucent white veil.
namespace frosted {

#if defined(Q_OS_WIN)
// A wide, hand-painted alpha shadow turns into a visibly banded halo on
// Windows' layered popup windows.  Keep only enough transparent space for
// antialiased rounded corners; paintPanel() supplies a crisp outline there.
inline constexpr bool kUseOpaquePopupSurface = true;
inline constexpr int kShadow = 2;
inline constexpr int kRadius = 8;
#else
inline constexpr bool kUseOpaquePopupSurface = false;
inline constexpr int kShadow = 12;  // translucent margin reserved for the shadow
inline constexpr int kRadius = 10;
#endif
// Solid color share over the blurred backdrop; low enough that the
// blurred app behind the panel clearly shows through the glass.
inline constexpr qreal kVeil = 0.72;

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
