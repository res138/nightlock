#pragma once

#include <QPixmap>
#include <QRect>

class QWidget;

// Shared frosted-glass panel look for popup surfaces (context menus,
// the icon gallery): rounded panel with a soft shadow, a blurred
// snapshot of the window behind it under a translucent white veil.
namespace frosted {

// The caller selects the backing material explicitly for menus.  Automatic
// preserves the existing popup behaviour (opaque on Windows, captured blur
// elsewhere); CapturedBlur is the cross-platform fallback used when Windows
// cannot provide Desktop Acrylic; NativeBackdrop leaves the backing store
// transparent so DWM's transient-window material remains visible.
enum class PanelMaterial {
    Automatic,
    CapturedBlur,
    NativeBackdrop,
};

#if defined(Q_OS_WIN)
// DWM supplies the native Windows 11 shadow/corners.  The captured fallback
// uses a rounded window mask, so neither path needs a transparent shadow
// margin (which was the source of layered-window banding on Windows).
inline constexpr bool kUseOpaquePopupSurface = true;
inline constexpr int kShadow = 0;
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
                const QPixmap& backdrop, const QPoint& backdropOffset,
                PanelMaterial material = PanelMaterial::Automatic);

// Blurred grab of the first non-popup ancestor window behind `widget`'s
// panel. Returns a null pixmap when there is nothing to capture;
// `offsetOut` receives the offset of the grab inside the panel.
QPixmap captureBackdrop(QWidget* widget, QPoint* offsetOut,
                        bool allowOnWindows = false);

// Configures a popup HWND as Windows 11 transient UI.  On Windows 11 22H2+
// this requests Desktop Acrylic over the whole client area; on Windows 11
// 21H2 it still applies the small menu corners and dark-frame preference.
// Returns true only when the Acrylic backdrop itself is active.  Older
// Windows versions, unavailable APIs and non-Windows platforms return false,
// allowing the caller to use CapturedBlur without changing behaviour.
bool enableNativeMenuBackdrop(QWidget* widget, bool dark);

}  // namespace frosted
