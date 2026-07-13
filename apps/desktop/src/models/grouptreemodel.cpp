#include "grouptreemodel.hpp"

#include <nightlock/group.hpp>

#include "iconutils.hpp"

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
