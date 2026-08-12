#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

// User-facing appearance switches (Settings → Appearance): the color
// scheme, accent, folder-icon visibility and compact entry layout.
// Everything persists via QSettings; setters reinstall the app
// stylesheet where needed and ping notifier() so open views refresh.
namespace appearancesettings {

// Stored option ids, index-aligned with the Settings dropdowns.
inline constexpr const char* kThemes[] = {"light", "dark", "system"};
inline constexpr const char* kAccents[] = {"black", "blue", "green"};

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

bool folderIcons();
void setFolderIcons(bool shown);

// Compact Mode replaces the entry list/detail split with one table.
// Column preferences describe the user's desired table; responsive
// layouts may temporarily show fewer columns without changing them.
enum class CompactColumn {
    Name,
    Login,
    Password,
    Url,
    Note,
    Date,
};

bool compactMode();
void setCompactMode(bool enabled);

// Returned in the canonical display order above. Login and Password
// are mandatory and therefore remain enabled even if stored settings
// are incomplete or a caller tries to turn them off.
QList<CompactColumn> compactColumns();
bool compactColumnEnabled(CompactColumn column);
void setCompactColumnEnabled(CompactColumn column, bool enabled);

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

// Loads style.qss, substitutes the @theme and @accent tokens and
// installs the result on the application. Startup and every setter go
// through it; it also follows the OS scheme while theme is "system".
void applyStylesheet();

class Notifier : public QObject {
    Q_OBJECT
public:
    void notify() { emit changed(); }
signals:
    void changed();
};
Notifier* notifier();

}  // namespace appearancesettings
