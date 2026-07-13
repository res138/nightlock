#pragma once

#include <QMainWindow>

class QLabel;
class QListView;
class QTreeView;
class EntryDetailView;
class EntryListModel;
class GroupTreeModel;

namespace nightlock {
class Group;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(nightlock::Group* root, QWidget* parent = nullptr);

    void selectGroupNamed(const QString& name);
    void selectEntryNamed(const QString& name);

private:
    void onGroupChanged(const QModelIndex& current);
    void onEntryChanged(const QModelIndex& current);

    GroupTreeModel* treeModel_;
    EntryListModel* entryModel_;
    QTreeView* tree_;
    QListView* list_;
    QLabel* countLabel_;
    QLabel* pathLabel_;
    EntryDetailView* detail_;
};
