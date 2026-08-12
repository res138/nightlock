#pragma once

#include <QHash>
#include <QMainWindow>

#include "models/entrylistmodel.hpp"

class QDialog;
class QHBoxLayout;
class QLabel;
class QListView;
class QMenu;
class QMenuBar;
class QShortcut;
class QSplitter;
class QToolButton;
class QVariantAnimation;
class EntryDetailView;
class CompactEntryView;
class GraphWindow;
class GroupTreeModel;
class GroupTreeView;
class LockScreen;
class NlMenu;
class PasswordGeneratorWindow;
class SearchWindow;
class SettingsWindow;

namespace nightlock {
class Group;
struct Entry;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(nightlock::Group* root, QWidget* parent = nullptr);

    // First-run/normal startup for a real (non-demo) vault: opens the
    // remembered vault behind the lock screen, or lands on the
    // first-run create screen when none is known.
    void startLocked();

    // Locks the current session and retargets Nightlock at another
    // vault file, which becomes the startup default right away.
    void switchToVault(const QString& path);
    // Locks the current session and walks the create flow at `path`;
    // the new vault becomes the default once it exists.
    void createVaultAt(const QString& path);
    // Locks, forgets the startup default and returns to the first-run
    // screen — Nightlock keeps no memory of the vault.
    void signOutVault();

    void selectGroupNamed(const QString& name);
    void selectEntryNamed(const QString& name);

    // Debug hook for NIGHTLOCK_SCREENSHOT_MENU.
    QMenu* popupEntryMenuForScreenshot();
    // Debug hook for NIGHTLOCK_SCREENSHOT_SORT_MENU: drops the sort
    // menu from the list-header funnel button.
    QMenu* popupSortMenuForScreenshot();
    // Debug hook for NIGHTLOCK_TEST_SORT: applies a sort mode
    // ("custom" | "created" | "modified" | "site").
    void debugSetSort(const QString& mode);
    // Debug hook for NIGHTLOCK_TEST_TREE_PANE: "hide" or "show" drives
    // the folder-panel toggle.
    void debugSetTreePane(const QString& state);
    // Same, after a delay — lets NIGHTLOCK_SCREENSHOT catch the curtain
    // animation mid-flight.
    void debugSetTreePaneDelayed(const QString& state, int delayMs);
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
    // Debug hook for NIGHTLOCK_TEST_RENAME: opens the inline rename
    // editor on the named folder, like the context menu does.
    void debugRenameFolder(const QString& name);
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
    // Debug hook for NIGHTLOCK_SCREENSHOT_STANDALONE: opens the
    // selected entry through the permanent context-menu window path.
    QWidget* openSelectedEntryStandaloneForScreenshot();
    // Debug hook for NIGHTLOCK_SCREENSHOT_GRAPH: opens the graph
    // window and returns it for grabbing.
    QWidget* openGraphForScreenshot();
    // Debug hook for NIGHTLOCK_SCREENSHOT_SEARCH: opens the search
    // popup, pre-fills `query` and returns it for grabbing.
    QWidget* openSearchForScreenshot(const QString& query);
    // Debug hook for NIGHTLOCK_SCREENSHOT_GENERATOR: opens the
    // standalone password generator and returns it for grabbing.
    QWidget* openPasswordGeneratorForScreenshot();
    // Debug hook for NIGHTLOCK_SCREENSHOT_SETTINGS: opens the Settings
    // window on the given category row and returns it for grabbing.
    QWidget* openSettingsForScreenshot(int category);
    // Debug hook for NIGHTLOCK_SCREENSHOT_LOCK: locks the vault;
    // `fail` first submits a wrong password for the error state.
    void debugLock(bool fail);
    // Debug hook for NIGHTLOCK_TEST_PASSWORD: feeds a password to the
    // lock screen flow (Create and Unlock alike).
    void debugSubmitPassword(const QString& password);
    // Debug hook for NIGHTLOCK_TEST_VAULT_TARGET: retargets the create
    // flow's location row, standing in for the Select Folder dialog.
    void debugSetVaultTarget(const QString& path);
    // Debug hook for NIGHTLOCK_TEST_REATTACH: docks the floating detail
    // view back through the regular drop path.
    void debugReattachDetail();

private:
    void onGroupChanged(const QModelIndex& current);
    void onEntryChanged(const QModelIndex& current);

    QWidget* buildTreeHeader();
    QWidget* buildListHeader();
    void buildGlobalMenu();
    nightlock::Group* currentGroup() const;
    NlMenu* showSortMenu();
    void applySortMode(EntryListModel::SortMode mode);
    void setTreePaneVisible(bool visible);
    void fadeReopenButton(bool shown);

