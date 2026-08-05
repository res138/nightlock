#include "mainwindow.hpp"

#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWindow>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>

#include <nightlock/group.hpp>

#include "appearancesettings.hpp"
#include "graphsettings.hpp"
#include "hotkeys.hpp"
#include "models/grouptreemodel.hpp"
#include "qsecure.hpp"
#include "respaths.hpp"
#include "standardicons.hpp"
#include "vaultservice.hpp"
#include "widgets/entrydetailview.hpp"
#include "widgets/entrylistdelegate.hpp"
#include "widgets/grouptreeview.hpp"
#include "widgets/icongallerypopup.hpp"
#include "widgets/nlmenu.hpp"
#include "widgets/overlayscrollbar.hpp"
#include "widgets/searchwindow.hpp"
#include "windows/entryeditdialog.hpp"
#include "windows/graphwindow.hpp"
#include "windows/lockscreen.hpp"
#include "windows/settingswindow.hpp"

#ifdef Q_OS_MACOS
#include "platform/macwindow.hpp"
#endif

namespace {

// Height shared by the tree-pane and list-pane toolbar headers, so
// their bottom borders form one continuous line.
constexpr int kHeaderHeight = 46;
// Traffic-light row: left margin and the vertical center inside the
// tree header (the buttons are repositioned to match on macOS).
constexpr int kTrafficLeft = 20;
constexpr int kTrafficSpan = 66;  // 3 buttons, 22pt pitch
// List-header geometry while the tree pane is hidden: the reopen
// button sits where the traffic lights end, the labels right after it.
constexpr int kReopenButtonX = kTrafficLeft + kTrafficSpan + 8;
constexpr int kHiddenLabelShift = kReopenButtonX + 28 + 4;

nightlock::Group* findGroup(nightlock::Group* group, const QString& name) {
    if (QString::fromStdString(group->name()) == name)
        return group;
    for (const auto& sub : group->groups())
        if (auto* found = findGroup(sub.get(), name))
            return found;
    return nullptr;
}

QIcon menuIcon(const QString& name) {
    // Theme-following: the glyph lightens with the dark scheme.
    return appearancesettings::themedMenuIcon(name);
}

// Toolbar strip at the top of a pane. Empty areas drag the whole
// window, replacing the old title bar.
class HeaderBar : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
    }
};

// Left pane that can leave like a curtain: while a slide runs, the
// inner content keeps its full width and hangs onto the pane's right
// edge, so a narrowing pane pushes it out over the window edge instead
// of squeezing it. Outside a slide the content simply fills the pane.
class SlidingPane : public QWidget {
public:
    explicit SlidingPane(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumWidth(kMinWidth);
    }
    void setInner(QWidget* inner) { inner_ = inner; }

    void beginSlide(int innerWidth) {
        sliding_ = true;
        setMinimumWidth(0);
        inner_->resize(innerWidth, height());
        inner_->move(width() - innerWidth, 0);
    }

    void endSlide() {
        sliding_ = false;
        setMinimumWidth(kMinWidth);
        inner_->setGeometry(rect());
    }

protected:
    void resizeEvent(QResizeEvent*) override {
        if (!inner_)
            return;
        if (sliding_)
            inner_->move(width() - inner_->width(), 0);
        else
            inner_->setGeometry(rect());
    }

private:
    static constexpr int kMinWidth = 140;
    QWidget* inner_ = nullptr;
    bool sliding_ = false;
};

QToolButton* headerButton(const QString& iconName, const QString& toolTip) {
    auto* button = new QToolButton;
    button->setObjectName(QStringLiteral("headerIconButton"));
    button->setIcon(menuIcon(iconName));
    button->setIconSize(QSize(17, 17));
    button->setFixedSize(28, 28);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(toolTip);
    return button;
}

}  // namespace

