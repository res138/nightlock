#include "mainwindow.hpp"

#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QDesktopServices>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
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

#include "models/grouptreemodel.hpp"
#include "standardicons.hpp"
#include "widgets/entrydetailview.hpp"
#include "widgets/entrylistdelegate.hpp"
#include "widgets/grouptreeview.hpp"
#include "widgets/icongallerypopup.hpp"
#include "widgets/nlmenu.hpp"
#include "widgets/scrollbarfader.hpp"
#include "windows/entryeditdialog.hpp"

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
    return QIcon(QStringLiteral(":/icons/menu/%1.svg").arg(name));
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
    splitter_->setSizes({300, 420, 460});
    setCentralWidget(splitter_);

    connect(detail_, &EntryDetailView::detachRequested, this, &MainWindow::detachDetail);
    connect(detail_, &EntryDetailView::dropped, this, &MainWindow::maybeReattachDetail);
    connect(detail_, &EntryDetailView::dockRequested, this, &MainWindow::dockDetail);
    connect(detail_, &EntryDetailView::editRequested, this, [this] {
        if (auto* entry = entryModel_->entry(list_->currentIndex()))
            editEntry(entry);
    });

    new ScrollBarFader(tree_);
    new ScrollBarFader(list_);

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
// panel toggle sits right after them, and the folder / graph /
// settings actions are right-aligned like the list-header icons.
QWidget* MainWindow::buildTreeHeader() {
    auto* header = new HeaderBar;
    header->setObjectName(QStringLiteral("treeHeader"));
    header->setFixedHeight(kHeaderHeight);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(kTrafficLeft + kTrafficSpan + 8, 0, 14, 0);
    layout->setSpacing(4);

    auto* closePane = headerButton(QStringLiteral("sidebar"), tr("Hide folder panel"));
    connect(closePane, &QToolButton::clicked, this, [this] { setTreePaneVisible(false); });
    layout->addWidget(closePane);
    layout->addWidget(headerButton(QStringLiteral("search"), tr("Search")));
    layout->addStretch(1);

    auto* newFolder = headerButton(QStringLiteral("folder-plus"), tr("New folder"));
    connect(newFolder, &QToolButton::clicked, this, [this] { addFolderTo(currentGroup()); });
    layout->addWidget(newFolder);

    layout->addWidget(headerButton(QStringLiteral("graph"), tr("Graph")));
    layout->addWidget(headerButton(QStringLiteral("settings"), tr("Settings")));
    layout->addWidget(headerButton(QStringLiteral("lock"), tr("Lock vault")));
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
        target = treeSplitterSizes_.isEmpty() ? 300 : treeSplitterSizes_.first();
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
                        [this, group] { changeFolderIcon(group); });
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
        QGuiApplication::clipboard()->setText(QString::fromStdString(entry->password));
    });
    if (!entry->code.empty()) {
        menu->addAction(menuIcon(QStringLiteral("hash")), tr("Copy 2FA code"), this, [entry] {
            QGuiApplication::clipboard()->setText(QString::fromStdString(entry->code));
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
    auto* moveMenu = buildMoveMenu(treeModel_->rootGroup(), entry, menu);
    moveMenu->setTitle(tr("Move to"));
    moveMenu->setIcon(menuIcon(QStringLiteral("corner-up-right")));
    // The vault root is a destination too, listed under its own name.
    prependMoveTarget(moveMenu, treeModel_->rootGroup(), entry,
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
NlMenu* MainWindow::buildMoveMenu(nightlock::Group* group, nightlock::Entry* entry,
                                  QWidget* parent) {
    auto* menu = new NlMenu(parent);
    for (const auto& sub : group->groups()) {
        nightlock::Group* target = sub.get();
        const QString name = QString::fromStdString(target->name());
        if (target->groups().empty()) {
            QAction* action = menu->addAction(
                name, this, [this, entry, target] { moveEntryTo(entry, target); });
            action->setEnabled(target != entryModel_->group());
        } else {
            auto* subMenu = buildMoveMenu(target, entry, menu);
            subMenu->setTitle(name);
            prependMoveTarget(subMenu, target, entry, tr("Move here"));
            menu->addMenu(subMenu);
        }
    }
    return menu;
}

// Inserts a "move into this very folder" item (plus a band below it)
// at the top of `menu`, disabled when the folder already holds the
// entry.
void MainWindow::prependMoveTarget(NlMenu* menu, nightlock::Group* target,
                                   nightlock::Entry* entry, const QString& title) {
    QAction* first = menu->actions().value(0);
    auto* action = new QAction(title, menu);
    action->setEnabled(target != entryModel_->group());
    connect(action, &QAction::triggered, this,
            [this, entry, target] { moveEntryTo(entry, target); });
    menu->insertAction(first, action);
    if (first)
        menu->insertSeparator(first);
}

void MainWindow::moveEntryTo(nightlock::Entry* entry, nightlock::Group* target) {
    nightlock::Group* source = entryModel_->group();
    if (!source || !target || source == target)
        return;
    if (!source->transferEntry(entry, *target))
        return;
    // Show where it landed, exactly like adding does: switch to the
    // target folder and keep the moved entry selected (the pointer
    // survives the transfer, so the detail view follows seamlessly).
    tree_->setCurrentIndex(treeModel_->indexOf(target));
    onGroupChanged(tree_->currentIndex());
    list_->setCurrentIndex(entryModel_->indexOf(entry));
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
    tree_->setCurrentIndex(treeModel_->indexOf(group));
    onGroupChanged(tree_->currentIndex());  // re-reads the list and the header
    list_->setCurrentIndex(entryModel_->indexOf(&added));
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
    if (changed)
        entry->modified = std::chrono::system_clock::now();
    standardicons::addRecentIconPath(QString::fromStdString(entry->icon));
    entryModel_->notifyEntryChanged(entry);
    // A sorted view may have moved the row under the edit; follow it.
    list_->setCurrentIndex(entryModel_->indexOf(entry));
    detail_->setEntry(entry);
}

void MainWindow::addFolderTo(nightlock::Group* group) {
    const QModelIndex parentIdx = treeModel_->indexOf(group);
    const QModelIndex idx = treeModel_->addGroup(parentIdx, tr("New Folder"));
    if (!idx.isValid())
        return;
    tree_->expand(parentIdx);
    tree_->setCurrentIndex(idx);
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

void MainWindow::changeFolderIcon(nightlock::Group* group) {
    auto* gallery = new IconGalleryPopup(this);
    connect(gallery, &IconGalleryPopup::iconSelected, this, [this, group](const QString& path) {
        treeModel_->setGroupIcon(treeModel_->indexOf(group), path);
        standardicons::addRecentIconPath(path);
    });
    gallery->popupAt(QCursor::pos());
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
        moveEntryTo(entry, target);
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
    entry.login = "debug@nightlock.app";
    entry.password = "debug";
    entry.created = entry.modified = std::chrono::system_clock::now();
    insertEntry(group, std::move(entry));
}

void MainWindow::debugSetEntryIcon(const QString& path) {
    auto* entry = entryModel_->entry(list_->currentIndex());
    if (!entry)
        return;
    entry->icon = path.toStdString();
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
        if (entry)
            entry->pattern = kindOf(kind);
    }
    detail_->setEntry(entryModel_->entry(list_->currentIndex()));
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
