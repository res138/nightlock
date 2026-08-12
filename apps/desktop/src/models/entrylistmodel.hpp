#pragma once

#include <QAbstractListModel>
#include <QIcon>

#include <vector>

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
        SubtitleRole,
        SubtitleStrongRole,
        ColorRole,
        ExpiredRole,
        PasswordRole,
        UrlRole,
        NoteRole,
        ModifiedRole,
    };

    // Display order of the list. Custom is the group's own order (the
    // one drag-reordering edits); the rest are non-destructive views —
    // the underlying order is untouched and reordering is disabled.
    enum class SortMode {
        Custom,
        Created,   // newest first
        Modified,  // newest first
        Site,      // by name, A–Z
    };

    explicit EntryListModel(QObject* parent = nullptr);

    void setGroup(nightlock::Group* group);
    nightlock::Group* group() const { return group_; }
    nightlock::Entry* entry(const QModelIndex& index) const;
    QModelIndex indexOf(const nightlock::Entry* entry) const;

    SortMode sortMode() const { return sortMode_; }
    void setSortMode(SortMode mode);

    // Re-reads the group after entries were added or removed.
    void refresh();
    // Announces in-place edits of one entry (name/icon in the list).
    void notifyEntryChanged(nightlock::Entry* entry);
    // Deletes the entry from its group.
    bool removeEntry(nightlock::Entry* entry);
    // Moves one entry by a single row in Custom order.
    bool moveEntryBy(nightlock::Entry* entry, int offset);

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
    // Refills view_ from the group and applies the sort mode.
    void rebuildView();

    nightlock::Group* group_ = nullptr;
    SortMode sortMode_ = SortMode::Custom;
    std::vector<nightlock::Entry*> view_;  // rows in display order
    QIcon defaultIcon_;
};