    void showGroupMenu(const QPoint& pos);
    void showEntryMenu(const QPoint& pos);
    void showCompactEntryMenu(const QPoint& pos);
    void popupEntryMenu(nightlock::Entry* entry,
                        const QList<nightlock::Entry*>& selected,
                        const QPoint& globalPos);
    NlMenu* buildEntryMenu(nightlock::Entry* entry);
    void populateCopyAttributeMenu(QMenu* menu, nightlock::Entry* entry,
                                   bool includeUrl = true);
    NlMenu* buildMoveMenu(nightlock::Group* group, const QList<nightlock::Entry*>& entries,
                          QWidget* parent);
    void prependMoveTarget(NlMenu* menu, nightlock::Group* target,
                           const QList<nightlock::Entry*>& entries, const QString& title);
    void moveEntriesTo(const QList<nightlock::Entry*>& entries, nightlock::Group* target);
    void deleteEntries(const QList<nightlock::Entry*>& entries);

    void addEntryTo(nightlock::Group* group);
    void insertEntry(nightlock::Group* group, nightlock::Entry entry);
    void editEntry(nightlock::Entry* entry);
    void addFolderTo(nightlock::Group* group);
    void renameFolder(nightlock::Group* group);
    void deleteFolder(nightlock::Group* group);
    void deleteFolders(const QList<nightlock::Group*>& groups);
    void changeFolderIcon(const QList<nightlock::Group*>& groups);

    void detachDetail(const QPoint& globalPos);
    void maybeReattachDetail(const QPoint& globalPos);
    void dockDetail();
    void openEntryInSeparateWindow(nightlock::Entry* entry);
    void closeStandaloneEntryWindow(const nightlock::Entry* entry);
    void closeStandaloneWindowsInGroup(const nightlock::Group* group);
    void closeAllStandaloneEntryWindows();
    void deleteEntry(nightlock::Entry* entry);

    void openGraph();
    void refreshGraph();
    void syncNetGraphAvailability();
    void syncGeneralToolbarVisibility();
    void syncCompactMode();
    SearchWindow* openSearch();
    PasswordGeneratorWindow* openPasswordGenerator();
    SettingsWindow* openSettings();
    void openVaultDialog();
    void createVaultDialog();
    void saveVaultAs();
    void closeDatabase();
    void lockVault();
    // Closes every vault-showing surface and wipes the session.
    void closeVaultSession();
    // Covers the window with the lock screen (Create or Unlock), the
    // location row prefilled from the service's current path.
    void showLockScreen(bool create);
    // Window title = the vault file's name.
    void updateVaultTitle();
    // Points both models (and the detail pane) at a new tree; nullptr
    // empties everything, which is the locked state.
    void setVaultRoot(nightlock::Group* root);
    // Verifies a lock-screen submission against the vault service.
    void handlePassword(const QString& password);
    // Retrieves the per-vault password from Keychain after macOS has
    // accepted the enrolled fingerprint.
    void handleTouchId();
    void moveCurrentEntry(int offset);
    void setAlwaysOnTop(bool enabled);
    void fillWindow();
    void centerWindow();
    // Hides the lock screen and shows `root`.
    void finishUnlock(nightlock::Group* root);
    void revealInVault(nightlock::Group* group, nightlock::Entry* entry);

    GroupTreeModel* treeModel_;
    EntryListModel* entryModel_;
    GroupTreeView* tree_;
    QWidget* treePane_;               // the leftmost splitter pane (a SlidingPane)
    QWidget* treeInner_;              // header + tree; slides inside treePane_
    QVariantAnimation* paneAnimation_ = nullptr;
    QListView* list_;
    CompactEntryView* compactView_ = nullptr;
    QLabel* countLabel_;
    QLabel* pathLabel_;
    QHBoxLayout* listHeaderLayout_;
    QToolButton* filterButton_;
    QToolButton* newFolderButton_ = nullptr;
    QToolButton* searchButton_ = nullptr;
    QToolButton* graphButton_ = nullptr;
    QToolButton* lockButton_ = nullptr;
    QToolButton* reopenTreeButton_;   // lives in the list header, shown
                                      // only while the tree pane is hidden
    QShortcut* graphShortcut_ = nullptr;
    QMenu* entryMenu_ = nullptr;       // rebuilt on show; cleared before vault wipe
    QMenu* directoryMenu_ = nullptr;   // likewise may hold Group* callbacks
    EntryDetailView* detail_;
    QHash<const nightlock::Entry*, EntryDetailView*> standaloneDetails_;
    GraphWindow* graph_ = nullptr;     // the one graph window, if open
    PasswordGeneratorWindow* passwordGenerator_ = nullptr;
    SearchWindow* search_ = nullptr;   // the one search window, if open
    SettingsWindow* settings_ = nullptr;  // the one settings window, if open
    QMenuBar* globalMenuBar_ = nullptr;   // parentless default menu bar on macOS
    LockScreen* lockScreen_ = nullptr; // covers the window while locked
    QSplitter* splitter_;
    QList<int> detailSplitterSizes_;  // pane widths to restore on re-dock
    QList<int> compactSplitterSizes_; // three-pane widths to restore after compact mode
    QList<int> treeSplitterSizes_;    // pane widths to restore on reopen
    bool alwaysOnTop_ = false;
    bool compactModeActive_ = false;

protected:
    void resizeEvent(QResizeEvent* event) override;
};
