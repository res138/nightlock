#pragma once

#include <QAbstractItemModel>
#include <QIcon>

namespace nightlock {
class Group;
}

// Tree of vault directories (entries are not exposed here). Folder
// names are editable in place; sub-folders can be added and removed.
class GroupTreeModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit GroupTreeModel(nightlock::Group* root, QObject* parent = nullptr);

    // Swaps the whole tree (nullptr = locked vault, empty model). The
    // views must drop cached indexes — this resets the model.
    void setRootGroup(nightlock::Group* root);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    // Appends a sub-folder under `parent` and returns its index.
    QModelIndex addGroup(const QModelIndex& parent, const QString& name);
    // Sets the folder's portable icon reference (legacy paths are accepted;
    // empty = default).
    bool setGroupIcon(const QModelIndex& index, const QString& path);
    // Deletes the folder (with its subtree); the root cannot be removed.
    bool removeGroup(const QModelIndex& index);

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
