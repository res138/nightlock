#include "grouptreemodel.hpp"

#include <QMimeData>

#include <cstring>

#include <nightlock/group.hpp>

#include "iconutils.hpp"

namespace {

constexpr char kGroupMime[] = "application/x-nightlock-group";

nightlock::Group* decodeGroup(const QMimeData* data) {
    const QByteArray raw = data->data(QLatin1String(kGroupMime));
    if (raw.size() != sizeof(quintptr))
        return nullptr;
    quintptr ptr = 0;
    std::memcpy(&ptr, raw.constData(), sizeof(ptr));
    return reinterpret_cast<nightlock::Group*>(ptr);
}

}  // namespace

GroupTreeModel::GroupTreeModel(nightlock::Group* root, QObject* parent)
    : QAbstractItemModel(parent),
      root_(root),
      folderIcon_(iconutils::iconWithWhiteKnockedOut(QStringLiteral(":/icons/folder.jpg"))) {}

QModelIndex GroupTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent))
        return {};
    if (!parent.isValid())
        return createIndex(0, 0, root_);
    return createIndex(row, 0, group(parent)->groups()[row].get());
}

QModelIndex GroupTreeModel::parent(const QModelIndex& child) const {
    auto* g = group(child);
    if (!g || g == root_)
        return {};
    return indexOf(g->parent());
}

int GroupTreeModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid())
        return 1;
    return static_cast<int>(group(parent)->groups().size());
}

int GroupTreeModel::columnCount(const QModelIndex&) const {
    return 1;
}

QVariant GroupTreeModel::data(const QModelIndex& index, int role) const {
    auto* g = group(index);
    if (!g)
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return QString::fromStdString(g->name());
    case Qt::DecorationRole:
        if (!g->icon().empty())
            return QIcon(QString::fromStdString(g->icon()));
        return folderIcon_;
    }
    return {};
}

bool GroupTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    auto* g = group(index);
    if (!g || role != Qt::EditRole)
        return false;
    const QString name = value.toString().trimmed();
    if (name.isEmpty() || name == QString::fromStdString(g->name()))
        return false;
    g->setName(name.toStdString());
    emit dataChanged(index, index, {Qt::DisplayRole});
    return true;
}

QModelIndex GroupTreeModel::addGroup(const QModelIndex& parent, const QString& name) {
    auto* parentGroup = group(parent);
    if (!parentGroup || name.trimmed().isEmpty())
        return {};
    const int row = static_cast<int>(parentGroup->groups().size());
    beginInsertRows(parent, row, row);
    auto& created = parentGroup->addGroup(name.trimmed().toStdString());
    endInsertRows();
    return indexOf(&created);
}

bool GroupTreeModel::removeGroup(const QModelIndex& index) {
    auto* g = group(index);
    if (!g || g == root_)
        return false;
    beginRemoveRows(indexOf(g->parent()), g->indexInParent(), g->indexInParent());
    const bool removed = g->parent()->removeGroup(g);
    endRemoveRows();
    return removed;
}

Qt::ItemFlags GroupTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;  // empty area accepts drops (into Root)
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    if (group(index) != root_)
        f |= Qt::ItemIsDragEnabled | Qt::ItemIsEditable;
    return f;
}

Qt::DropActions GroupTreeModel::supportedDropActions() const {
    return Qt::MoveAction;
}

QStringList GroupTreeModel::mimeTypes() const {
    return {QLatin1String(kGroupMime)};
}

QMimeData* GroupTreeModel::mimeData(const QModelIndexList& indexes) const {
    if (indexes.isEmpty())
        return nullptr;
    auto* g = group(indexes.first());
    if (!g || g == root_)
        return nullptr;
    // Internal move within one process: encoding the pointer is enough.
    auto* mime = new QMimeData;
    const quintptr ptr = reinterpret_cast<quintptr>(g);
    mime->setData(QLatin1String(kGroupMime),
                  QByteArray(reinterpret_cast<const char*>(&ptr), sizeof(ptr)));
    return mime;
}

bool GroupTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                     int /*column*/, const QModelIndex& parent) const {
    if (action != Qt::MoveAction || !data || !data->hasFormat(QLatin1String(kGroupMime)))
        return false;
    auto* moving = decodeGroup(data);
    if (!moving)
        return false;
    if (!parent.isValid() && row >= 0)
        return false;  // nothing can sit beside Root at the top level
    nightlock::Group* target = parent.isValid() ? group(parent) : root_;
    return target && moving != target && !moving->isAncestorOf(target);
}

bool GroupTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                  int column, const QModelIndex& parent) {
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    auto* moving = decodeGroup(data);
    nightlock::Group* targetParent = parent.isValid() ? group(parent) : root_;

    const int destRow = row < 0 ? static_cast<int>(targetParent->groups().size()) : row;
    nightlock::Group* sourceParent = moving->parent();
    const int sourceRow = moving->indexInParent();
    if (sourceParent == targetParent && (destRow == sourceRow || destRow == sourceRow + 1))
        return false;  // dropped back onto its own slot

    if (!beginMoveRows(indexOf(sourceParent), sourceRow, sourceRow, indexOf(targetParent),
                       destRow))
        return false;
    nightlock::Group::reparent(*moving, *targetParent, destRow);
    endMoveRows();
    emit groupMoved(moving);
    return true;
}

nightlock::Group* GroupTreeModel::rootGroup() const {
    return root_;
}

nightlock::Group* GroupTreeModel::group(const QModelIndex& index) const {
    if (!index.isValid())
        return nullptr;
    return static_cast<nightlock::Group*>(index.internalPointer());
}

QModelIndex GroupTreeModel::indexOf(nightlock::Group* group) const {
    if (!group)
        return {};
    if (group == root_)
        return createIndex(0, 0, root_);
    return createIndex(group->indexInParent(), 0, group);
}
