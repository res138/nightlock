#pragma once

#include <QMainWindow>

class QDialog;
class QLabel;
class QListView;
class QMenu;
class EntryDetailView;
class EntryListModel;
class GroupTreeModel;
class GroupTreeView;
class NlMenu;

namespace nightlock {
class Group;
struct Entry;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(nightlock::Group* root, QWidget* parent = nullptr);

    void selectGroupNamed(const QString& name);
    void selectEntryNamed(const QString& name);

    // Debug hook for NIGHTLOCK_SCREENSHOT_MENU.
    QMenu* popupEntryMenuForScreenshot();
    // Debug hook for NIGHTLOCK_SCREENSHOT_DIALOG: opens the edit dialog
    // for the selected entry (or an empty add dialog) non-modally.
    QDialog* openEntryDialogForScreenshot();
    // Debug hook for NIGHTLOCK_TEST_MOVE: drops `groupName` onto
    // `targetName` through the same model path a mouse drag uses.
    void debugMoveGroup(const QString& groupName, const QString& targetName);
    // Debug hook for NIGHTLOCK_TEST_FOLDERS: exercises folder create,
    // rename and delete through the tree model.
    void debugFolderOps();

private:
    void onGroupChanged(const QModelIndex& current);
    void onEntryChanged(const QModelIndex& current);

    void showGroupMenu(const QPoint& pos);
    void showEntryMenu(const QPoint& pos);
    NlMenu* buildEntryMenu(nightlock::Entry* entry);
    NlMenu* buildMoveMenu(nightlock::Group* group, QWidget* parent);

    void addEntryTo(nightlock::Group* group);
    void editEntry(nightlock::Entry* entry);
    void addFolderTo(nightlock::Group* group);
    void renameFolder(nightlock::Group* group);
    void deleteFolder(nightlock::Group* group);

    GroupTreeModel* treeModel_;
    EntryListModel* entryModel_;
    GroupTreeView* tree_;
    QListView* list_;
    QLabel* countLabel_;
    QLabel* pathLabel_;
    EntryDetailView* detail_;
};
