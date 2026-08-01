#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// The app's two configurable font roles (Settings → Appearance):
// Primary is the interface font (San Francisco by default — not "the
// system default", which would swap it for Segoe UI / DejaVu on a
// Windows or Linux port), Secondary is the display font of the tree,
// titles and entry names (Georgia by default). Both pickers offer the
// same fixed catalog — sans, serif and mono alike — with per-platform
// family aliases; only the defaults differ per role. A stored choice
// whose font is missing on this system resolves to the role's default
// (then to the first available option). macOS ships SF built in;
// other platforms side-load it from resources/fonts (the license
// forbids carrying the files in the repository).
namespace fonts {

enum class Role { Primary, Secondary };

struct Option {
    QString id;            // stable identifier, persisted
    QString title;         // shown in the Settings dropdown
    QStringList families;  // alias chain, most specific first
    bool available;        // any alias present on this system
};

// The catalog for a role, availability resolved for this system.
QList<Option> options(Role role);

// Index into options() of the effective choice: the stored one when
// its font is available, otherwise the first available option.
int selectedIndex(Role role);

// The concrete family name the effective choice resolves to.
QString resolvedFamily(Role role);

// Persists the choice and applies it everywhere: the application
// font, the stylesheet and the painted widgets.
void setSelected(Role role, const QString& id);

// Applies the effective Primary choice as the application font. Call
// once right after QApplication is up, before the first window.
void applyApplicationFont();

}  // namespace fonts
