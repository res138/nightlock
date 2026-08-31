#include "standardicons.hpp"

#include <QCoreApplication>
#include <QPixmap>
#include <QSettings>

#include "respaths.hpp"

#include "appearancesettings.hpp"
#include "iconreferences.hpp"

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
        // The ICO supplies native shell/title-bar frames. Fill any gaps from
        // the 1024px master (important for development trees carrying an old
        // ICO), then retain the master itself. The latter keeps the 96px lock
        // artwork sharp at Windows 300/400% instead of enlarging a 256px ICO
        // frame to 288/384 physical pixels.
        static constexpr int kWindowsSizes[] = {
            16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 96, 128, 256,
        };
        const QList<QSize> available = icon.availableSizes();
        for (const int extent : kWindowsSizes) {
            if (available.contains(QSize(extent, extent)))
                continue;
            icon.addPixmap(QPixmap::fromImage(
                source.scaled(extent, extent, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation)));
        }
        icon.addPixmap(QPixmap::fromImage(source));
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

namespace {

constexpr int kMaxRecentIcons = 14;
const QLatin1String kRecentIconsKey("recentIcons");

}  // namespace

QStringList recentIconPaths() {
    QSettings settings;
    const QStringList stored = settings.value(kRecentIconsKey).toStringList();
    QStringList existing;
    for (const QString& storedValue : stored) {
        const QString value = iconreferences::normalizeStoredValue(storedValue);
        // Keep unresolved portable values: reinstalling their pack restores
        // them, while IconPicker renders a safe built-in fallback meanwhile.
        if ((iconreferences::isPortable(value) ||
             iconreferences::isLegacyPackPath(value) ||
             !iconreferences::resolve(value).isEmpty()) &&
            !existing.contains(value)) {
            existing.append(value);
        }
        if (existing.size() == kMaxRecentIcons)
            break;
    }
    if (existing != stored)
        settings.setValue(kRecentIconsKey, existing);
    return existing;
}

void addRecentIconPath(const QString& path) {
    const QString value = iconreferences::normalizeStoredValue(path);
    if (value.isEmpty() ||
        (!iconreferences::isPortable(value) &&
         !iconreferences::isLegacyPackPath(value) &&
         iconreferences::resolve(value).isEmpty())) {
        return;
    }
    QSettings settings;
    QStringList recents = settings.value(kRecentIconsKey).toStringList();
    recents.removeAll(value);
    recents.prepend(value);
    settings.setValue(kRecentIconsKey, recents.mid(0, kMaxRecentIcons));
}

}  // namespace standardicons
