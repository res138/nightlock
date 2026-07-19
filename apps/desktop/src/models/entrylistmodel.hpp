#pragma once

#include <QAbstractListModel>
#include <QIcon>

namespace nightlock {
class Group;
struct Entry;
}

// Entries of the currently selected group.
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

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

private:
    nightlock::Group* group_ = nullptr;
    QIcon defaultIcon_;
};