MainWindow::MainWindow(nightlock::Group* root, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Primary.nlck"));

    treeModel_ = new GroupTreeModel(root, this);
    tree_ = new GroupTreeView;
    tree_->setModel(treeModel_);
    tree_->expandAll();
    // Folder renames and deletions ripple into an open graph window;
    // entry edits call refreshGraph() from their mutation paths.
    connect(treeModel_, &QAbstractItemModel::dataChanged, this, &MainWindow::refreshGraph);
    connect(treeModel_, &QAbstractItemModel::rowsRemoved, this, &MainWindow::refreshGraph);

    auto* treePane = new SlidingPane;
    treePane_ = treePane;
    treeInner_ = new QWidget(treePane_);
    auto* treePaneLayout = new QVBoxLayout(treeInner_);
    treePaneLayout->setContentsMargins(0, 0, 0, 0);
    treePaneLayout->setSpacing(0);
    treePaneLayout->addWidget(buildTreeHeader());
    treePaneLayout->addWidget(tree_, 1);
    treePane->setInner(treeInner_);

    entryModel_ = new EntryListModel(this);
    list_ = new QListView;
    list_->setObjectName(QStringLiteral("entryList"));
    list_->setModel(entryModel_);
    list_->setItemDelegate(new EntryListDelegate(list_));
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Entries reorder by drag within the group.
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setDragEnabled(true);
    list_->setAcceptDrops(true);
    list_->setDropIndicatorShown(true);
    list_->setDefaultDropAction(Qt::MoveAction);

    auto* middle = new QWidget;
    auto* middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);
    middleLayout->addWidget(buildListHeader());
    middleLayout->addWidget(list_, 1);

    detail_ = new EntryDetailView;

    splitter_ = new QSplitter;
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(1);
    splitter_->addWidget(treePane_);
    splitter_->addWidget(middle);
    splitter_->addWidget(detail_);
    splitter_->setSizes({375, 420, 460});
    setCentralWidget(splitter_);

    // The ⌘-shortcuts (Ctrl on other platforms), all rebindable from
    // Settings → Hotkeys, which retargets these live QShortcuts.
    // Vault-touching ones stay dead while locked; Settings is
    // app-level and works even then. Registration order is the row
    // order on the Hotkeys page.
    hotkeys::bind(QStringLiteral("new-entry"), tr("New entry"), QKeySequence(QKeySequence::New),
                  this, [this] {
                      if (!lockScreen_->isVisible())
                          addEntryTo(currentGroup() ? currentGroup() : treeModel_->rootGroup());
                  });
    hotkeys::bind(QStringLiteral("new-folder"), tr("New folder"),
                  QKeySequence(QStringLiteral("Ctrl+T")), this, [this] {
                      if (!lockScreen_->isVisible())
                          addFolderTo(currentGroup() ? currentGroup() : treeModel_->rootGroup());
                  });
    hotkeys::bind(QStringLiteral("search"), tr("Search"), QKeySequence(QKeySequence::Find), this,
                  [this] { openSearch(); });
    graphShortcut_ = hotkeys::bind(QStringLiteral("graph"), tr("NetGraph view"),
                                   QKeySequence(QStringLiteral("Ctrl+G")), this,
                                   [this] { openGraph(); });
    hotkeys::bind(QStringLiteral("lock"), tr("Lock vault"),
                  QKeySequence(QStringLiteral("Ctrl+L")), this, [this] { lockVault(); });
    hotkeys::bind(QStringLiteral("toggle-folder-panel"), tr("Toggle folder panel"),
                  QKeySequence(QStringLiteral("Ctrl+D")), this, [this] {
                      if (!lockScreen_->isVisible())
                          setTreePaneVisible(!treePane_->isVisible());
                  });
    hotkeys::bind(QStringLiteral("settings"), tr("Settings"),
                  QKeySequence(QStringLiteral("Ctrl+,")), this, [this] { openSettings(); });

    lockScreen_ = new LockScreen(this);
    lockScreen_->hide();
    connect(lockScreen_, &LockScreen::passwordSubmitted, this,
            &MainWindow::handlePassword);
    connect(lockScreen_, &LockScreen::openExistingRequested, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open Vault"),
            QFileInfo(VaultService::instance()->vaultPath()).absolutePath(),
            tr("Nightlock Vault (*.nlck)"));
        if (!path.isEmpty())
            switchToVault(path);
    });
    // Forgetting is the whole point: the same flow as Settings' Sign
    // Out, landing on the first-run screen.
    connect(lockScreen_, &LockScreen::forgotPasswordRequested, this,
            &MainWindow::signOutVault);
    // A failed autosave must not pass silently — the user thinks their
    // edit is on disk.
    connect(VaultService::instance(), &VaultService::saveFailed, this,
            [this](const QString& reason) {
                auto* box = new QMessageBox(
                    QMessageBox::Warning, tr("Save Failed"),
                    tr("The vault could not be saved: %1").arg(reason),
                    QMessageBox::Ok, this);
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->open();
            });

    connect(detail_, &EntryDetailView::detachRequested, this, &MainWindow::detachDetail);
    connect(detail_, &EntryDetailView::dropped, this, &MainWindow::maybeReattachDetail);
    connect(detail_, &EntryDetailView::dockRequested, this, &MainWindow::dockDetail);
    connect(detail_, &EntryDetailView::graphRequested, this, [this] {
        auto* entry = entryModel_->entry(list_->currentIndex());
        openGraph();
        if (graph_ && entry)
            graph_->focusEntry(entry);
    });
    connect(detail_, &EntryDetailView::editRequested, this, [this] {
        if (auto* entry = entryModel_->entry(list_->currentIndex()))
            editEntry(entry);
    });

    connect(graphsettings::notifier(), &graphsettings::Notifier::changed, this,
            &MainWindow::syncNetGraphAvailability);
    syncNetGraphAvailability();

    new OverlayScrollBar(tree_);
    new OverlayScrollBar(list_);

    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) { onGroupChanged(current); });
    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) { onEntryChanged(current); });

    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, &MainWindow::showGroupMenu);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QWidget::customContextMenuRequested, this, &MainWindow::showEntryMenu);

    connect(treeModel_, &QAbstractItemModel::dataChanged, this, [this] {
        // A folder rename may change the path shown under the counter.
        if (auto* current = treeModel_->group(tree_->currentIndex()))
            pathLabel_->setText(QString::fromStdString(current->path()));
    });

    connect(treeModel_, &GroupTreeModel::groupMoved, this, [this](nightlock::Group* moved) {
        // Let the view settle after the move, then show where it landed.
        QTimer::singleShot(0, this, [this, moved] {
            const QModelIndex idx = treeModel_->indexOf(moved);
            tree_->scrollTo(idx);
            tree_->flashRow(idx);
            if (auto* current = treeModel_->group(tree_->currentIndex()))
                pathLabel_->setText(QString::fromStdString(current->path()));
        });
    });

    detail_->setEntry(nullptr);
}

// Toolbar over the directory pane: the traffic lights keep their
// corner (repositioned to the strip's vertical center on macOS), the
// panel toggle sits right after them on its own, and every other
// action — folder / search / graph / lock / settings — is
// right-aligned like the list-header icons.
QWidget* MainWindow::buildTreeHeader() {
    auto* header = new HeaderBar;
    header->setObjectName(QStringLiteral("treeHeader"));
    header->setFixedHeight(kHeaderHeight);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(kTrafficLeft + kTrafficSpan + 4, 0, 14, 0);
    layout->setSpacing(4);

    auto* closePane = headerButton(QStringLiteral("sidebar"), tr("Hide folder panel"));
    connect(closePane, &QToolButton::clicked, this, [this] { setTreePaneVisible(false); });
    layout->addWidget(closePane);
    layout->addStretch(1);

    auto* newFolder = headerButton(QStringLiteral("folder-plus"), tr("New folder"));
    connect(newFolder, &QToolButton::clicked, this, [this] { addFolderTo(currentGroup()); });
    layout->addWidget(newFolder);

    auto* searchButton = headerButton(QStringLiteral("search"), tr("Search"));
    connect(searchButton, &QToolButton::clicked, this, &MainWindow::openSearch);
    layout->addWidget(searchButton);
    graphButton_ = headerButton(QStringLiteral("graph"), tr("NetGraph"));
    connect(graphButton_, &QToolButton::clicked, this, &MainWindow::openGraph);
    layout->addWidget(graphButton_);
    auto* lockButton = headerButton(QStringLiteral("lock"), tr("Lock vault"));
    connect(lockButton, &QToolButton::clicked, this, &MainWindow::lockVault);
    layout->addWidget(lockButton);
    auto* settingsButton = headerButton(QStringLiteral("settings"), tr("Settings"));
    connect(settingsButton, &QToolButton::clicked, this, &MainWindow::openSettings);
    layout->addWidget(settingsButton);
    return header;
}

