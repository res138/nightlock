#pragma once

#include <QString>
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

const QVector<StandardIcon>& entryIcons();

// The icon used when Entry::icon is empty. First item of entryIcons().
const StandardIcon& defaultEntryIcon();

// Maps a persisted Entry::icon value (resource path; empty = default)
// back to the catalog id. Unknown paths fall back to the default id.
QString idForEntryIcon(const QString& icon);

// Maps a catalog id to the Entry::icon value to persist: empty string
// for the default icon, the resource path otherwise.
QString entryIconForId(const QString& id);

}  // namespace standardicons
