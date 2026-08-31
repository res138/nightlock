#pragma once

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVector>

// Catalog of the standard (built-in) entry icons. The picker in the
// entry dialog renders whatever this list contains, so growing the set
// later only means appending here (and to resources.qrc).
namespace standardicons {

struct StandardIcon {
    QString id;        // stable identifier, safe to persist
    QString title;     // human-readable name (tooltip in the picker)
    QString resource;  // Qt resource path
};

// Rendered choices shown in Settings → Appearance. `resource` and
// `lockedResource` live in the installed icons directory; `windowsResource`
// keeps native multi-frame title-bar/taskbar sizes on Windows.
struct ApplicationIcon {
    QString id;
    QString title;
    QString resource;
    QString lockedResource;
    QString windowsResource;
};

const QVector<StandardIcon>& entryIcons();

// The icon used when Entry::icon is empty. First item of entryIcons().
const StandardIcon& defaultEntryIcon();

// Selectable application-icon catalog. The first item is the product default.
const QVector<ApplicationIcon>& applicationIcons();
const ApplicationIcon& defaultApplicationIcon();

// Renders one catalog choice, or the default for an unknown id.
QIcon applicationIconForId(const QString& id, bool locked = false);

// Multi-resolution icon for the persisted appearance choice. On Windows this
// keeps native ICO sizes and supplies fractional-DPI sizes from the 1024px PNG.
QIcon applicationIcon(bool locked = false);

// Maps a persisted Entry::icon value (resource path; empty = default)
// back to the catalog id. Unknown paths fall back to the default id.
QString idForEntryIcon(const QString& icon);

// Maps a catalog id to the Entry::icon value to persist: empty string
// for the default icon, the resource path otherwise.
QString entryIconForId(const QString& id);

// The last icons the user picked (most recent first, up to 14),
// persisted across runs as portable references. Legacy paths are migrated
// when their installed-pack counterpart can be identified.
QStringList recentIconPaths();
void addRecentIconPath(const QString& path);

}  // namespace standardicons