// Header of the middle pane: the item counter and path on the left,
// the sort and new-entry actions on the right.
QWidget* MainWindow::buildListHeader() {
    auto* header = new HeaderBar;
    header->setObjectName(QStringLiteral("listHeader"));
    header->setFixedHeight(kHeaderHeight);
    listHeaderLayout_ = new QHBoxLayout(header);
    listHeaderLayout_->setContentsMargins(20, 0, 14, 0);
    listHeaderLayout_->setSpacing(4);

    // Way back into the hidden tree pane. An overlay outside the layout
    // flow: it fades in/out without shoving the labels around.
    reopenTreeButton_ = headerButton(QStringLiteral("sidebar"), tr("Show folder panel"));
    connect(reopenTreeButton_, &QToolButton::clicked, this,
            [this] { setTreePaneVisible(true); });
    reopenTreeButton_->setParent(header);
    reopenTreeButton_->move(kReopenButtonX, (kHeaderHeight - reopenTreeButton_->height()) / 2);
    reopenTreeButton_->hide();

    countLabel_ = new QLabel;
    countLabel_->setObjectName(QStringLiteral("itemsCount"));
    pathLabel_ = new QLabel;
    pathLabel_->setObjectName(QStringLiteral("itemsPath"));
    auto* labels = new QVBoxLayout;
    labels->setContentsMargins(0, 0, 0, 0);
    labels->setSpacing(4);
    labels->addWidget(countLabel_);
    labels->addWidget(pathLabel_);
    listHeaderLayout_->addLayout(labels);
    // Keep the two lines at their natural, tight spacing instead of
    // letting the column stretch over the full header height.
    listHeaderLayout_->setAlignment(labels, Qt::AlignVCenter);
    listHeaderLayout_->addStretch(1);

    filterButton_ = headerButton(QStringLiteral("filter"), tr("Sort entries"));
    connect(filterButton_, &QToolButton::clicked, this, &MainWindow::showSortMenu);
    listHeaderLayout_->addWidget(filterButton_);

    auto* newEntry = headerButton(QStringLiteral("key"), tr("New entry"));
    connect(newEntry, &QToolButton::clicked, this, [this] { addEntryTo(currentGroup()); });
    listHeaderLayout_->addWidget(newEntry);
    return header;
}

nightlock::Group* MainWindow::currentGroup() const {
    if (auto* group = treeModel_->group(tree_->currentIndex()))
        return group;
    return treeModel_->rootGroup();
}

NlMenu* MainWindow::showSortMenu() {
    auto* menu = new NlMenu(this);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);

    const auto current = entryModel_->sortMode();
    const auto addMode = [&](const QString& title, EntryListModel::SortMode mode) {
        QAction* action = menu->addAction(title, this, [this, mode] { applySortMode(mode); });
        if (mode == current)
            action->setIcon(menuIcon(QStringLiteral("check")));
    };
    addMode(tr("Custom order"), EntryListModel::SortMode::Custom);
    addMode(tr("By date created"), EntryListModel::SortMode::Created);
    addMode(tr("By date modified"), EntryListModel::SortMode::Modified);
    addMode(tr("By site (A–Z)"), EntryListModel::SortMode::Site);

    menu->popupAt(filterButton_->mapToGlobal(QPoint(0, filterButton_->height() + 4)));
    return menu;
}

void MainWindow::applySortMode(EntryListModel::SortMode mode) {
    auto* entry = entryModel_->entry(list_->currentIndex());
    entryModel_->setSortMode(mode);
    if (entry)
        list_->setCurrentIndex(entryModel_->indexOf(entry));

    // The funnel stays highlighted while a non-default order is on.
    const bool active = mode != EntryListModel::SortMode::Custom;
    if (filterButton_->property("active").toBool() != active) {
        filterButton_->setProperty("active", active);
        filterButton_->style()->unpolish(filterButton_);
        filterButton_->style()->polish(filterButton_);
    }
}

// Curtain animation: the pane's splitter slot narrows while the inner
// content, glued to the slot's right edge, slides out over the window
// edge (and back in when reopening).
void MainWindow::setTreePaneVisible(bool visible) {
    if (paneAnimation_ || treePane_->isVisible() == visible)
        return;
    auto* pane = static_cast<SlidingPane*>(treePane_);

    int start = 0;
    int target = 0;
    if (visible) {
        target = treeSplitterSizes_.isEmpty() ? 375 : treeSplitterSizes_.first();
        pane->beginSlide(target);
        pane->show();
        // The splitter would hand the reappearing pane its remembered
        // width in one jump; take it back before anything paints.
        QList<int> sizes = splitter_->sizes();
        sizes[1] += sizes[0];
        sizes[0] = 0;
        splitter_->setSizes(sizes);
        fadeReopenButton(false);
    } else {
        treeSplitterSizes_ = splitter_->sizes();
        start = treeSplitterSizes_.first();
        pane->beginSlide(start);
    }

    paneAnimation_ = new QVariantAnimation(this);
    paneAnimation_->setDuration(260);
    paneAnimation_->setEasingCurve(QEasingCurve::InOutCubic);
    paneAnimation_->setStartValue(start);
    paneAnimation_->setEndValue(target);
    connect(paneAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        QList<int> sizes = splitter_->sizes();
        const int width = value.toInt();
        sizes[1] += sizes[0] - width;  // the middle pane absorbs the change
        sizes[0] = width;
        splitter_->setSizes(sizes);
        // Labels follow the pane's edge until they reach the spot they
        // hold while it is hidden (right of the traffic lights), then
        // stay pinned there — no jump when the slide ends.
        listHeaderLayout_->setContentsMargins(qMax(20, kHiddenLabelShift - width), 0, 14, 0);
    });
    connect(paneAnimation_, &QVariantAnimation::finished, this, [this, pane, visible] {
        paneAnimation_->deleteLater();
        paneAnimation_ = nullptr;
        pane->endSlide();
        if (visible) {
            if (!treeSplitterSizes_.isEmpty())
                splitter_->setSizes(treeSplitterSizes_);
            listHeaderLayout_->setContentsMargins(20, 0, 14, 0);
        } else {
            pane->hide();
            // The labels already rest right of the traffic lights; the
            // way back fades into the gap reserved before them.
            fadeReopenButton(true);
        }
    });
    paneAnimation_->start();
}

// Opacity fade for the overlay reopen button, so it appears in (and
// leaves) its corner softly instead of popping.
void MainWindow::fadeReopenButton(bool shown) {
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(reopenTreeButton_->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(reopenTreeButton_);
        reopenTreeButton_->setGraphicsEffect(effect);
    }
    if (shown)
        reopenTreeButton_->show();
    auto* fade = new QVariantAnimation(reopenTreeButton_);
    fade->setDuration(150);
    fade->setStartValue(shown ? 0.0 : effect->opacity());
    fade->setEndValue(shown ? 1.0 : 0.0);
    connect(fade, &QVariantAnimation::valueChanged, effect,
            [effect](const QVariant& value) { effect->setOpacity(value.toReal()); });
    connect(fade, &QVariantAnimation::finished, this, [this, fade, shown] {
        fade->deleteLater();
        if (!shown)
            reopenTreeButton_->hide();
    });
    fade->start();
}

void MainWindow::selectGroupNamed(const QString& name) {
    if (auto* group = findGroup(treeModel_->rootGroup(), name))
        tree_->setCurrentIndex(treeModel_->indexOf(group));
}

