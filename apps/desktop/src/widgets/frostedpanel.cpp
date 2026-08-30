#include "frostedpanel.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#ifdef Q_OS_WIN
#include <QLibrary>
#include <QOperatingSystemVersion>
#include <qt_windows.h>
#endif

#include "appearancesettings.hpp"

namespace frosted {

namespace {

#ifdef Q_OS_WIN

// Keep the Windows 11 SDK values local and resolve dwmapi.dll at runtime.  The
// release can therefore still start on Windows 10 and can be built with an
// older SDK whose dwmapi.h predates the backdrop enums.
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaWindowCornerPreference = 33;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaRedirectionBitmapAlpha = 39;
constexpr int kDwmcpRoundSmall = 3;
constexpr int kDwmsbtNone = 1;
constexpr int kDwmsbtTransientWindow = 3;
constexpr int kWindows11InitialBuild = 22000;
constexpr int kWindows11BackdropBuild = 22621;
constexpr int kWindows11RedirectionAlphaBuild = 26100;

using DwmSetWindowAttributeFn =
    HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
// MARGINS normally comes from a recent dwmapi.h/uxtheme.h. Keep the four-int
// ABI local so the dynamic path also compiles against older Windows SDKs.
struct DwmMargins {
    int left;
    int right;
    int top;
    int bottom;
};
using DwmExtendFrameIntoClientAreaFn =
    HRESULT(WINAPI*)(HWND, const DwmMargins*);

struct DwmApi {
    DwmApi() : library(QStringLiteral("dwmapi")) {
        setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
            library.resolve("DwmSetWindowAttribute"));
        extendFrameIntoClientArea = reinterpret_cast<DwmExtendFrameIntoClientAreaFn>(
            library.resolve("DwmExtendFrameIntoClientArea"));
    }

    QLibrary library;
    DwmSetWindowAttributeFn setWindowAttribute = nullptr;
    DwmExtendFrameIntoClientAreaFn extendFrameIntoClientArea = nullptr;
};

const DwmApi& dwmApi() {
    static const DwmApi api;
    return api;
}

bool isAtLeastWindowsBuild(int build) {
    const QOperatingSystemVersion version = QOperatingSystemVersion::current();
    return version.majorVersion() > 10 ||
           (version.majorVersion() == 10 && version.microVersion() >= build);
}

#endif

}  // namespace

QRect panelRect(const QWidget* widget) {
    return widget->rect().marginsRemoved(QMargins(kShadow, kShadow, kShadow, kShadow));
}

void paintPanel(QWidget* widget, const QRectF& panel, qreal reveal,
                const QPixmap& backdrop, const QPoint& backdropOffset,
                PanelMaterial material) {
    QPainter painter(widget);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    if (material == PanelMaterial::NativeBackdrop) {
        // DWM owns the blur/noise/tint.  Clearing rather than filling is
        // essential: an opaque Qt background would completely cover Desktop
        // Acrylic.  A very light theme-aware wash keeps Nightlock's text
        // contrast stable while retaining the native material underneath.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(widget->rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        QPainterPath path;
        path.addRoundedRect(panel, kRadius, kRadius);
        painter.save();
        painter.setClipPath(path);
        if (!backdrop.isNull()) {
            // Keep a low-opacity captured blur underneath as a defensive
            // fallback for remote sessions/drivers that accept the DWM
            // attribute but do not visibly render the material.
            painter.setOpacity(0.22);
            painter.drawPixmap(panel.topLeft() + backdropOffset, backdrop);
            painter.setOpacity(1.0);
        }
        QColor wash = appearancesettings::palette().veil;
        wash.setAlpha(appearancesettings::darkActive() ? 108 : 124);
        painter.fillPath(path, wash);
        painter.restore();
        painter.strokePath(
            path, QPen(appearancesettings::palette().borderStrong, 1.0));
        return;
    }

    if constexpr (kUseOpaquePopupSurface) {
        if (material != PanelMaterial::CapturedBlur) {
            // QMenu/Qt::Popup is a layered window when
            // WA_TranslucentBackground is enabled. Repeated,
            // almost-transparent shadow rings and a blurred grab look heavily
            // banded after Windows composites that layer (and again when
            // whole-window opacity is animated). Other popup surfaces keep an
            // opaque elevated treatment with one device-independent outline.
            // Clear the alpha backing store explicitly so pixels from an
            // earlier paint/theme cannot survive in the rounded corners.
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(widget->rect(), Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

            const auto& palette = appearancesettings::palette();
            const QRectF outlined = panel.adjusted(0.5, 0.5, -0.5, -0.5);
            QPainterPath path;
            path.addRoundedRect(outlined, kRadius - 0.5, kRadius - 0.5);
            painter.fillPath(path, palette.veil);
            painter.strokePath(path, QPen(palette.borderStrong, 1.0));
            return;
        }
    }

    // Clear stale alpha before painting a software-frosted frame.  This also
    // makes repeated opens deterministic after a theme switch.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(widget->rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for (int i = kShadow; i >= 1; --i) {
        const qreal t = 1.0 - static_cast<qreal>(i) / kShadow;
        const qreal alpha = (1 + 4 * t * t) * reveal;
        painter.setBrush(QColor(0, 0, 0, static_cast<int>(alpha)));
        painter.drawRoundedRect(panel.adjusted(-i, -i + 2, i, i + 2),
                                kRadius + i * 0.6, kRadius + i * 0.6);
    }

    // Frosted glass: blurred backdrop showing through the veil tint.
    QPainterPath path;
    path.addRoundedRect(panel, kRadius, kRadius);
    painter.save();
    painter.setClipPath(path);
    const QColor veil = appearancesettings::palette().veil;
    painter.fillRect(panel, veil);
    if (!backdrop.isNull())
        painter.drawPixmap(panel.topLeft() + backdropOffset, backdrop);
    painter.fillRect(panel, QColor(veil.red(), veil.green(), veil.blue(), qRound(kVeil * 255)));
    painter.restore();
}

