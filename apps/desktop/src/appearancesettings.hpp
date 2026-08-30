#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

// User-facing appearance switches (Settings → Appearance): the color
// scheme, accent, application icon, list density and folder-icon visibility.
// Everything persists via QSettings; setters reinstall the app stylesheet
// where needed and ping notifier() so open views refresh live.
namespace appearancesettings {

// Stable option ids, index-aligned with their Settings controls.
inline constexpr const char* kThemes[] = {"light", "dark", "system"};
inline constexpr const char* kAccents[] = {"black", "blue", "green"};
inline constexpr const char* kApplicationIcons[] = {"petal-keyhole", "flower"};
inline constexpr const char* kSidebarItemSizes[] = {"small", "default", "large"};
inline constexpr const char* kEntryListItemSizes[] = {"default", "small"};

QString theme();  // one of kThemes
void setTheme(const QString& theme);

// Whether the dark look is on right now: "dark" directly, "system"
// when the OS reports a dark scheme.
bool darkActive();

QString accent();  // one of kAccents
QColor accentColorFor(const QString& accent);
QColor accentColor();
// Text drawn on top of accent surfaces: white over deep accents,
// near-black once the accent itself is light (dark theme's "black").
QColor accentTextColor();
void setAccent(const QString& accent);

// Stable catalog id used by the Dock/taskbar/window icon. Missing or
// obsolete values always resolve to the first entry: Petal Keyhole.
QString applicationIcon();  // one of kApplicationIcons
void setApplicationIcon(const QString& icon);

bool folderIcons();
void setFolderIcons(bool shown);

// Stable preset ids used by the two density dropdowns. Missing and obsolete
// values resolve to "default", preserving the pre-setting layout exactly.
QString sidebarItemSize();  // one of kSidebarItemSizes
void setSidebarItemSize(const QString& size);

QString entryListItemSize();  // one of kEntryListItemSizes
void setEntryListItemSize(const QString& size);

struct SidebarItemMetrics {
    int fontPixelSize;
    int iconExtent;
    int rowHeight;
    int indentation;
};
SidebarItemMetrics sidebarItemMetrics();

struct EntryListItemMetrics {
    int rowHeight;
    int iconExtent;
    int nameFontPixelSize;
    int subtitleFontPixelSize;
};
EntryListItemMetrics entryListItemMetrics();

// Semantic colors for hand-painted widgets, resolved for the active
// scheme. Read at paint time — a theme switch repolishes every
// widget, so the next paint already picks the other side.
struct Palette {
    QColor window;       // top-level surfaces
    QColor canvas;       // the NetGraph canvas
    QColor card;         // card fills
    QColor input;        // input-like fills (fields, dropdowns)
    QColor inputHover;   // their hover fill
    QColor border;       // regular hairlines
    QColor borderStrong; // stronger hairlines (buttons, legend)
    QColor separator;    // row separator lines
    QColor ink;          // primary text and glyphs
    QColor value;        // field-value text
    QColor muted;        // secondary text
    QColor faint;        // tertiary text
    QColor scroll;       // scrollbar handle
    QColor scrollHover;
    QColor toggleOff;    // switch track in the off state
    QColor veil;         // frosted-panel veil base color
};
const Palette& palette();

// Menu icon (resources/icons/menu) that re-tints itself at paint
// time: the dark theme lightens the glyph, QIcon::Selected renders
// white for accent-colored selections.
QIcon themedMenuIcon(const QString& name);

// Same vector-backed icon with a caller-supplied tint. Unlike first
// rasterizing at a hard-coded 2x size, the engine renders at the painter's
// actual (including fractional) device-pixel ratio.
QIcon tintedMenuIcon(const QString& name, const QColor& color);

// Painter-backed color swatch used by popup menus. It remains sharp at
// Windows' fractional scale factors instead of stretching a fixed 2x raster.
QIcon colorSwatchIcon(const QColor& color);

// Loads style.qss, substitutes the @theme and @accent tokens and
// installs the result on the application. Startup and every setter go
// through it; it also follows the OS scheme while theme is "system".
void applyStylesheet();

class Notifier : public QObject {
    Q_OBJECT
public:
    void notify() { emit changed(); }
    void notifyApplicationIcon() {
        emit applicationIconChanged();
        emit changed();
    }
    void notifySidebarItemSize() {
        emit sidebarItemSizeChanged();
        emit changed();
    }
    void notifyEntryListItemSize() {
        emit entryListItemSizeChanged();
        emit changed();
    }
signals:
    void changed();
    void applicationIconChanged();
    void sidebarItemSizeChanged();
    void entryListItemSizeChanged();
};
Notifier* notifier();

}  // namespace appearancesettings
