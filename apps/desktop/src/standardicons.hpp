#pragma once

#include <QIcon>
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

// Multi-resolution application icon. On Windows this keeps the native
// executable/icon-resource sizes (and supplies fractional-DPI sizes), rather
// than asking the shell to shrink a single 1024px PNG for every surface.
QIcon applicationIcon(bool locked = false);

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
// Pre-decoded native-size variants for a pack icon; empty if not (yet)
// cached. In particular, .ico files retain their distinct 16/24/32/48/etc.
// frames so QIcon can choose for the current monitor's device-pixel ratio.
QVector<QImage> cachedGalleryImages(const QString& path);

// The last icons the user picked (most recent first, up to 14),
// persisted across runs. Missing files are pruned on read.
QStringList recentIconPaths();
void addRecentIconPath(const QString& path);

}  // namespace standardicons
