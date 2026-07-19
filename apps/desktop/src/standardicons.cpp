#include "standardicons.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QSettings>

#include <atomic>
#include <mutex>
#include <thread>

namespace standardicons {

const QVector<StandardIcon>& entryIcons() {
    static const QVector<StandardIcon> icons = {
        {QStringLiteral("default"),
         QCoreApplication::translate("standardicons", "Default"),
         QStringLiteral(":/icons/entry.png")},
    };
    return icons;
}

const StandardIcon& defaultEntryIcon() {
    return entryIcons().first();
}

QString idForEntryIcon(const QString& icon) {
    if (icon.isEmpty())
        return defaultEntryIcon().id;
    for (const StandardIcon& item : entryIcons())
        if (item.resource == icon)
            return item.id;
    return defaultEntryIcon().id;
}

QString entryIconForId(const QString& id) {
    if (id == defaultEntryIcon().id)
        return {};
    for (const StandardIcon& item : entryIcons())
        if (item.id == id)
            return item.resource;
    return {};
}

QStringList galleryIconPaths() {
    QStringList result;
    // NIGHTLOCK_ICONS_DIR points at resources/icons in the source tree
    // (set by CMake); a packaged build would ship the packs next to the
    // binary and adjust this lookup.
    QDir root(QStringLiteral(NIGHTLOCK_ICONS_DIR));
    const QStringList packs =
        root.entryList({QStringLiteral("P*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& pack : packs) {
        QDir dir(root.filePath(pack));
        const QStringList files =
            dir.entryList({QStringLiteral("*.ico"), QStringLiteral("*.png"),
                           QStringLiteral("*.jpg"), QStringLiteral("*.svg")},
                          QDir::Files, QDir::Name);
        for (const QString& file : files)
            result << dir.filePath(file);
    }
    return result;
}

namespace {

std::mutex g_galleryCacheMutex;
QHash<QString, QImage> g_galleryCache;
std::atomic<bool> g_stopPreload{false};
std::thread g_preloadThread;

constexpr int kGalleryDecodeSize = 64;  // 32px cells on a 2x display
constexpr int kMaxRecentIcons = 10;
const QLatin1String kRecentIconsKey("recentIcons");

}  // namespace

void preloadGalleryIcons() {
    const QStringList paths = galleryIconPaths();
    // QImage decoding is thread-safe (unlike QPixmap); the gallery
    // converts the cached images on the GUI thread when painting. The
    // thread must be stopped before the application tears down —
    // decoding against unloaded image plugins crashes.
    g_preloadThread = std::thread([paths] {
        for (const QString& path : paths) {
            if (g_stopPreload.load(std::memory_order_relaxed))
                return;
            QImage image(path);
            if (image.isNull())
                continue;
            if (image.width() > kGalleryDecodeSize || image.height() > kGalleryDecodeSize)
                image = image.scaled(kGalleryDecodeSize, kGalleryDecodeSize,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
            std::lock_guard<std::mutex> lock(g_galleryCacheMutex);
            g_galleryCache.insert(path, image);
        }
    });
}

void stopGalleryPreload() {
    g_stopPreload.store(true, std::memory_order_relaxed);
    if (g_preloadThread.joinable())
        g_preloadThread.join();
}

QImage cachedGalleryImage(const QString& path) {
    std::lock_guard<std::mutex> lock(g_galleryCacheMutex);
    return g_galleryCache.value(path);
}

QStringList recentIconPaths() {
    return QSettings().value(kRecentIconsKey).toStringList().mid(0, kMaxRecentIcons);
}

void addRecentIconPath(const QString& path) {
    if (path.isEmpty())
        return;
    QSettings settings;
    QStringList recents = settings.value(kRecentIconsKey).toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    settings.setValue(kRecentIconsKey, recents.mid(0, kMaxRecentIcons));
}

}  // namespace standardicons
