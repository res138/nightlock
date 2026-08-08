#include "entrylistmodel.hpp"

#include <QIODevice>
#include <QMimeData>

#include <algorithm>
#include <cstring>

#include <nightlock/group.hpp>

#include "vaultservice.hpp"
#include "expirationui.hpp"

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
    rebuildView();
    endResetModel();
}

void EntryListModel::setSortMode(SortMode mode) {
    if (mode == sortMode_)
        return;
    beginResetModel();
    sortMode_ = mode;
    rebuildView();
    endResetModel();
}

// Ties keep the group's own order (stable sort), so switching modes
// back and forth is deterministic.
void EntryListModel::rebuildView() {
    view_.clear();
    if (!group_)
        return;
    view_.reserve(group_->entries().size());
    for (const auto& e : group_->entries())
        view_.push_back(e.get());
    switch (sortMode_) {
    case SortMode::Custom:
        break;
    case SortMode::Created:
        std::stable_sort(view_.begin(), view_.end(),
                         [](const auto* a, const auto* b) { return a->created > b->created; });
        break;
    case SortMode::Modified:
        std::stable_sort(view_.begin(), view_.end(),
                         [](const auto* a, const auto* b) { return a->modified > b->modified; });
        break;
    case SortMode::Site:
        std::stable_sort(view_.begin(), view_.end(), [](const auto* a, const auto* b) {
            return QString::compare(QString::fromStdString(a->name),
                                    QString::fromStdString(b->name), Qt::CaseInsensitive) < 0;
        });
        break;
    }
}

nightlock::Entry* EntryListModel::entry(const QModelIndex& index) const {
    if (!index.isValid() || index.row() >= rowCount())
        return nullptr;
    return view_[index.row()];
}

QModelIndex EntryListModel::indexOf(const nightlock::Entry* entry) const {
    if (!entry)
        return {};
    for (int row = 0; row < rowCount(); ++row)
        if (view_[row] == entry)
            return index(row, 0);
    return {};
}

void EntryListModel::refresh() {
    beginResetModel();
    rebuildView();
    endResetModel();
}

void EntryListModel::notifyEntryChanged(nightlock::Entry* entry) {
    if (sortMode_ != SortMode::Custom) {
        // The edit may have moved the row (new name or Modified date);
        // re-sort. Callers re-select by pointer afterwards.
        refresh();
        return;
    }
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
    rebuildView();
    endRemoveRows();
    if (removed)
        VaultService::instance()->markDirty();
    return removed;
}

bool EntryListModel::moveEntryBy(nightlock::Entry* entry, int offset) {
    if (sortMode_ != SortMode::Custom || (offset != -1 && offset != 1))
        return false;
    const QModelIndex source = indexOf(entry);
    if (!source.isValid())
        return false;
    const int from = source.row();
    const int target = from + offset;
    if (target < 0 || target >= rowCount())
        return false;
    const int destination = offset < 0 ? target : target + 1;
    if (!beginMoveRows({}, from, from, {}, destination))
        return false;
    group_->moveEntry(from, destination);
    rebuildView();
    endMoveRows();
    VaultService::instance()->markDirty();
    return true;
}

Qt::ItemFlags EntryListModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;  // drops land between rows
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (sortMode_ == SortMode::Custom)  // sorted views are read-only orders
        flags |= Qt::ItemIsDragEnabled;
    return flags;
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
    return group_ && sortMode_ == SortMode::Custom && action == Qt::MoveAction && data &&
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
    // Only reachable in Custom mode, where view rows equal group rows.
    group_->moveEntry(from, to);
    rebuildView();
    endMoveRows();
    VaultService::instance()->markDirty();
    return true;
}

int EntryListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(view_.size());
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
    case SubtitleRole:
        switch (e->preset) {
            case nightlock::EntryPreset::Classic:
                return QString::fromStdString(e->login);
            case nightlock::EntryPreset::Wifi:
                return tr("Wi-Fi Hotspot");
            case nightlock::EntryPreset::BankCard:
                return tr("Bank Card");
            case nightlock::EntryPreset::BrowserBookmark:
                return tr("Browser Bookmark");
            case nightlock::EntryPreset::CryptoWallet:
                return tr("cryptowallet");
        }
        return {};
    case SubtitleStrongRole:
        return e->preset != nightlock::EntryPreset::Classic;
    case ColorRole:
        return static_cast<int>(e->color);
    case ExpiredRole:
        return expirationui::isExpired(*e);
    case Qt::DecorationRole:
        if (!e->icon.empty())
            return QIcon(QString::fromStdString(e->icon));
        return defaultIcon_;
    }
    return {};
}