void MainWindow::selectEntryNamed(const QString& name) {
    for (int row = 0; row < entryModel_->rowCount(); ++row) {
        const QModelIndex idx = entryModel_->index(row, 0);
        if (idx.data(EntryListModel::NameRole).toString() == name) {
            list_->setCurrentIndex(idx);
            return;
        }
    }
}

// One graph window at a time: a second click just brings it forward.
// The window is rebuilt from scratch on every open, so it always
// shows a fresh snapshot of the vault.
void MainWindow::openGraph() {
    if (graphsettings::disabled() || lockScreen_->isVisible())
        return;
    if (graph_) {
        graph_->raise();
        graph_->activateWindow();
        return;
    }
    graph_ = new GraphWindow(treeModel_->rootGroup());
    graph_->setAttribute(Qt::WA_DeleteOnClose);
    connect(graph_, &QObject::destroyed, this, [this] { graph_ = nullptr; });
    connect(graph_, &GraphWindow::nodeActivated, this, &MainWindow::revealInVault);
    graph_->resize(900, 640);
    graph_->show();
}

void MainWindow::syncNetGraphAvailability() {
    const bool isDisabled = graphsettings::disabled();
    if (graphButton_)
        graphButton_->setVisible(!graphsettings::hideIcon());
    if (graphShortcut_)
        graphShortcut_->setEnabled(!isDisabled);
    if (isDisabled && graph_)
        graph_->close();
}

// Jump to a spot in the vault — from a graph-node click or a search
// pick. Both hand over a snapshot; pointers into a since-edited vault
// are dropped instead of followed.
void MainWindow::revealInVault(nightlock::Group* group, nightlock::Entry* entry) {
    auto* root = treeModel_->rootGroup();
    if (!group || (group != root && !root->isAncestorOf(group)))
        return;
    tree_->setCurrentIndex(treeModel_->indexOf(group));
    if (entry) {
        const QModelIndex index = entryModel_->indexOf(entry);
        if (!index.isValid())
            return;
        list_->setCurrentIndex(index);
    }
    raise();
    activateWindow();
}

void MainWindow::refreshGraph() {
    if (graph_)
        graph_->refresh();
}

// The standalone "Search Entry" window, centered over the main
// window. One at a time: a second call just brings it forward.
SearchWindow* MainWindow::openSearch() {
    if (lockScreen_->isVisible())
        return nullptr;
    if (search_) {
        search_->raise();
        search_->activateWindow();
        return search_;
    }
    search_ = new SearchWindow(treeModel_->rootGroup());
    connect(search_, &QObject::destroyed, this, [this] { search_ = nullptr; });
    connect(search_, &SearchWindow::entryChosen, this, &MainWindow::revealInVault);
    search_->move(geometry().center() - QPoint(search_->width() / 2, search_->height() / 2));
    search_->show();
    return search_;
}

QWidget* MainWindow::openGraphForScreenshot() {
    openGraph();
    // NIGHTLOCK_GRAPH_FOCUS=<entry name> replays the Show-in-Graph
    // jump for screenshots.
    const QString focus = qEnvironmentVariable("NIGHTLOCK_GRAPH_FOCUS");
    if (!focus.isEmpty()) {
        selectEntryNamed(focus);
        if (auto* entry = entryModel_->entry(list_->currentIndex()))
            graph_->focusEntry(entry);
    }
    return graph_;
}

// The standalone Settings window, centered over the main window. Not
// gated on the lock screen — the Database page must be reachable from
// the first-run state — but closeVaultSession() shuts it together
// with every other dependent window. One at a time: a second call
// just brings it forward.
SettingsWindow* MainWindow::openSettings() {
    if (settings_) {
        settings_->raise();
        settings_->activateWindow();
        return settings_;
    }
    settings_ = new SettingsWindow;
    connect(settings_, &QObject::destroyed, this, [this] { settings_ = nullptr; });
    connect(settings_, &SettingsWindow::switchDatabaseRequested, this,
            &MainWindow::switchToVault);
    connect(settings_, &SettingsWindow::createDatabaseRequested, this,
            &MainWindow::createVaultAt);
    connect(settings_, &SettingsWindow::signOutRequested, this,
            &MainWindow::signOutVault);
    settings_->move(geometry().center() -
                    QPoint(settings_->width() / 2, settings_->height() / 2));
    settings_->show();
    return settings_;
}

QWidget* MainWindow::openSettingsForScreenshot(int category) {
    SettingsWindow* window = openSettings();
    window->selectCategory(category);
    return window;
}

QWidget* MainWindow::openSearchForScreenshot(const QString& query) {
    SearchWindow* window = openSearch();
    if (window)
        window->setQuery(query);
    return window;
}

// The lock icon / ⌘L: windows showing vault data close, the floating
// detail docks back, the models drop their pointers into the tree,
// the decrypted tree and key are wiped, and the lock screen covers
// everything until the right password comes in.
void MainWindow::lockVault() {
    if (lockScreen_->isVisible())
        return;
    closeVaultSession();
    showLockScreen(false);
}

void MainWindow::closeVaultSession() {
    if (graph_)
        graph_->close();
    if (search_)
        search_->close();
    if (settings_)
        settings_->close();
    // Ad-hoc dialogs — entry editors, confirmation boxes — hold
    // Entry*/Group* into the tree that is about to be wiped.
    const auto dialogs = findChildren<QDialog*>();
    for (QDialog* dialog : dialogs)
        dialog->close();
    if (detail_->isWindow())
        dockDetail();
    // Views first, tree second: after lockAndWipe() every cached
    // Group*/Entry* dangles.
    setVaultRoot(nullptr);
    VaultService::instance()->lockAndWipe();
}

void MainWindow::showLockScreen(bool create) {
    lockScreen_->setMode(create ? LockScreen::Mode::Create
                                : LockScreen::Mode::Unlock);
    lockScreen_->setVaultTarget(VaultService::instance()->vaultPath());
    lockScreen_->setGeometry(rect());
    lockScreen_->reset();
    lockScreen_->show();
    lockScreen_->raise();
    // Keynote-style: the Dock icon wears a padlock while locked.
    QGuiApplication::setWindowIcon(
        QIcon(respaths::icon(QStringLiteral("appicon-locked.png"))));
}

void MainWindow::updateVaultTitle() {
    setWindowTitle(QFileInfo(VaultService::instance()->vaultPath()).fileName());
}

void MainWindow::startLocked() {
    auto* service = VaultService::instance();
    const QString path = service->startupPath();
    // Empty = first-run: nothing remembered (or explicitly cleared) —
    // offer creation at the default location.
    service->setVaultPath(path.isEmpty() ? VaultService::defaultVaultPath()
                                         : path);
    updateVaultTitle();
    showLockScreen(path.isEmpty() || !service->vaultExists());
}

