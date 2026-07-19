#pragma once

#include <QImage>
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

const QVector<StandardIcon>& entryIcons();

// The icon used when Entry::icon is empty. First item of entryIcons().
const StandardIcon& defaultEntryIcon();

// Maps a persisted Entry::icon value (resource path; empty = default)
// back to the catalog id. Unknown paths fall back to the default id.
QString idForEntryIcon(const QString& icon);

// Maps a catalog id to the Entry::icon value to persist: empty string
// for the default icon, the resource path otherwise.
QString entryIconForId(const QString& id);

// All icons of the bundled "P*" packs (resources/icons/P1, P2, …)
// merged into one flat list of file paths, pack by pack. The packs
// live on disk rather than in the Qt resource system — ~190 MB is too
// much to embed into the binary.
QStringList galleryIconPaths();

// Starts decoding every pack icon on a background thread, so the
// gallery scrolls without hitching on first open.
void preloadGalleryIcons();
// Stops and joins the preload thread; call before the app shuts down.
void stopGalleryPreload();
// Pre-decoded image for a pack icon; null if not (yet) cached.
QImage cachedGalleryImage(const QString& path);

// The last icons the user picked (most recent first, up to 10),
// persisted across runs.
QStringList recentIconPaths();
void addRecentIconPath(const QString& path);

}  // namespace standardicons
