#include "mainwindow.hpp"

#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

#include <nightlock/group.hpp>

#include "models/entrylistmodel.hpp"
#include "models/grouptreemodel.hpp"
#include "widgets/entrydetailview.hpp"
#include "widgets/entrylistdelegate.hpp"

namespace {

nightlock::Group* findGroup(nightlock::Group* group, const QString& name) {
    if (QString::fromStdString(group->name()) == name)
        return group;
    for (const auto& sub : group->groups())
        if (auto* found = findGroup(sub.get(), name))
            return found;
    return nullptr;
}

}  // namespace

MainWindow::MainWindow(nightlock::Group* root, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Primary.nlck"));

    treeModel_ = new GroupTreeModel(root, this);
    tree_ = new QTreeView;
    tree_->setObjectName(QStringLiteral("groupTree"));
    tree_->setModel(treeModel_);
    tree_->setHeaderHidden(true);
    tree_->setIndentation(22);
    tree_->setRootIsDecorated(false);
    tree_->setIconSize(QSize(22, 22));
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