void MainWindow::switchToVault(const QString& path) {
    auto* service = VaultService::instance();
    if (service->demoMode())
        return;  // demo sessions never retarget
    closeVaultSession();
    service->setVaultPath(path);
    // Switching is the commitment: the pick opens on the next launch
    // even if it never gets unlocked in this session.
    service->setRememberedVaultPath(service->vaultPath());
    updateVaultTitle();
    showLockScreen(!service->vaultExists());
    raise();
    activateWindow();
}

void MainWindow::createVaultAt(const QString& path) {
    auto* service = VaultService::instance();
    if (service->demoMode())
        return;
    closeVaultSession();
    service->setVaultPath(path);
    updateVaultTitle();
    showLockScreen(true);
    raise();
    activateWindow();
}

void MainWindow::signOutVault() {
    auto* service = VaultService::instance();
    if (service->demoMode())
        return;
    closeVaultSession();
    service->clearRememberedVaultPath();
    service->setVaultPath(VaultService::defaultVaultPath());
    updateVaultTitle();
    showLockScreen(true);
    raise();
    activateWindow();
}

void MainWindow::setVaultRoot(nightlock::Group* root) {
    detail_->setEntry(nullptr);
    entryModel_->setGroup(nullptr);
    treeModel_->setRootGroup(root);
    countLabel_->clear();
    pathLabel_->clear();
    if (root) {
        tree_->expandAll();
        tree_->setCurrentIndex(treeModel_->indexOf(root));
    }
}

void MainWindow::handlePassword(const QString& password) {
    auto* service = VaultService::instance();
    // The demo vault has no file behind it; the mockup password from
    // the Figma days keeps the screenshot flows working.
    if (service->demoMode()) {
        if (password == QLatin1String("nightlock"))
            finishUnlock(service->root());
        else
            lockScreen_->rejectPassword();
        return;
    }
    if (lockScreen_->mode() == LockScreen::Mode::Create) {
        // The location row picks the target; never overwrite a vault
        // that is already there — the link below opens those.
        const QString target = lockScreen_->vaultTarget();
        if (QFileInfo::exists(target)) {
            lockScreen_->rejectPassword(
                tr("A vault already exists here. Select another folder."));
            return;
        }
        service->setVaultPath(target);
        updateVaultTitle();
        const nightlock::VaultError error = service->createNew(password);
        if (error == nightlock::VaultError::None)
            finishUnlock(service->root());
        else
            lockScreen_->rejectPassword(
                QString::fromUtf8(nightlock::errorMessage(error)));
        return;
    }
    const nightlock::VaultError error = service->unlock(password);
    if (error == nightlock::VaultError::None) {
        finishUnlock(service->root());
    } else if (error == nightlock::VaultError::WrongPassword) {
        lockScreen_->rejectPassword();
    } else {
        lockScreen_->rejectPassword(
            QString::fromUtf8(nightlock::errorMessage(error)));
    }
}

void MainWindow::finishUnlock(nightlock::Group* root) {
    setVaultRoot(root);
    lockScreen_->hide();
    // The Dock loses its lock badge together with the screen.
    QGuiApplication::setWindowIcon(
        QIcon(respaths::icon(QStringLiteral("appicon.png"))));
}

void MainWindow::debugLock(bool fail) {
    lockVault();
    if (fail)
        lockScreen_->debugFail();
}

void MainWindow::debugSubmitPassword(const QString& password) {
    handlePassword(password);
}

void MainWindow::debugSetVaultTarget(const QString& path) {
    lockScreen_->setVaultTarget(path);
}

void MainWindow::onGroupChanged(const QModelIndex& current) {
    auto* group = treeModel_->group(current);
    entryModel_->setGroup(group);
    detail_->setEntry(nullptr);
    if (!group) {
        countLabel_->clear();
        pathLabel_->clear();
        return;
    }
    const auto count = group->entries().size();
    countLabel_->setText(
        QStringLiteral("%1 %2").arg(count).arg(count == 1 ? tr("entry") : tr("entries")));
    pathLabel_->setText(QString::fromStdString(group->path()));
}

void MainWindow::onEntryChanged(const QModelIndex& current) {
    detail_->setEntry(entryModel_->entry(current));
}

void MainWindow::showGroupMenu(const QPoint& pos) {
    const QModelIndex idx = tree_->indexAt(pos);
    auto* group = treeModel_->group(idx);
    if (!group)
        group = treeModel_->rootGroup();  // empty area acts on the vault root

    // A multi-selection gets a reduced menu acting on every selected
    // folder at once (the root never joins a batch).
    QList<nightlock::Group*> selected;
    for (const QModelIndex& i : tree_->selectionModel()->selectedIndexes())
        if (auto* g = treeModel_->group(i); g && g != treeModel_->rootGroup())
            selected << g;
    if (selected.size() > 1 && selected.contains(group)) {
        auto* menu = new NlMenu(this);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->addAction(menuIcon(QStringLiteral("image")), tr("Change icons…"), this,
                        [this, selected] { changeFolderIcon(selected); });
        menu->addSeparator();
        auto* del = menu->addAction(menuIcon(QStringLiteral("trash")), tr("Delete"), this,
                                    [this, selected] { deleteFolders(selected); });
        del->setProperty("danger", true);
        menu->popupAt(tree_->viewport()->mapToGlobal(pos));
        return;
    }

    auto* menu = new NlMenu(this);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);

    menu->addAction(menuIcon(QStringLiteral("key")), tr("New entry"), this,
                    [this, group] { addEntryTo(group); });
    menu->addAction(menuIcon(QStringLiteral("folder-plus")), tr("New folder"), this,
                    [this, group] { addFolderTo(group); });
    if (group != treeModel_->rootGroup()) {
        menu->addSeparator();
        menu->addAction(menuIcon(QStringLiteral("edit-3")), tr("Rename"), this,
                        [this, group] { renameFolder(group); });
        menu->addAction(menuIcon(QStringLiteral("image")), tr("Change icon…"), this,
                        [this, group] { changeFolderIcon({group}); });
        menu->addSeparator();
        auto* del = menu->addAction(menuIcon(QStringLiteral("trash")), tr("Delete"), this,
                                    [this, group] { deleteFolder(group); });
        del->setProperty("danger", true);
    }
    menu->popupAt(tree_->viewport()->mapToGlobal(pos));
}

