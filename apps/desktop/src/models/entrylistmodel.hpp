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

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

private:
    nightlock::Group* group_ = nullptr;
    QIcon defaultIcon_;
};
