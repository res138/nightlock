#include "entrylistmodel.hpp"

#include <nightlock/group.hpp>

EntryListModel::EntryListModel(QObject* parent)
    : QAbstractListModel(parent), defaultIcon_(QStringLiteral(":/icons/entry.png")) {}

void EntryListModel::setGroup(nightlock::Group* group) {
    beginResetModel();
    group_ = group;
    endResetModel();
}

nightlock::Entry* EntryListModel::entry(const QModelIndex& index) const {
    if (!group_ || !index.isValid() || index.row() >= rowCount())
        return nullptr;
    return group_->entries()[index.row()].get();
}

QModelIndex EntryListModel::indexOf(const nightlock::Entry* entry) const {
    if (!group_ || !entry)
        return {};
    for (int row = 0; row < rowCount(); ++row)
        if (group_->entries()[row].get() == entry)
            return index(row, 0);
    return {};
}

void EntryListModel::refresh() {
    beginResetModel();
    endResetModel();
}

void EntryListModel::notifyEntryChanged(nightlock::Entry* entry) {
    const QModelIndex idx = indexOf(entry);
    if (idx.isValid())
        emit dataChanged(idx, idx);
}

int EntryListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !group_)
        return 0;
    return static_cast<int>(group_->entries().size());
}

QVariant EntryListModel::data(const QModelIndex& index, int role) const {
    auto* e = entry(index);
    if (!e)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(e->name);
    case LoginRole:
        return QString::fromStdString(e->login);
    case Qt::DecorationRole:
        if (!e->icon.empty())
            return QIcon(QString::fromStdString(e->icon));
        return defaultIcon_;
    }
    return {};
}
