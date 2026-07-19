#include "standardicons.hpp"

#include <QCoreApplication>

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

}  // namespace standardicons
