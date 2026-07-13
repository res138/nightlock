#pragma once

#include <QAbstractItemModel>
#include <QIcon>

namespace nightlock {
class Group;
}

// Read-only tree of vault directories (entries are not exposed here).
class GroupTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit GroupTreeModel(nightlock::Group* root, QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    nightlock::Group* rootGroup() const;
    nightlock::Group* group(const QModelIndex& index) const;
    QModelIndex indexOf(nightlock::Group* group) const;

private:
    nightlock::Group* root_;
    QIcon folderIcon_;
};
