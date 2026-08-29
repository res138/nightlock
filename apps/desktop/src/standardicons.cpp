#include "standardicons.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImageReader>
#include <QPixmap>
#include <QSet>
#include <QSettings>

#include "respaths.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <thread>

#include "appearancesettings.hpp"

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

const QVector<ApplicationIcon>& applicationIcons() {
    static const QVector<ApplicationIcon> icons = {
        {QStringLiteral("petal-keyhole"),
         QCoreApplication::translate("standardicons", "Petal Keyhole"),
         QStringLiteral("appicon-petal-keyhole.png"),
         QStringLiteral("appicon-petal-keyhole-locked.png"),
         QStringLiteral("appicon-petal-keyhole.ico")},
        {QStringLiteral("flower"),
         QCoreApplication::translate("standardicons", "Blue Flower"),
         QStringLiteral("appicon-flower.png"),
         QStringLiteral("appicon-flower-locked.png"),
         QStringLiteral("appicon-flower.ico")},
    };
    return icons;
}

const ApplicationIcon& defaultApplicationIcon() {
    return applicationIcons().first();
}

QIcon applicationIconForId(const QString& id, bool locked) {
    const ApplicationIcon* selected = &defaultApplicationIcon();
    for (const ApplicationIcon& choice : applicationIcons()) {
        if (choice.id == id) {
            selected = &choice;
            break;
        }
    }

#ifdef Q_OS_WIN
    // A detailed lock badge collapses at 16px. Preserve the selected family,
    // but use its regular multi-frame ICO in both vault states.
    Q_UNUSED(locked);
    QIcon icon(respaths::icon(selected->windowsResource));
    if (icon.isNull() && selected->id == defaultApplicationIcon().id)
        icon = QIcon(QStringLiteral(":/nightlock.ico"));
    const QImage source(respaths::icon(selected->resource));
    if (!source.isNull()) {
        static constexpr int kAllSizes[] = {
            16, 20, 24, 32, 40, 48, 64, 80, 96, 128, 256,
        };
        static constexpr int kFractionalSizes[] = {20, 24, 40, 80, 96};
        // Normally the ICO contributes the native sizes and we only fill the
        // 125/150/250/300% gaps. kAllSizes is a robust fallback if a build
        // accidentally omits the embedded ICO resource.
        const int* sizes = icon.isNull() ? kAllSizes : kFractionalSizes;
        const qsizetype count = icon.isNull() ? std::size(kAllSizes)
                                              : std::size(kFractionalSizes);
        for (qsizetype i = 0; i < count; ++i) {
            const int extent = sizes[i];
            icon.addPixmap(QPixmap::fromImage(
                source.scaled(extent, extent, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation)));
        }
    }
    if (!icon.isNull())
        return icon;
#else
    const QString resource = locked ? selected->lockedResource : selected->resource;
    QIcon icon(respaths::icon(resource));
    if (!icon.isNull())
        return icon;
#endif

    // Robust fallback for incomplete development/install trees. Keep the
    // semantic vault state on macOS/Linux instead of silently dropping the
    // badge when one selected-family asset is missing.
#ifdef Q_OS_WIN
    return QIcon(respaths::icon(defaultApplicationIcon().resource));
#else
    return QIcon(respaths::icon(
        locked ? defaultApplicationIcon().lockedResource
               : defaultApplicationIcon().resource));
#endif
}

QIcon applicationIcon(bool locked) {
    return applicationIconForId(appearancesettings::applicationIcon(), locked);
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
    // Packaged builds ship the packs inside the app (respaths resolves
    // them); dev builds read resources/icons in the source tree.
    QDir root(respaths::iconsDir());
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
QHash<QString, QVector<QImage>> g_galleryCache;
std::atomic<bool> g_stopPreload{false};
std::thread g_preloadThread;

// The gallery paints 32 logical pixels. Keep enough source data for Windows
// at 300% while bounding modern 256/512px pack artwork in memory.
constexpr int kGalleryDecodeSize = 96;
constexpr int kMaxRecentIcons = 14;
const QLatin1String kRecentIconsKey("recentIcons");

struct DecodedFrame {
    QImage image;
    int quality = std::numeric_limits<int>::min();
};

quint64 sizeKey(const QSize& size) {
    return (static_cast<quint64>(static_cast<quint32>(size.width())) << 32) |
           static_cast<quint32>(size.height());
}

int frameQuality(const QImage& image) {
    // ICO packs often contain same-size 4/8/32-bit duplicates. Qt expands
    // all of them to ARGB32, so decoded depth/colorCount alone cannot tell a
    // 16-color frame from its true-color sibling. A bounded unique-pixel
    // sample preserves that distinction without scanning a full 1024px PNG.
    int stride = 1;
    while (static_cast<qint64>((image.width() + stride - 1) / stride) *
               ((image.height() + stride - 1) / stride) >
           4096)
        ++stride;
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += stride) {
        for (int x = 0; x < image.width(); x += stride) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) != 0)
                colors.insert(pixel);
        }
    }
    return image.depth() * 10000 + (image.hasAlphaChannel() ? 10000 : 0) +
           qMin(colors.size(), 9999);
}