void MainWindow::showEntryMenu(const QPoint& pos) {
    const QModelIndex idx = list_->indexAt(pos);
    auto* entry = entryModel_->entry(idx);
    if (!entry) {
        // Empty area: offer creating an entry in the current folder.
        auto* group = treeModel_->group(tree_->currentIndex());
        if (!group)
            return;
        auto* menu = new NlMenu(this);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->addAction(menuIcon(QStringLiteral("key")), tr("New entry"), this,
                        [this, group] { addEntryTo(group); });
        menu->popupAt(list_->viewport()->mapToGlobal(pos));
        return;
    }
    // A multi-selection gets a reduced menu: move or delete them all.
    QList<nightlock::Entry*> selected;
    for (const QModelIndex& i : list_->selectionModel()->selectedIndexes())
        if (auto* e = entryModel_->entry(i))
            selected << e;
    if (selected.size() > 1 && selected.contains(entry)) {
        auto* menu = new NlMenu(this);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        auto* moveMenu = buildMoveMenu(treeModel_->rootGroup(), selected, menu);
        moveMenu->setTitle(tr("Move to"));
        moveMenu->setIcon(menuIcon(QStringLiteral("corner-up-right")));
        prependMoveTarget(moveMenu, treeModel_->rootGroup(), selected,
                          QString::fromStdString(treeModel_->rootGroup()->name()));
        menu->addMenu(moveMenu);
        menu->addSeparator();
        auto* del = menu->addAction(menuIcon(QStringLiteral("trash")), tr("Delete"), this,
                                    [this, selected] { deleteEntries(selected); });
        del->setProperty("danger", true);
        menu->popupAt(list_->viewport()->mapToGlobal(pos));
        return;
    }

    if (idx != list_->currentIndex())
        list_->setCurrentIndex(idx);  // keeps the detail panel in sync

    auto* menu = buildEntryMenu(entry);
    menu->popupAt(list_->viewport()->mapToGlobal(pos));
}

NlMenu* MainWindow::buildEntryMenu(nightlock::Entry* entry) {
    auto* menu = new NlMenu(this);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);

    menu->addAction(menuIcon(QStringLiteral("user")), tr("Copy login"), this, [entry] {
        QGuiApplication::clipboard()->setText(QString::fromStdString(entry->login));
    });
    menu->addAction(menuIcon(QStringLiteral("key")), tr("Copy password"), this, [entry] {
        QGuiApplication::clipboard()->setText(toQString(entry->password));
    });
    if (!entry->code.empty()) {
        menu->addAction(menuIcon(QStringLiteral("hash")), tr("Copy 2FA code"), this, [entry] {
            QGuiApplication::clipboard()->setText(toQString(entry->code));
        });
    }
    if (!entry->url.empty()) {
        menu->addAction(menuIcon(QStringLiteral("external-link")), tr("Open URL"), this, [entry] {
            QDesktopServices::openUrl(QUrl(QString::fromStdString(entry->url)));
        });
    }

    menu->addSeparator();
    menu->addAction(menuIcon(QStringLiteral("edit")), tr("Edit"), this,
                    [this, entry] { editEntry(entry); });
    auto* moveMenu = buildMoveMenu(treeModel_->rootGroup(), {entry}, menu);
    moveMenu->setTitle(tr("Move to"));
    moveMenu->setIcon(menuIcon(QStringLiteral("corner-up-right")));
    // The vault root is a destination too, listed under its own name.
    prependMoveTarget(moveMenu, treeModel_->rootGroup(), {entry},
                      QString::fromStdString(treeModel_->rootGroup()->name()));
    menu->addMenu(moveMenu);

    menu->addSeparator();
    auto* del = menu->addAction(menuIcon(QStringLiteral("trash")), tr("Delete"), this,
                                [this, entry] { deleteEntry(entry); });
    del->setProperty("danger", true);
    return menu;
}

// Every folder of the vault is a destination: leaves are plain items,
// folders with children become sub-menus whose first item ("Move
// here") targets the folder itself. The entry's current folder stays
// visible but disabled.
NlMenu* MainWindow::buildMoveMenu(nightlock::Group* group,
                                  const QList<nightlock::Entry*>& entries,
                                  QWidget* parent) {
    auto* menu = new NlMenu(parent);
    for (const auto& sub : group->groups()) {
        nightlock::Group* target = sub.get();
        const QString name = QString::fromStdString(target->name());
        if (target->groups().empty()) {
            QAction* action = menu->addAction(
                name, this, [this, entries, target] { moveEntriesTo(entries, target); });
            action->setEnabled(target != entryModel_->group());
        } else {
            auto* subMenu = buildMoveMenu(target, entries, menu);
            subMenu->setTitle(name);
            prependMoveTarget(subMenu, target, entries, tr("Move here"));
            menu->addMenu(subMenu);
        }
    }
    return menu;
}

// Inserts a "move into this very folder" item (plus a band below it)
// at the top of `menu`, disabled when the folder already holds the
// entries.
void MainWindow::prependMoveTarget(NlMenu* menu, nightlock::Group* target,
                                   const QList<nightlock::Entry*>& entries,
                                   const QString& title) {
    QAction* first = menu->actions().value(0);
    auto* action = new QAction(title, menu);
    action->setEnabled(target != entryModel_->group());
    connect(action, &QAction::triggered, this,
            [this, entries, target] { moveEntriesTo(entries, target); });
    menu->insertAction(first, action);
    if (first)
        menu->insertSeparator(first);
}

void MainWindow::moveEntriesTo(const QList<nightlock::Entry*>& entries,
                               nightlock::Group* target) {
    nightlock::Group* source = entryModel_->group();
    if (!source || !target || source == target)
        return;
    for (nightlock::Entry* entry : entries)
        source->transferEntry(entry, *target);
    VaultService::instance()->markDirty();
    // Show where they landed, exactly like adding does: switch to the
    // target folder and keep the first moved entry selected (pointers
    // survive the transfer, so the detail view follows seamlessly).
    tree_->setCurrentIndex(treeModel_->indexOf(target));
    onGroupChanged(tree_->currentIndex());
    if (!entries.isEmpty())
        list_->setCurrentIndex(entryModel_->indexOf(entries.first()));
    refreshGraph();
}

