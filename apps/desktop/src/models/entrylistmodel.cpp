#include "entrylistmodel.hpp"

#include <QIODevice>
#include <QMimeData>

#include <cstring>

#include <nightlock/group.hpp>

namespace {

constexpr char kEntryMime[] = "application/x-nightlock-entry";

nightlock::Entry* decodeEntry(const QMimeData* data) {
    const QByteArray raw = data->data(QLatin1String(kEntryMime));
    if (raw.size() != sizeof(quintptr))
        return nullptr;
    quintptr ptr = 0;
    std::memcpy(&ptr, raw.constData(), sizeof(ptr));
    return reinterpret_cast<nightlock::Entry*>(ptr);
}

}  // namespace

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

bool EntryListModel::removeEntry(nightlock::Entry* entry) {
    const QModelIndex idx = indexOf(entry);
    if (!idx.isValid())
        return false;
    beginRemoveRows({}, idx.row(), idx.row());
    const bool removed = group_->removeEntry(entry);
    endRemoveRows();
    return removed;
}

Qt::ItemFlags EntryListModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;  // drops land between rows
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

Qt::DropActions EntryListModel::supportedDropActions() const {
    return Qt::MoveAction;
}

QStringList EntryListModel::mimeTypes() const {
    return {QLatin1String(kEntryMime)};
}

QMimeData* EntryListModel::mimeData(const QModelIndexList& indexes) const {
    if (indexes.isEmpty())
        return nullptr;
    auto* e = entry(indexes.first());
    if (!e)
        return nullptr;
    // Internal reorder within one process: the pointer is enough.
    auto* mime = new QMimeData;
    const quintptr ptr = reinterpret_cast<quintptr>(e);
    mime->setData(QLatin1String(kEntryMime),
                  QByteArray(reinterpret_cast<const char*>(&ptr), sizeof(ptr)));
    return mime;
}

bool EntryListModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int /*row*/,
                                     int /*column*/, const QModelIndex& parent) const {
    return group_ && action == Qt::MoveAction && data &&
           data->hasFormat(QLatin1String(kEntryMime)) && !parent.isValid() &&
           decodeEntry(data);
}

bool EntryListModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                  int column, const QModelIndex& parent) {
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    const QModelIndex source = indexOf(decodeEntry(data));
    if (!source.isValid())
        return false;

    const int from = source.row();
    const int to = row < 0 ? rowCount() : row;
    if (to == from || to == from + 1)
        return false;  // dropped back onto its own slot

    if (!beginMoveRows({}, from, from, {}, to))
        return false;
    group_->moveEntry(from, to);
    endMoveRows();
    return true;
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
