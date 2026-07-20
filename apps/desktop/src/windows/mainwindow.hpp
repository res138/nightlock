#pragma once

#include <QMainWindow>

class QDialog;
class QLabel;
class QListView;
class QMenu;
class QSplitter;
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
    // Debug hook for NIGHTLOCK_SCREENSHOT_GALLERY: opens the icon pack
    // gallery popup over the window.
    QWidget* openIconGalleryForScreenshot();
    // Debug hook for NIGHTLOCK_TEST_MOVE: drops `groupName` onto
    // `targetName` through the same model path a mouse drag uses.
    void debugMoveGroup(const QString& groupName, const QString& targetName);
    // Debug hook for NIGHTLOCK_TEST_MOVE_ENTRY: moves the selected
    // entry into the named folder, like the "Move to" context menu.
    void debugMoveEntry(const QString& targetName);
    // Debug hook for NIGHTLOCK_TEST_FOLDERS: exercises folder create,
    // rename and delete through the tree model.
    void debugFolderOps();
    // Debug hook for NIGHTLOCK_TEST_ENTRY_ICON: assigns an icon path to
    // the selected entry (list + detail refresh included).
    void debugSetEntryIcon(const QString& path);
    // Debug hook for NIGHTLOCK_TEST_ENTRY_PATTERN: assigns background
    // patterns from a "[name:]kind[,name:kind…]" spec; an omitted name
    // targets the selected entry.
    void debugSetEntryPattern(const QString& spec);
    // Debug hook for NIGHTLOCK_TEST_SPOILER: drives the password
    // spoiler ("reveal" or "copied").
    void debugSpoiler(const QString& state);
    // Debug hook for NIGHTLOCK_TEST_ADD_ENTRY: creates an entry in the
    // current folder with fresh Created/Modified timestamps.
    void debugAddEntry(const QString& name);
    // Debug hook for NIGHTLOCK_SCREENSHOT_DETACHED: floats the detail
    // view as it would after a grip drag; returns it for grabbing.
    QWidget* debugDetachDetail();
    // Debug hook for NIGHTLOCK_TEST_REATTACH: docks the floating detail
    // view back through the regular drop path.
    void debugReattachDetail();

private:
    void onGroupChanged(const QModelIndex& current);
    void onEntryChanged(const QModelIndex& current);

    void showGroupMenu(const QPoint& pos);
    void showEntryMenu(const QPoint& pos);
    NlMenu* buildEntryMenu(nightlock::Entry* entry);
    NlMenu* buildMoveMenu(nightlock::Group* group, nightlock::Entry* entry, QWidget* parent);
    void moveEntryTo(nightlock::Entry* entry, nightlock::Group* target);

    void addEntryTo(nightlock::Group* group);
    void insertEntry(nightlock::Group* group, nightlock::Entry entry);
    void editEntry(nightlock::Entry* entry);
    void addFolderTo(nightlock::Group* group);
    void renameFolder(nightlock::Group* group);
    void deleteFolder(nightlock::Group* group);
    void changeFolderIcon(nightlock::Group* group);

    void detachDetail(const QPoint& globalPos);
    void maybeReattachDetail(const QPoint& globalPos);
    void dockDetail();
    void deleteEntry(nightlock::Entry* entry);

    GroupTreeModel* treeModel_;
    EntryListModel* entryModel_;
    GroupTreeView* tree_;
    QListView* list_;
    QLabel* countLabel_;
    QLabel* pathLabel_;
    EntryDetailView* detail_;
    QSplitter* splitter_;
    QList<int> detailSplitterSizes_;  // pane widths to restore on re-dock
    QWidget* dragStrip_;              // window-drag zone over the tree top

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};
