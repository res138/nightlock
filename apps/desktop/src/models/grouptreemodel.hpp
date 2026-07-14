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

    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

    nightlock::Group* rootGroup() const;
    nightlock::Group* group(const QModelIndex& index) const;
    QModelIndex indexOf(nightlock::Group* group) const;

signals:
    // Emitted after a drag-and-drop move has been applied.
    void groupMoved(nightlock::Group* group);

private:
    nightlock::Group* root_;
    QIcon folderIcon_;
};