QPixmap captureBackdrop(QWidget* widget, QPoint* offsetOut, bool allowOnWindows) {
    *offsetOut = QPoint();

    if constexpr (kUseOpaquePopupSurface) {
        if (!allowOnWindows) {
            // Windows popup surfaces use the opaque Automatic path unless a
            // menu explicitly asks for the captured-blur fallback.
            Q_UNUSED(widget);
            return {};
        }
    }

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
    canvas.fill(appearancesettings::palette().window);
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

bool enableNativeMenuBackdrop(QWidget* widget, bool dark) {
#ifdef Q_OS_WIN
    if (!widget || qEnvironmentVariableIsSet("NIGHTLOCK_DISABLE_NATIVE_BACKDROP"))
        return false;

    const DwmApi& api = dwmApi();
    if (!api.setWindowAttribute)
        return false;

    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return false;

    // These two hints arrived with the first Windows 11 build.  Apply them
    // even on 21H2, where Desktop Acrylic itself is not yet exposed.
    if (isAtLeastWindowsBuild(kWindows11InitialBuild)) {
        const BOOL darkMode = dark ? TRUE : FALSE;
        api.setWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode,
                               sizeof(darkMode));
        const int corner = kDwmcpRoundSmall;
        api.setWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &corner,
                               sizeof(corner));
    }

    if (!isAtLeastWindowsBuild(kWindows11BackdropBuild) ||
        !api.extendFrameIntoClientArea) {
        return false;
    }

    // Windows 11 24H2 finally exposes the alpha channel of a classic Win32
    // redirection bitmap to DWM. Qt's raster backing store is premultiplied,
    // so enabling it prevents a transparent menu client area from being
    // treated as opaque before the Acrylic backdrop is composed. Older
    // Windows 11 builds continue to use the extended-frame path below.
    if (isAtLeastWindowsBuild(kWindows11RedirectionAlphaBuild)) {
        const BOOL useAlpha = TRUE;
        if (FAILED(api.setWindowAttribute(
                hwnd, kDwmwaRedirectionBitmapAlpha, &useAlpha,
                sizeof(useAlpha)))) {
            return false;
        }
    }

    const int backdrop = kDwmsbtTransientWindow;
    const HRESULT backdropResult = api.setWindowAttribute(
        hwnd, kDwmwaSystemBackdropType, &backdrop, sizeof(backdrop));
    if (FAILED(backdropResult))
        return false;

    // The backdrop enum covers the whole window bounds; extending the DWM
    // frame makes the transparent Qt client surface reveal it as intended.
    const DwmMargins margins{-1, -1, -1, -1};
    const HRESULT frameResult = api.extendFrameIntoClientArea(hwnd, &margins);
    if (FAILED(frameResult)) {
        const int none = kDwmsbtNone;
        api.setWindowAttribute(hwnd, kDwmwaSystemBackdropType, &none,
                               sizeof(none));
        return false;
    }

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOACTIVATE | SWP_NOZORDER);
    return true;
#else
    Q_UNUSED(widget);
    Q_UNUSED(dark);
    return false;
#endif
}

}  // namespace frosted