void MainWindow::addEntryTo(nightlock::Group* group) {
    EntryEditDialog dialog(EntryEditDialog::Mode::Add, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    nightlock::Entry entry;
    dialog.applyTo(entry);
    entry.created = entry.modified = std::chrono::system_clock::now();
    standardicons::addRecentIconPath(QString::fromStdString(entry.icon));
    insertEntry(group, std::move(entry));
}

void MainWindow::insertEntry(nightlock::Group* group, nightlock::Entry entry) {
    auto& added = group->addEntry(std::move(entry));
    VaultService::instance()->markDirty();
    tree_->setCurrentIndex(treeModel_->indexOf(group));
    onGroupChanged(tree_->currentIndex());  // re-reads the list and the header
    list_->setCurrentIndex(entryModel_->indexOf(&added));
    refreshGraph();
}

void MainWindow::editEntry(nightlock::Entry* entry) {
    EntryEditDialog dialog(EntryEditDialog::Mode::Edit, this);
    dialog.setEntry(*entry);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const nightlock::Entry before = *entry;
    dialog.applyTo(*entry);
    // Modified only moves when some field actually changed — saving
    // an untouched dialog keeps the old date.
    const bool changed = entry->name != before.name || entry->login != before.login ||
                         entry->password != before.password || entry->url != before.url ||
                         entry->note != before.note || entry->icon != before.icon ||
                         entry->pattern != before.pattern;
    if (changed) {
        entry->modified = std::chrono::system_clock::now();
        VaultService::instance()->markDirty();
    }
    standardicons::addRecentIconPath(QString::fromStdString(entry->icon));
    entryModel_->notifyEntryChanged(entry);
    // A sorted view may have moved the row under the edit; follow it.
    list_->setCurrentIndex(entryModel_->indexOf(entry));
    detail_->setEntry(entry);
    refreshGraph();
}

void MainWindow::addFolderTo(nightlock::Group* group) {
    const QModelIndex parentIdx = treeModel_->indexOf(group);
    const QModelIndex idx = treeModel_->addGroup(parentIdx, tr("New Folder"));
    if (!idx.isValid())
        return;
    tree_->expand(parentIdx);
    // Explicit ClearAndSelect: plain setCurrentIndex() reads live keyboard
    // modifiers, so the still-held ⌘ of ⌘T would add to the selection.
    tree_->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
    tree_->edit(idx);  // Finder-style: name the folder right away
}

void MainWindow::renameFolder(nightlock::Group* group) {
    const QModelIndex idx = treeModel_->indexOf(group);
    tree_->setCurrentIndex(idx);
    tree_->edit(idx);
}

void MainWindow::deleteFolder(nightlock::Group* group) {
    QMessageBox box(QMessageBox::Warning, tr("Delete Folder"),
                    tr("Delete “%1” and everything inside it?")
                        .arg(QString::fromStdString(group->name())),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("This cannot be undone."));
    QAbstractButton* deleteButton = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != deleteButton)
        return;

    nightlock::Group* parent = group->parent();
    if (treeModel_->removeGroup(treeModel_->indexOf(group)))
        tree_->setCurrentIndex(treeModel_->indexOf(parent));
}

// One gallery pick assigns the icon to every group in the batch.
void MainWindow::changeFolderIcon(const QList<nightlock::Group*>& groups) {
    auto* gallery = new IconGalleryPopup(this);
    connect(gallery, &IconGalleryPopup::iconSelected, this, [this, groups](const QString& path) {
        for (nightlock::Group* group : groups)
            treeModel_->setGroupIcon(treeModel_->indexOf(group), path);
        standardicons::addRecentIconPath(path);
    });
    gallery->popupAt(QCursor::pos());
}

// Batch folder delete: one confirmation, then the top-most selected
// folders go (a selected descendant of another selected folder dies
// with its ancestor anyway).
void MainWindow::deleteFolders(const QList<nightlock::Group*>& groups) {
    QList<nightlock::Group*> tops;
    for (nightlock::Group* group : groups) {
        bool covered = false;
        for (nightlock::Group* other : groups)
            covered = covered || (other != group && other->isAncestorOf(group));
        if (!covered)
            tops << group;
    }
    QMessageBox box(QMessageBox::Warning, tr("Delete Folders"),
                    tr("Delete %1 folders and everything inside them?").arg(groups.size()),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("This cannot be undone."));
    QAbstractButton* deleteButton = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != deleteButton)
        return;

    for (nightlock::Group* group : tops)
        treeModel_->removeGroup(treeModel_->indexOf(group));
    tree_->setCurrentIndex(treeModel_->indexOf(treeModel_->rootGroup()));
}

void MainWindow::debugMoveGroup(const QString& groupName, const QString& targetName) {
    auto* moving = findGroup(treeModel_->rootGroup(), groupName);
    auto* target = findGroup(treeModel_->rootGroup(), targetName);
    if (!moving || !target)
        return;
    QMimeData* mime = treeModel_->mimeData({treeModel_->indexOf(moving)});
    treeModel_->dropMimeData(mime, Qt::MoveAction, -1, 0, treeModel_->indexOf(target));
    delete mime;
    tree_->expandAll();
}

void MainWindow::debugMoveEntry(const QString& targetName) {
    auto* entry = entryModel_->entry(list_->currentIndex());
    auto* target = findGroup(treeModel_->rootGroup(), targetName);
    if (entry && target)
        moveEntriesTo({entry}, target);
}

QMenu* MainWindow::popupSortMenuForScreenshot() {
    return showSortMenu();
}

void MainWindow::debugSetSort(const QString& mode) {
    if (mode == QLatin1String("created"))
        applySortMode(EntryListModel::SortMode::Created);
    else if (mode == QLatin1String("modified"))
        applySortMode(EntryListModel::SortMode::Modified);
    else if (mode == QLatin1String("site"))
        applySortMode(EntryListModel::SortMode::Site);
    else
        applySortMode(EntryListModel::SortMode::Custom);
}

void MainWindow::debugSetTreePane(const QString& state) {
    setTreePaneVisible(state != QLatin1String("hide"));
}

QMenu* MainWindow::popupEntryMenuForScreenshot() {
    auto* entry = entryModel_->entry(list_->currentIndex());
    if (!entry)
        return nullptr;
    auto* menu = buildEntryMenu(entry);
    menu->popupAt(list_->viewport()->mapToGlobal(QPoint(240, 300)));
    return menu;
}

QDialog* MainWindow::openEntryDialogForScreenshot() {
    auto* entry = entryModel_->entry(list_->currentIndex());
    auto* dialog = new EntryEditDialog(
        entry ? EntryEditDialog::Mode::Edit : EntryEditDialog::Mode::Add, this);
    if (entry)
        dialog->setEntry(*entry);
    dialog->show();
    return dialog;
}

QWidget* MainWindow::debugDetachDetail() {
    detachDetail(mapToGlobal(QPoint(width() / 2, height() / 2)));
    return detail_;
}

void MainWindow::debugReattachDetail() {
    maybeReattachDetail(frameGeometry().center());
}

QWidget* MainWindow::openIconGalleryForScreenshot() {
    auto* gallery = new IconGalleryPopup(this);
    gallery->popupAt(mapToGlobal(QPoint(260, 160)));
    return gallery;
}

