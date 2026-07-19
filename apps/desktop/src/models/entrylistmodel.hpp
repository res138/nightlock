#pragma once

#include <QAbstractListModel>
#include <QIcon>

namespace nightlock {
class Group;
struct Entry;
}

// Entries of the currently selected group. Rows can be dragged to
// reorder them within the group.
class EntryListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        LoginRole,
    };

    explicit EntryListModel(QObject* parent = nullptr);

    void setGroup(nightlock::Group* group);
    nightlock::Entry* entry(const QModelIndex& index) const;
    QModelIndex indexOf(const nightlock::Entry* entry) const;

    // Re-reads the group after entries were added or removed.
    void refresh();
    // Announces in-place edits of one entry (name/icon in the list).
    void notifyEntryChanged(nightlock::Entry* entry);
    // Deletes the entry from its group.
    bool removeEntry(nightlock::Entry* entry);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

private:
    nightlock::Group* group_ = nullptr;
    QIcon defaultIcon_;
};