void keepBestFrame(QHash<quint64, DecodedFrame>& frames, QImage image) {
    if (image.isNull())
        return;
    const quint64 key = sizeKey(image.size());
    const int quality = frameQuality(image);
    auto it = frames.find(key);
    if (it == frames.end() || quality > it->quality)
        frames.insert(key, {std::move(image), quality});
}

QVector<QImage> decodeGalleryIcon(const QString& path) {
    QImageReader probe(path);
    probe.setAutoTransform(true);
    const int frameCount = qMax(1, probe.imageCount());

    QHash<quint64, DecodedFrame> kept;
    QImage largest;
    int largestQuality = std::numeric_limits<int>::min();
    for (int frame = 0; frame < frameCount; ++frame) {
        if (g_stopPreload.load(std::memory_order_relaxed))
            return {};

        // A fresh reader makes random access reliable for ICO plugins whose
        // read() advances internal state. The OS file cache makes the repeated
        // opens cheap, and all work stays off the GUI thread.
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (frame > 0 && !reader.jumpToImage(frame))
            continue;
        QImage image = reader.read();
        if (image.isNull())
            continue;

        const int quality = frameQuality(image);
        const qint64 pixels = static_cast<qint64>(image.width()) * image.height();
        const qint64 largestPixels =
            static_cast<qint64>(largest.width()) * largest.height();
        if (pixels > largestPixels ||
            (pixels == largestPixels && quality > largestQuality)) {
            largest = image;
            largestQuality = quality;
        }

        if (image.width() <= kGalleryDecodeSize &&
            image.height() <= kGalleryDecodeSize)
            keepBestFrame(kept, std::move(image));
    }

    // Modern icons frequently jump straight from 48 to 256px. Retain one
    // bounded 96px rendition so 200-300% Windows displays downscale a clean
    // source instead of enlarging the 48px legacy frame.
    if (!largest.isNull() &&
        (largest.width() > kGalleryDecodeSize ||
         largest.height() > kGalleryDecodeSize)) {
        keepBestFrame(kept, largest.scaled(kGalleryDecodeSize,
                                           kGalleryDecodeSize,
                                           Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
    }

    QVector<QImage> result;
    result.reserve(kept.size());
    for (auto it = kept.cbegin(); it != kept.cend(); ++it)
        result.append(it->image);
    std::sort(result.begin(), result.end(), [](const QImage& left, const QImage& right) {
        return static_cast<qint64>(left.width()) * left.height() <
               static_cast<qint64>(right.width()) * right.height();
    });
    return result;
}

}  // namespace

void preloadGalleryIcons() {
    const QStringList paths = galleryIconPaths();
    // QImage decoding is thread-safe (unlike QPixmap); the gallery
    // converts the cached images on the GUI thread when painting. The
    // thread must be stopped before the application tears down —
    // decoding against unloaded image plugins crashes.
    g_stopPreload.store(false, std::memory_order_relaxed);
    g_preloadThread = std::thread([paths] {
        for (const QString& path : paths) {
            if (g_stopPreload.load(std::memory_order_relaxed))
                return;
            QVector<QImage> images = decodeGalleryIcon(path);
            if (images.isEmpty())
                continue;
            std::lock_guard<std::mutex> lock(g_galleryCacheMutex);
            g_galleryCache.insert(path, std::move(images));
        }
    });
}

void stopGalleryPreload() {
    g_stopPreload.store(true, std::memory_order_relaxed);
    if (g_preloadThread.joinable())
        g_preloadThread.join();
}

QVector<QImage> cachedGalleryImages(const QString& path) {
    std::lock_guard<std::mutex> lock(g_galleryCacheMutex);
    return g_galleryCache.value(path);
}

QStringList recentIconPaths() {
    QSettings settings;
    const QStringList stored = settings.value(kRecentIconsKey).toStringList();
    QStringList existing;
    for (const QString& path : stored) {
        if (QFile::exists(path) && !existing.contains(path))
            existing.append(path);
        if (existing.size() == kMaxRecentIcons)
            break;
    }
    if (existing != stored)
        settings.setValue(kRecentIconsKey, existing);
    return existing;
}

void addRecentIconPath(const QString& path) {
    if (path.isEmpty() || !QFile::exists(path))
        return;
    QSettings settings;
    QStringList recents = settings.value(kRecentIconsKey).toStringList();
    recents.removeAll(path);
    recents.prepend(path);
    settings.setValue(kRecentIconsKey, recents.mid(0, kMaxRecentIcons));
}

}  // namespace standardicons