void MainWindow::detachDetail(const QPoint& globalPos) {
    if (detail_->isWindow())
        return;
    detailSplitterSizes_ = splitter_->sizes();
    const QSize paneSize = detail_->size();
    detail_->setParent(nullptr);
    detail_->setFloatingMode(true);
    detail_->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    detail_->resize(paneSize);
    // Keep the grip (top-center of the panel) under the cursor.
    detail_->move(globalPos - QPoint(paneSize.width() / 2, 14));
    detail_->show();
    detail_->beginFloatingDrag(globalPos);
}

void MainWindow::maybeReattachDetail(const QPoint& globalPos) {
    if (frameGeometry().contains(globalPos))
        dockDetail();
}

void MainWindow::dockDetail() {
    if (!detail_->isWindow())
        return;
    detail_->setFloatingMode(false);
    splitter_->addWidget(detail_);  // rightmost pane — its original slot
    splitter_->setSizes(detailSplitterSizes_);
    detail_->show();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (lockScreen_ && lockScreen_->isVisible())
        lockScreen_->setGeometry(rect());
#ifdef Q_OS_MACOS
    // AppKit puts the buttons back into the title-bar corner on its own
    // relayouts; re-center them on the tree header after every resize.
    macwindow::layoutTrafficLights(this, kTrafficLeft, kHeaderHeight / 2);
#endif
}

// Delays the toggle so NIGHTLOCK_SCREENSHOT (fixed at 800ms) can catch
// the curtain mid-flight.
void MainWindow::debugSetTreePaneDelayed(const QString& state, int delayMs) {
    QTimer::singleShot(delayMs, this, [this, state] { debugSetTreePane(state); });
}

void MainWindow::deleteEntry(nightlock::Entry* entry) {
    QMessageBox box(QMessageBox::Warning, tr("Delete Entry"),
                    tr("Delete “%1”?").arg(QString::fromStdString(entry->name)),
                    QMessageBox::NoButton, this);
    box.setInformativeText(tr("This cannot be undone."));
    QAbstractButton* deleteButton = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != deleteButton)
        return;

    if (detail_->isWindow())
        detail_->setEntry(nullptr);
    entryModel_->removeEntry(entry);
    onGroupChanged(tree_->currentIndex());  // refreshes the counter
    refreshGraph();
}

void MainWindow::deleteEntries(const QList<nightlock::Entry*>& entries) {
    QMessageBox box(QMessageBox::Warning, tr("Delete Entries"),
                    tr("Delete %1 entries?").arg(entries.size()), QMessageBox::NoButton, this);
    box.setInformativeText(tr("This cannot be undone."));
    QAbstractButton* deleteButton = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != deleteButton)
        return;

    if (detail_->isWindow())
        detail_->setEntry(nullptr);
    for (nightlock::Entry* entry : entries)
        entryModel_->removeEntry(entry);
    onGroupChanged(tree_->currentIndex());  // refreshes the counter
    refreshGraph();
}

void MainWindow::debugSpoiler(const QString& state) {
    detail_->debugSpoiler(state);
}

void MainWindow::debugAddEntry(const QString& name) {
    auto* group = treeModel_->group(tree_->currentIndex());
    if (!group)
        return;
    nightlock::Entry entry;
    entry.name = name.toStdString();
    // NIGHTLOCK_TEST_ADD_ENTRY_FIELDS picks the filled fields —
    // "login,password" when unset, "none" leaves a name-only entry.
    const QString fields = qEnvironmentVariable("NIGHTLOCK_TEST_ADD_ENTRY_FIELDS",
                                                QStringLiteral("login,password"));
    if (fields.contains(QLatin1String("login")))
        entry.login = "debug@nightlock.app";
    if (fields.contains(QLatin1String("password")))
        entry.password = "debug";
    if (fields.contains(QLatin1String("url")))
        entry.url = "https://example.com";
    entry.created = entry.modified = std::chrono::system_clock::now();
    insertEntry(group, std::move(entry));
}

void MainWindow::debugSetEntryIcon(const QString& path) {
    auto* entry = entryModel_->entry(list_->currentIndex());
    if (!entry)
        return;
    entry->icon = path.toStdString();
    VaultService::instance()->markDirty();
    entryModel_->notifyEntryChanged(entry);
    detail_->setEntry(entry);
}

void MainWindow::debugSetEntryPattern(const QString& spec) {
    const auto kindOf = [](const QString& kind) {
        if (kind == QLatin1String("glow-soft"))
            return nightlock::Pattern::GlowSoft;
        if (kind == QLatin1String("glow-bold"))
            return nightlock::Pattern::GlowBold;
        if (kind == QLatin1String("icon-tile"))
            return nightlock::Pattern::IconTile;
        if (kind == QLatin1String("icon-tile-v2"))
            return nightlock::Pattern::IconTileV2;
        if (kind == QLatin1String("icon-tile-v3"))
            return nightlock::Pattern::IconTileV3;
        if (kind == QLatin1String("ripple"))
            return nightlock::Pattern::Ripple;
        if (kind == QLatin1String("constellation"))
            return nightlock::Pattern::Constellation;
        if (kind == QLatin1String("aurora"))
            return nightlock::Pattern::Aurora;
        if (kind == QLatin1String("halo"))
            return nightlock::Pattern::Halo;
        return nightlock::Pattern::None;
    };
    for (const QString& part : spec.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int colon = part.lastIndexOf(QLatin1Char(':'));
        const QString name = colon < 0 ? QString() : part.left(colon);
        const QString kind = colon < 0 ? part : part.mid(colon + 1);
        nightlock::Entry* entry = nullptr;
        if (name.isEmpty()) {
            entry = entryModel_->entry(list_->currentIndex());
        } else {
            for (int row = 0; row < entryModel_->rowCount() && !entry; ++row) {
                const QModelIndex idx = entryModel_->index(row, 0);
                if (idx.data(EntryListModel::NameRole).toString() == name)
                    entry = entryModel_->entry(idx);
            }
        }
        if (entry) {
            entry->pattern = kindOf(kind);
            VaultService::instance()->markDirty();
        }
    }
    detail_->setEntry(entryModel_->entry(list_->currentIndex()));
}

void MainWindow::debugRenameFolder(const QString& name) {
    if (auto* group = findGroup(treeModel_->rootGroup(), name))
        renameFolder(group);
}

void MainWindow::debugFolderOps() {
    treeModel_->addGroup(treeModel_->indexOf(treeModel_->rootGroup()),
                         QStringLiteral("Debug Created"));
    if (auto* g = findGroup(treeModel_->rootGroup(), QStringLiteral("Demo Folder")))
        treeModel_->setData(treeModel_->indexOf(g), QStringLiteral("Renamed Folder"));
    if (auto* g = findGroup(treeModel_->rootGroup(), QStringLiteral("Personal 2018")))
        treeModel_->removeGroup(treeModel_->indexOf(g));
    tree_->expandAll();
}
