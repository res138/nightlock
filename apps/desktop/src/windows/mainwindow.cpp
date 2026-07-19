#include "mainwindow.hpp"

#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <chrono>

#include <nightlock/group.hpp>

#include "models/entrylistmodel.hpp"
#include "models/grouptreemodel.hpp"
#include "widgets/entrydetailview.hpp"
#include "widgets/entrylistdelegate.hpp"
#include "widgets/grouptreeview.hpp"
#include "widgets/nlmenu.hpp"
#include "windows/entryeditdialog.hpp"

namespace {

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

}  // namespace

MainWindow::MainWindow(nightlock::Group* root, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Primary.nlck"));

    treeModel_ = new GroupTreeModel(root, this);
    tree_ = new GroupTreeView;
    tree_->setModel(treeModel_);
    tree_->expandAll();

    entryModel_ = new EntryListModel(this);
    list_ = new QListView;
    list_->setObjectName(QStringLiteral("entryList"));
    list_->setModel(entryModel_);
    list_->setItemDelegate(new EntryListDelegate(list_));
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    countLabel_ = new QLabel;
    countLabel_->setObjectName(QStringLiteral("itemsCount"));
    pathLabel_ = new QLabel;
    pathLabel_->setObjectName(QStringLiteral("itemsPath"));

    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("listHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(20, 14, 20, 12);
    headerLayout->setSpacing(4);
    headerLayout->addWidget(countLabel_);
    headerLayout->addWidget(pathLabel_);

    auto* middle = new QWidget;
    auto* middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);
    middleLayout->addWidget(header);
    middleLayout->addWidget(list_, 1);

    detail_ = new EntryDetailView;

    auto* splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    splitter->addWidget(tree_);
    splitter->addWidget(middle);
    splitter->addWidget(detail_);
    splitter->setSizes({300, 420, 460});
    setCentralWidget(splitter);

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
    countLabel_->setText(QStringLiteral("%1 %2").arg(count).arg(count == 1 ? tr("item") : tr("items")));
    pathLabel_->setText(QString::fromStdString(group->path()));
}

void MainWindow::onEntryChanged(const QModelIndex& current) {
    detail_->setEntry(entryModel_->entry(current));
}

void MainWindow::showGroupMenu(const QPoint& pos) {
    const QModelIndex idx = tree_->indexAt(pos);
    auto* group = treeModel_->group(idx);
    if (!group)
        return;

    auto* menu = new NlMenu(this);
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);

    menu->addAction(menuIcon(QStringLiteral("file-plus")), tr("New entry"), this,
                    [this, group] { addEntryTo(group); });
    menu->addAction(menuIcon(QStringLiteral("folder-plus")), tr("New folder"), this,
                    [this, group] { addFolderTo(group); });
    if (group != treeModel_->rootGroup()) {
        menu->addSeparator();
        menu->addAction(menuIcon(QStringLiteral("edit-3")), tr("Rename"), this,
                        [this, group] { renameFolder(group); });
        menu->addAction(menuIcon(QStringLiteral("image")), tr("Change icon…"), this,
                        [] { qInfo() << "TODO: change folder icon"; });
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
    if (!entry)
        return;
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
    auto* moveMenu = buildMoveMenu(treeModel_->rootGroup(), menu);
    moveMenu->setTitle(tr("Move to"));
    moveMenu->setIcon(menuIcon(QStringLiteral("corner-up-right")));
    menu->addMenu(moveMenu);

    menu->addSeparator();
    auto* del = menu->addAction(menuIcon(QStringLiteral("trash")), tr("Delete"), this,
                                [] { qInfo() << "TODO: delete entry"; });
    del->setProperty("danger", true);
    return menu;
}

NlMenu* MainWindow::buildMoveMenu(nightlock::Group* group, QWidget* parent) {
    auto* menu = new NlMenu(parent);
    for (const auto& sub : group->groups()) {
        nightlock::Group* target = sub.get();
        const QString name = QString::fromStdString(target->name());
        if (target->groups().empty()) {
            menu->addAction(name, this, [target] {
                qInfo() << "TODO: move to" << QString::fromStdString(target->path());
            });
        } else {
            auto* subMenu = buildMoveMenu(target, menu);
            subMenu->setTitle(name);
            menu->addMenu(subMenu);
        }
    }
    return menu;
}

void MainWindow::addEntryTo(nightlock::Group* group) {
    EntryEditDialog dialog(EntryEditDialog::Mode::Add, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    nightlock::Entry entry;
    dialog.applyTo(entry);
    entry.created = entry.modified = std::chrono::system_clock::now();
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

    dialog.applyTo(*entry);
    entry->modified = std::chrono::system_clock::now();
    entryModel_->notifyEntryChanged(entry);
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

void MainWindow::debugFolderOps() {
    treeModel_->addGroup(treeModel_->indexOf(treeModel_->rootGroup()),
                         QStringLiteral("Debug Created"));
    if (auto* g = findGroup(treeModel_->rootGroup(), QStringLiteral("Demo Folder")))
        treeModel_->setData(treeModel_->indexOf(g), QStringLiteral("Renamed Folder"));
    if (auto* g = findGroup(treeModel_->rootGroup(), QStringLiteral("Personal 2018")))
        treeModel_->removeGroup(treeModel_->indexOf(g));
    tree_->expandAll();
}
