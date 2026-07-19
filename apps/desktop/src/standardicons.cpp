#include "standardicons.hpp"

#include <QCoreApplication>
#include <QDir>

namespace standardicons {

const QVector<StandardIcon>& entryIcons() {
    static const QVector<StandardIcon> icons = {
        {QStringLiteral("keys"),
         QCoreApplication::translate("standardicons", "Keys"),
         QStringLiteral(":/icons/keys.png")},
        {QStringLiteral("globe"),
         QCoreApplication::translate("standardicons", "Website"),
         QStringLiteral(":/icons/entry/globe.svg")},
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

}  // namespace standardicons
