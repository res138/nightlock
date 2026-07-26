#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

// User-facing appearance switches (Settings → Appearance): the color
// scheme, the accent color and folder-icon visibility. Everything
// persists via QSettings; setters reinstall the app stylesheet where
// needed and ping notifier() so open views refresh live.
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
