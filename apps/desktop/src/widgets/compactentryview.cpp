#include "compactentryview.hpp"

#include <QAbstractProxyModel>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QUrl>

#include <array>

#include "appearancesettings.hpp"
#include "entrycolors.hpp"
#include "fonts.hpp"
#include "generalsettings.hpp"
#include "models/entrylistmodel.hpp"
#include "widgets/copylabel.hpp"
#include "widgets/overlayscrollbar.hpp"
#include "widgets/spoilerlabel.hpp"

namespace {

enum Column {
    NameColumn,
    LoginColumn,
    PasswordColumn,
    UrlColumn,
    NoteColumn,
    DateColumn,
    ColumnCount,
};

constexpr int kRowHeight = 42;
constexpr int kHeaderHeight = 32;
constexpr char kInteractiveRowProperty[] = "compactEntryRow";

struct ColumnMetrics {
    int minimum;
    int preferred;
    int grow;
};

constexpr std::array<ColumnMetrics, ColumnCount> kColumnMetrics = {{
    {104, 200, 2},  // Name
    {112, 190, 2},  // Login
    {116, 190, 2},  // Password
    {112, 205, 2},  // URL
    {120, 235, 3},  // Note
    {90, 100, 1},   // Date
}};

appearancesettings::CompactColumn settingColumn(int column) {
    using appearancesettings::CompactColumn;
    switch (column) {
    case NameColumn: return CompactColumn::Name;
    case LoginColumn: return CompactColumn::Login;
    case PasswordColumn: return CompactColumn::Password;
    case UrlColumn: return CompactColumn::Url;
    case NoteColumn: return CompactColumn::Note;
    case DateColumn: return CompactColumn::Date;
    }
    return CompactColumn::Name;
}

// A normal child widget would swallow selection and context-menu
// events before QTableView sees them. The wrapper marks its row (and
// so do its children); CompactEntryView's event filter restores the
// usual row-selection behavior without interfering with copy/reveal.
class InteractiveCell final : public QWidget {
public:
    enum class Kind { Login, Password };

    explicit InteractiveCell(Kind kind, QWidget* parent = nullptr)
        : QWidget(parent), kind_(kind) {
        setAttribute(Qt::WA_StyledBackground, false);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 0, 10, 0);
        layout->setSpacing(0);
        if (kind == Kind::Login) {
            login_ = new CopyLabel;
            login_->setContentAlignment(Qt::AlignLeft);
            login_->setTextElideMode(Qt::ElideRight);
            login_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            layout->addWidget(login_);
        } else {
            password_ = new SpoilerLabel;
            password_->setTextElideMode(Qt::ElideRight);
            password_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            layout->addWidget(password_);
        }
    }

    Kind kind() const { return kind_; }
    CopyLabel* login() const { return login_; }
    SpoilerLabel* password() const { return password_; }

private:
    Kind kind_;
    CopyLabel* login_ = nullptr;
    SpoilerLabel* password_ = nullptr;
};

class CompactHeaderView final : public QHeaderView {
public:
    explicit CompactHeaderView(QWidget* parent)
        : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(false);
        setSectionsMovable(false);
        setHighlightSections(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setMinimumSectionSize(0);
        setFixedHeight(kHeaderHeight);
    }

protected:
    void paintSection(QPainter* painter, const QRect& rect,
                      int logicalIndex) const override {
        if (!rect.isValid())
            return;
        painter->save();
        painter->fillRect(rect, appearancesettings::palette().window);
        painter->setPen(appearancesettings::palette().separator);
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        painter->drawLine(rect.topRight(), rect.bottomRight());

        QFont headerFont(fonts::resolvedFamily(fonts::Role::Primary));
        headerFont.setPixelSize(12);
        headerFont.setWeight(QFont::DemiBold);
        painter->setFont(headerFont);
        painter->setPen(appearancesettings::palette().muted);
        const QString title = model()->headerData(logicalIndex, orientation(),
                                                   Qt::DisplayRole).toString();
        const QRect textRect = rect.adjusted(10, 0, -10, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(headerFont).elidedText(
                              title, Qt::ElideRight, textRect.width()));
        painter->restore();
    }
};

QColor rowBackground(const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (option.state.testFlag(QStyle::State_Selected)) {
        QColor selected = appearancesettings::accentColor();
        selected.setAlpha(appearancesettings::darkActive() ? 54 : 25);
        return selected;
    }
    if (option.state.testFlag(QStyle::State_MouseOver))
        return appearancesettings::palette().inputHover;
    if (generalsettings::entryColorsEnabled()) {
        const auto color = static_cast<nightlock::EntryColor>(
            index.data(EntryListModel::ColorRole).toInt());
        if (color != nightlock::EntryColor::None)
            return entrycolors::subtleFill(color);
    }
    return appearancesettings::palette().window;
}

class CompactCellDelegate : public QStyledItemDelegate {
public:
    CompactCellDelegate(int column, QObject* parent)
        : QStyledItemDelegate(parent), column_(column) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {0, kRowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(option.rect, rowBackground(option, index));
        painter->setPen(appearancesettings::palette().separator);
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

        // Login and Password are live widgets layered on this painted
        // row; the delegate supplies only their background/separator.
        if (column_ == LoginColumn || column_ == PasswordColumn) {
            painter->restore();
            return;
        }

        QRect content = option.rect.adjusted(10, 0, -10, 0);
        QFont textFont(fonts::resolvedFamily(fonts::Role::Primary));
        textFont.setPixelSize(12);
        QColor textColor = appearancesettings::palette().value;

        if (column_ == NameColumn) {
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            constexpr int kIconSize = 18;
            icon.paint(painter, QRect(content.left(),
                                      content.center().y() - kIconSize / 2,
                                      kIconSize, kIconSize));
            content.setLeft(content.left() + kIconSize + 7);
            textFont.setFamily(fonts::resolvedFamily(fonts::Role::Secondary));
            textFont.setWeight(QFont::DemiBold);
            textColor = appearancesettings::palette().ink;
        }

        painter->setFont(textFont);
        painter->setPen(textColor);
        const QString text = index.data(Qt::DisplayRole).toString();
        const QString visible = QFontMetrics(textFont).elidedText(
            text, Qt::ElideRight, qMax(0, content.width()));
        const Qt::Alignment alignment = column_ == DateColumn
                                            ? Qt::AlignCenter
                                            : Qt::AlignLeft | Qt::AlignVCenter;
        painter->drawText(content, alignment | Qt::TextSingleLine, visible);
        painter->restore();
    }

private:
    int column_;
};

class UrlDelegate final : public CompactCellDelegate {
public:
    explicit UrlDelegate(QObject* parent)
        : CompactCellDelegate(UrlColumn, parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->fillRect(option.rect, rowBackground(option, index));
        painter->setPen(appearancesettings::palette().separator);
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());

        const QString url = index.data(Qt::DisplayRole).toString();
        if (!url.isEmpty()) {
            QFont textFont(fonts::resolvedFamily(fonts::Role::Primary));
            textFont.setPixelSize(12);
            painter->setFont(textFont);
            painter->setPen(appearancesettings::palette().value);

            constexpr int kIconSize = 13;
            constexpr int kGap = 6;
            QRect content = option.rect.adjusted(10, 0, -10, 0);
            const int textWidth = qMax(0, content.width() - kIconSize - kGap);
            const QString visible = QFontMetrics(textFont).elidedText(
                url, Qt::ElideRight, textWidth);
            painter->drawText(QRect(content.left(), content.top(), textWidth,
                                    content.height()),
                              Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                              visible);
            static const QIcon externalLink =
                appearancesettings::themedMenuIcon(QStringLiteral("external-link"));
            externalLink.paint(painter, QRect(content.right() - kIconSize,
                                               content.center().y() - kIconSize / 2,
                                               kIconSize, kIconSize));
        }
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel*,
                     const QStyleOptionViewItem&, const QModelIndex& index) override {
        if (event->type() != QEvent::MouseButtonRelease)
            return false;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton)
            return false;
        const QString value = index.data(Qt::DisplayRole).toString();
        if (value.isEmpty())
            return false;
        const QUrl url = QUrl::fromUserInput(value);
        if (!url.isValid())
            return false;
        QDesktopServices::openUrl(url);
        return true;
    }
};

}  // namespace

// A table-shaped proxy over EntryListModel. Source rows remain the
// authority for selection, ordering, sorting and drag/drop; the proxy
// only fans their roles out into six presentation columns.
class CompactEntryProxyModel final : public QAbstractProxyModel {
public:
    explicit CompactEntryProxyModel(QObject* parent = nullptr)
        : QAbstractProxyModel(parent) {}

    void setSourceModel(QAbstractItemModel* source) override {
        if (source == sourceModel())
            return;
        beginResetModel();
        if (sourceModel())
            disconnect(sourceModel(), nullptr, this, nullptr);
        QAbstractProxyModel::setSourceModel(source);
        if (source) {
            connect(source, &QAbstractItemModel::modelAboutToBeReset,
                    this, [this] { beginResetModel(); });
            connect(source, &QAbstractItemModel::modelReset,
                    this, [this] { endResetModel(); });
            connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this,
                    [this](const QModelIndex& parent, int first, int last) {
                        if (!parent.isValid())
                            beginInsertRows({}, first, last);
                    });
            connect(source, &QAbstractItemModel::rowsInserted, this,
                    [this](const QModelIndex& parent) {
                        if (!parent.isValid())
                            endInsertRows();
                    });
            connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                    [this](const QModelIndex& parent, int first, int last) {
                        if (!parent.isValid())
                            beginRemoveRows({}, first, last);
                    });
            connect(source, &QAbstractItemModel::rowsRemoved, this,
                    [this](const QModelIndex& parent) {
                        if (!parent.isValid())
                            endRemoveRows();
                    });
            connect(source, &QAbstractItemModel::rowsAboutToBeMoved, this,
                    [this](const QModelIndex& sourceParent, int sourceFirst,
                           int sourceLast, const QModelIndex& destinationParent,
                           int destinationRow) {
                        if (!sourceParent.isValid() && !destinationParent.isValid())
                            beginMoveRows({}, sourceFirst, sourceLast, {}, destinationRow);
                    });
            connect(source, &QAbstractItemModel::rowsMoved, this,
                    [this](const QModelIndex& sourceParent, int, int,
                           const QModelIndex& destinationParent, int) {
                        if (!sourceParent.isValid() && !destinationParent.isValid())
                            endMoveRows();
                    });
            connect(source, &QAbstractItemModel::layoutAboutToBeChanged, this,
                    [this] { beginResetModel(); });
            connect(source, &QAbstractItemModel::layoutChanged, this,
                    [this] { endResetModel(); });
            connect(source, &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                           const QList<int>&) {
                        if (!topLeft.isValid() || !bottomRight.isValid())
                            return;
                        emit dataChanged(index(topLeft.row(), 0),
                                         index(bottomRight.row(), ColumnCount - 1));
                    });
        }
        endResetModel();
    }

    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override {
        if (!proxyIndex.isValid() || !sourceModel())
            return {};
        return sourceModel()->index(proxyIndex.row(), 0);
    }

    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override {
        if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel())
            return {};
        return index(sourceIndex.row(), NameColumn);
    }

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = {}) const override {
        if (parent.isValid() || row < 0 || row >= rowCount() ||
            column < 0 || column >= ColumnCount)
            return {};
        return createIndex(row, column);
    }

    QModelIndex parent(const QModelIndex&) const override { return {}; }

    int rowCount(const QModelIndex& parent = {}) const override {
        return !parent.isValid() && sourceModel() ? sourceModel()->rowCount() : 0;
    }

    int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant data(const QModelIndex& proxyIndex, int role) const override {
        const QModelIndex source = mapToSource(proxyIndex);
        if (!source.isValid())
            return {};

        // Password is deliberately absent from DisplayRole/tooltips so
        // generic table and accessibility paths never expose it. The
        // dedicated SpoilerLabel requests PasswordRole explicitly.
        if (role == EntryListModel::PasswordRole)
            return source.data(role);
        if (role == EntryListModel::LoginRole ||
            role == EntryListModel::ColorRole ||
            role == EntryListModel::ExpiredRole)
            return source.data(role);
        if (role == Qt::DecorationRole)
            return proxyIndex.column() == NameColumn
                       ? source.data(Qt::DecorationRole)
                       : QVariant{};
        if (role == Qt::DisplayRole) {
            switch (proxyIndex.column()) {
            case NameColumn: return source.data(EntryListModel::NameRole);
            case LoginColumn: return source.data(EntryListModel::LoginRole);
            case PasswordColumn: return {};
            case UrlColumn: return source.data(EntryListModel::UrlRole);
            case NoteColumn: return source.data(EntryListModel::NoteRole);
            case DateColumn:
                return source.data(EntryListModel::ModifiedRole)
                    .toDateTime().toString(QStringLiteral("dd.MM.yyyy"));
            }
        }
        if (role == Qt::ToolTipRole) {
            if (proxyIndex.column() == PasswordColumn)
                return {};
            return data(proxyIndex, Qt::DisplayRole);
        }
        return source.data(role);
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case NameColumn: return tr("Name");
        case LoginColumn: return tr("Login");
        case PasswordColumn: return tr("Password");
        case UrlColumn: return tr("URL");
        case NoteColumn: return tr("Note");
        case DateColumn: return tr("Date");
        }
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex& proxyIndex) const override {
        return sourceModel() ? sourceModel()->flags(mapToSource(proxyIndex))
                             : Qt::NoItemFlags;
    }

    QStringList mimeTypes() const override {
        return sourceModel() ? sourceModel()->mimeTypes() : QStringList{};
    }

    QMimeData* mimeData(const QModelIndexList& indexes) const override {
        if (!sourceModel())
            return nullptr;
        QModelIndexList sourceIndexes;
        QSet<int> rows;
        for (const QModelIndex& proxy : indexes) {
            if (!proxy.isValid() || rows.contains(proxy.row()))
                continue;
            rows.insert(proxy.row());
            sourceIndexes.append(mapToSource(proxy));
        }
        return sourceModel()->mimeData(sourceIndexes);
    }

    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int column,
                         const QModelIndex& parent) const override {
        return sourceModel() && sourceModel()->canDropMimeData(
                                    data, action, row, column,
                                    parent.isValid() ? mapToSource(parent)
                                                     : QModelIndex{});
    }

    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column,
                      const QModelIndex& parent) override {
        return sourceModel() && sourceModel()->dropMimeData(
                                    data, action, row, column,
                                    parent.isValid() ? mapToSource(parent)
                                                     : QModelIndex{});
    }

    Qt::DropActions supportedDropActions() const override {
        return sourceModel() ? sourceModel()->supportedDropActions()
                             : Qt::IgnoreAction;
    }
};

CompactEntryView::CompactEntryView(QWidget* parent)
    : QTableView(parent), compactModel_(new CompactEntryProxyModel(this)) {
    setObjectName(QStringLiteral("compactEntryTable"));
    setHorizontalHeader(new CompactHeaderView(this));
    QTableView::setModel(compactModel_);

    verticalHeader()->hide();
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setMinimumSectionSize(kRowHeight);
    verticalHeader()->setDefaultSectionSize(kRowHeight);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    horizontalHeader()->setStretchLastSection(false);
    setCornerButtonEnabled(false);
    setShowGrid(false);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    setDragDropMode(QAbstractItemView::InternalMove);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);

    setItemDelegateForColumn(NameColumn,
                             new CompactCellDelegate(NameColumn, this));
    setItemDelegateForColumn(LoginColumn,
                             new CompactCellDelegate(LoginColumn, this));
    setItemDelegateForColumn(PasswordColumn,
                             new CompactCellDelegate(PasswordColumn, this));
    setItemDelegateForColumn(UrlColumn, new UrlDelegate(this));
    setItemDelegateForColumn(NoteColumn,
                             new CompactCellDelegate(NoteColumn, this));
    setItemDelegateForColumn(DateColumn,
                             new CompactCellDelegate(DateColumn, this));

    viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(viewport(), &QWidget::customContextMenuRequested, this,
            &CompactEntryView::contextMenuRequested);
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex& previous) {
                emit currentSourceChanged(compactModel_->mapToSource(current),
                                          compactModel_->mapToSource(previous));
            });

    const auto rebuildCells = [this] { scheduleInteractiveRefresh(); };
    connect(compactModel_, &QAbstractItemModel::modelAboutToBeReset, this,
            &CompactEntryView::clearInteractiveCells);
    connect(compactModel_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this] { clearInteractiveCells(); });
    connect(compactModel_, &QAbstractItemModel::rowsAboutToBeMoved, this,
            [this] { clearInteractiveCells(); });
    connect(compactModel_, &QAbstractItemModel::modelReset, this, rebuildCells);
    connect(compactModel_, &QAbstractItemModel::rowsInserted, this, rebuildCells);
    connect(compactModel_, &QAbstractItemModel::rowsRemoved, this, rebuildCells);
    connect(compactModel_, &QAbstractItemModel::rowsMoved, this, rebuildCells);
    connect(compactModel_, &QAbstractItemModel::dataChanged, this, rebuildCells);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged,
            this, rebuildCells);
    connect(verticalScrollBar(), &QAbstractSlider::rangeChanged,
            this, rebuildCells);
    connect(horizontalScrollBar(), &QAbstractSlider::valueChanged,
            this, [this](int value) {
                if (value != 0)
                    horizontalScrollBar()->setValue(0);
                horizontalHeader()->setOffset(0);
            });

    connect(appearancesettings::notifier(),
            &appearancesettings::Notifier::changed, this, [this] {
                layoutColumns();
                horizontalHeader()->viewport()->update();
                viewport()->update();
            });
    connect(generalsettings::notifier(), &generalsettings::Notifier::changed,
            viewport(), qOverload<>(&QWidget::update));

    new OverlayScrollBar(this);
    layoutColumns();
}

CompactEntryView::~CompactEntryView() {
    clearInteractiveCells();
}

void CompactEntryView::setSourceModel(EntryListModel* model) {
    clearInteractiveCells();
    compactModel_->setSourceModel(model);
    layoutColumns();
    scheduleInteractiveRefresh();
}

EntryListModel* CompactEntryView::sourceModel() const {
    return qobject_cast<EntryListModel*>(compactModel_->sourceModel());
}

QModelIndex CompactEntryView::sourceIndexAt(const QPoint& viewportPos) const {
    return compactModel_->mapToSource(indexAt(viewportPos));
}

QModelIndex CompactEntryView::currentSourceIndex() const {
    return compactModel_->mapToSource(currentIndex());
}

QModelIndexList CompactEntryView::selectedSourceIndexes() const {
    QModelIndexList result;
    if (!selectionModel())
        return result;
    for (const QModelIndex& proxy : selectionModel()->selectedRows()) {
        const QModelIndex source = compactModel_->mapToSource(proxy);
        if (source.isValid())
            result.append(source);
    }
    return result;
}

void CompactEntryView::setSelectedSourceIndexes(
    const QModelIndexList& sourceIndexes,
    const QModelIndex& requestedCurrentSource) {
    if (!selectionModel() || !compactModel_->sourceModel())
        return;

    QModelIndex desiredCurrent = requestedCurrentSource;
    if (!desiredCurrent.isValid())
        desiredCurrent = currentSourceIndex();
    if (!desiredCurrent.isValid() ||
        desiredCurrent.model() != compactModel_->sourceModel() ||
        desiredCurrent.row() < 0 ||
        desiredCurrent.row() >= compactModel_->sourceModel()->rowCount())
        desiredCurrent = {};

    QItemSelection restored;
    QSet<int> rows;
    QModelIndex firstSource;
    for (const QModelIndex& source : sourceIndexes) {
        if (!source.isValid() || source.model() != compactModel_->sourceModel() ||
            source.row() < 0 ||
            source.row() >= compactModel_->sourceModel()->rowCount() ||
            rows.contains(source.row()))
            continue;
        rows.insert(source.row());
        if (!firstSource.isValid())
            firstSource = source;
        restored.select(compactModel_->index(source.row(), 0),
                        compactModel_->index(source.row(), ColumnCount - 1));
    }

    if (restored.isEmpty())
        selectionModel()->clearSelection();
    else
        selectionModel()->select(
            restored, QItemSelectionModel::ClearAndSelect |
                          QItemSelectionModel::Rows);

    if (!desiredCurrent.isValid())
        desiredCurrent = firstSource;
    if (!desiredCurrent.isValid()) {
        selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
        return;
    }

    int column = LoginColumn;
    for (int candidate = 0; candidate < ColumnCount; ++candidate) {
        if (!isColumnHidden(candidate)) {
            column = candidate;
            break;
        }
    }
    const QModelIndex proxy = compactModel_->index(desiredCurrent.row(), column);
    selectionModel()->setCurrentIndex(proxy, QItemSelectionModel::NoUpdate);
    scrollTo(proxy);
}

void CompactEntryView::setCurrentSourceIndex(const QModelIndex& sourceIndex) {
    if (!sourceIndex.isValid() || sourceIndex.model() != compactModel_->sourceModel()) {
        clearSelection();
        setCurrentIndex({});
        return;
    }
    int column = LoginColumn;
    for (int candidate = 0; candidate < ColumnCount; ++candidate) {
        if (!isColumnHidden(candidate)) {
            column = candidate;
            break;
        }
    }
    const QModelIndex proxy = compactModel_->index(sourceIndex.row(), column);
    selectionModel()->setCurrentIndex(
        proxy, QItemSelectionModel::ClearAndSelect |
                   QItemSelectionModel::Rows |
                   QItemSelectionModel::Current);
    scrollTo(proxy);
}

QPoint CompactEntryView::viewportGlobal(const QPoint& viewportPos) const {
    return viewport()->mapToGlobal(viewportPos);
}

void CompactEntryView::scrollTo(const QModelIndex& index, ScrollHint hint) {
    QTableView::scrollTo(index, hint);
    // ScrollBarAlwaysOff only hides Qt's bar; it does not stop
    // QAbstractItemView::scrollTo() from changing its value. Compact
    // Mode never permits a latent horizontal offset.
    horizontalScrollBar()->setValue(0);
    horizontalHeader()->setOffset(0);
}

void CompactEntryView::startDrag(Qt::DropActions supportedActions) {
    // Only QAbstractItemView decides whether the forwarded viewport
    // gesture has really crossed its drag threshold and is draggable.
    const bool interactiveDrag = interactiveGestureActive_;
    if (interactiveDrag)
        interactiveGestureDragged_ = true;
    QTableView::startDrag(supportedActions);
    if (interactiveDrag) {
        // QDrag::exec() is synchronous and the platform drag loop may
        // consume the physical release instead of delivering it back to
        // the original index widget. The view has completed its native
        // drag state by the time startDrag returns, so finish our child-
        // gesture bookkeeping here as well. This also guarantees that a
        // subsequent click can never inherit a stale target.
        interactivePressTarget_.clear();
        interactiveGestureActive_ = false;
        interactiveGestureDragged_ = false;
    }
}

void CompactEntryView::resizeEvent(QResizeEvent* event) {
    QTableView::resizeEvent(event);
    layoutColumns();
    scheduleInteractiveRefresh();
}

void CompactEntryView::showEvent(QShowEvent* event) {
    QTableView::showEvent(event);
    layoutColumns();
    scheduleInteractiveRefresh();
}

void CompactEntryView::hideEvent(QHideEvent* event) {
    clearInteractiveCells();
    QTableView::hideEvent(event);
}

bool CompactEntryView::eventFilter(QObject* watched, QEvent* event) {
    if (forwardingInteractiveMouse_)
        return QTableView::eventFilter(watched, event);

    // Mouse events received by an index widget never reach the viewport,
    // where QAbstractItemView maintains its private press/selection/drag
    // state. Send a complete mapped sequence there. The real child press
    // remains suppressed until release; it is replayed only when the view
    // did not turn the gesture into a native drag.
    const auto forwardMouse = [this](QWidget* receiver, QEvent::Type type,
                                     const QMouseEvent& source,
                                     Qt::MouseButton button,
                                     Qt::MouseButtons buttons) {
        if (!receiver)
            return;
        const QPointF globalPosition = source.globalPosition();
        const QPointF localPosition = receiver->mapFromGlobal(
            globalPosition.toPoint());
        const QPointF scenePosition = receiver->window()->mapFromGlobal(
            globalPosition.toPoint());
        QMouseEvent forwarded(type, localPosition, scenePosition,
                              globalPosition, button, buttons,
                              source.modifiers(), source.source(),
                              source.pointingDevice());
        forwarded.setTimestamp(source.timestamp());
        const bool alreadyForwarding = forwardingInteractiveMouse_;
        forwardingInteractiveMouse_ = true;
        QApplication::sendEvent(receiver, &forwarded);
        forwardingInteractiveMouse_ = alreadyForwarding;
    };

    if (interactiveGestureActive_) {
        if (event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (!mouse->buttons().testFlag(Qt::LeftButton)) {
                interactivePressTarget_.clear();
                interactiveGestureActive_ = false;
                interactiveGestureDragged_ = false;
                // A platform can occasionally report the lost button as
                // a move instead of a release. Close the native sequence
                // explicitly so no private pressed index survives.
                forwardMouse(viewport(), QEvent::MouseButtonRelease, *mouse,
                             Qt::LeftButton, Qt::NoButton);
                return true;
            }
            forwardMouse(viewport(), QEvent::MouseMove, *mouse,
                         Qt::NoButton, mouse->buttons());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                QPointer<QWidget> clickTarget = interactivePressTarget_;
                const bool replayClick = !interactiveGestureDragged_;
                interactivePressTarget_.clear();
                interactiveGestureActive_ = false;
                interactiveGestureDragged_ = false;

                // Release the exact native press that was forwarded to
                // the viewport before invoking any child-side behavior.
                forwardMouse(viewport(), QEvent::MouseButtonRelease, *mouse,
                             Qt::LeftButton, mouse->buttons());
                if (replayClick && clickTarget) {
                    forwardMouse(clickTarget, QEvent::MouseButtonPress, *mouse,
                                 Qt::LeftButton, Qt::LeftButton);
                    if (clickTarget)
                        forwardMouse(clickTarget, QEvent::MouseButtonRelease,
                                     *mouse, Qt::LeftButton, Qt::NoButton);
                }
                return true;
            }
        }
    }

    const QVariant rowValue = watched->property(kInteractiveRowProperty);
    if (rowValue.isValid()) {
        const int row = rowValue.toInt();
        const int column = watched->property("compactEntryColumn").toInt();
        const QModelIndex index = compactModel_->index(row, column);
        if ((event->type() == QEvent::MouseButtonPress ||
             event->type() == QEvent::MouseButtonDblClick) &&
            index.isValid()) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                interactivePressTarget_ = qobject_cast<QWidget*>(watched);
                interactiveGestureActive_ = true;
                interactiveGestureDragged_ = false;
                forwardMouse(viewport(), event->type(), *mouse,
                             Qt::LeftButton, mouse->buttons());
                return true;
            }
            if (mouse->button() == Qt::RightButton) {
                forwardMouse(viewport(), event->type(), *mouse,
                             Qt::RightButton, mouse->buttons());
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease && index.isValid()) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton ||
                mouse->button() == Qt::RightButton) {
                forwardMouse(viewport(), QEvent::MouseButtonRelease, *mouse,
                             mouse->button(), mouse->buttons());
                return true;
            }
        }
        if (event->type() == QEvent::ContextMenu) {
            const auto* context = static_cast<QContextMenuEvent*>(event);
            emit contextMenuRequested(
                viewport()->mapFromGlobal(context->globalPos()));
            return true;
        }
    }
    return QTableView::eventFilter(watched, event);
}

void CompactEntryView::layoutColumns() {
    if (layingOutColumns_ || !viewport())
        return;
    layingOutColumns_ = true;

    std::array<bool, ColumnCount> visible{};
    for (int column = 0; column < ColumnCount; ++column)
        visible[column] = appearancesettings::compactColumnEnabled(
            settingColumn(column));
    visible[LoginColumn] = true;
    visible[PasswordColumn] = true;

    const int available = qMax(0, viewport()->width());
    const auto minimumTotal = [&visible] {
        int total = 0;
        for (int column = 0; column < ColumnCount; ++column)
            if (visible[column])
                total += kColumnMetrics[column].minimum;
        return total;
    };

    // Optional columns disappear only after every visible column has
    // reached its useful minimum. Preferences are not mutated: a wider
    // resize brings them back immediately in canonical order.
    constexpr std::array<int, 4> kHideOrder = {
        DateColumn, NoteColumn, UrlColumn, NameColumn};
    for (const int column : kHideOrder) {
        if (minimumTotal() <= available)
            break;
        visible[column] = false;
    }

    std::array<int, ColumnCount> widths{};
    int used = 0;
    for (int column = 0; column < ColumnCount; ++column) {
        setColumnHidden(column, !visible[column]);
        if (visible[column]) {
            widths[column] = kColumnMetrics[column].minimum;
            used += widths[column];
        }
    }

    // At the extreme narrow end Login and Password split every pixel;
    // neither is ever hidden, and both text/widgets elide naturally.
    if (used > available) {
        widths.fill(0);
        widths[LoginColumn] = available / 2;
        widths[PasswordColumn] = available - widths[LoginColumn];
    } else {
        int extra = available - used;
        // First grow every column toward its preferred width. Integer
        // grow weights keep the distribution stable and deterministic.
        while (extra > 0) {
            bool progressed = false;
            for (int column = 0; column < ColumnCount && extra > 0; ++column) {
                if (!visible[column] ||
                    widths[column] >= kColumnMetrics[column].preferred)
                    continue;
                for (int unit = 0;
                     unit < kColumnMetrics[column].grow && extra > 0 &&
                     widths[column] < kColumnMetrics[column].preferred;
                     ++unit) {
                    ++widths[column];
                    --extra;
                    progressed = true;
                }
            }
            if (!progressed)
                break;
        }
        // A very wide table has no blank tail: distribute the remainder
        // with the same weights, keeping the header length exact.
        while (extra > 0) {
            for (int column = 0; column < ColumnCount && extra > 0; ++column) {
                if (!visible[column])
                    continue;
                for (int unit = 0;
                     unit < kColumnMetrics[column].grow && extra > 0;
                     ++unit) {
                    ++widths[column];
                    --extra;
                }
            }
        }
    }

    for (int column = 0; column < ColumnCount; ++column)
        if (visible[column])
            setColumnWidth(column, widths[column]);

    horizontalScrollBar()->setValue(0);
    horizontalHeader()->setOffset(0);

    layingOutColumns_ = false;
}

void CompactEntryView::scheduleInteractiveRefresh() {
    if (interactiveRefreshPending_)
        return;
    interactiveRefreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        interactiveRefreshPending_ = false;
        refreshInteractiveCells();
    });
}

void CompactEntryView::refreshInteractiveCells() {
    if (!isVisible()) {
        clearInteractiveCells();
        return;
    }
    const int rowCount = compactModel_->rowCount();
    if (rowCount == 0) {
        clearInteractiveCells();
        return;
    }

    int first = rowAt(0);
    if (first < 0)
        first = 0;
    int last = rowAt(qMax(0, viewport()->height() - 1));
    if (last < 0)
        last = rowCount - 1;
    first = qBound(0, first, rowCount - 1);
    last = qBound(first, last, rowCount - 1);

    // Keep just one row of overscan above/below the viewport. This
    // avoids flashes during pixel scrolling without creating particle
    // timers or QString copies for the rest of a large directory.
    first = qMax(0, first - 1);
    last = qMin(rowCount - 1, last + 1);

    const auto discardRow = [this, rowCount](int row) {
        if (row < 0 || row >= rowCount)
            return;
        for (const int column : {LoginColumn, PasswordColumn}) {
            const QModelIndex index = compactModel_->index(row, column);
            auto* cell = dynamic_cast<InteractiveCell*>(indexWidget(index));
            if (!cell)
                continue;
            if (cell->login())
                cell->login()->clear();
            if (cell->password())
                cell->password()->clear();
            setIndexWidget(index, nullptr);
        }
    };
    if (interactiveFirstRow_ >= 0) {
        for (int row = interactiveFirstRow_; row <= interactiveLastRow_; ++row)
            if (row < first || row > last)
                discardRow(row);
    }

    for (int row = first; row <= last; ++row) {
        const auto install = [this, row](int column, InteractiveCell::Kind kind,
                                        const QString& value) {
            const QModelIndex index = compactModel_->index(row, column);
            auto* cell = dynamic_cast<InteractiveCell*>(indexWidget(index));
            if (!cell || cell->kind() != kind) {
                cell = new InteractiveCell(kind);
                setIndexWidget(index, cell);
                cell->installEventFilter(this);
                for (QObject* child : cell->children()) {
                    if (auto* widget = qobject_cast<QWidget*>(child))
                        widget->installEventFilter(this);
                }
            }
            cell->setProperty(kInteractiveRowProperty, row);
            cell->setProperty("compactEntryColumn", column);
            for (QObject* child : cell->children()) {
                if (auto* widget = qobject_cast<QWidget*>(child)) {
                    widget->setProperty(kInteractiveRowProperty, row);
                    widget->setProperty("compactEntryColumn", column);
                }
            }
            if (kind == InteractiveCell::Kind::Login)
                cell->login()->setText(value);
            else
                cell->password()->setSecret(value);
            if (kind == InteractiveCell::Kind::Login)
                cell->login()->setVisible(!value.isEmpty());
            else
                cell->password()->setVisible(!value.isEmpty());
        };

        const QModelIndex login = compactModel_->index(row, LoginColumn);
        install(LoginColumn, InteractiveCell::Kind::Login,
                login.data(EntryListModel::LoginRole).toString());
        const QModelIndex password = compactModel_->index(row, PasswordColumn);
        install(PasswordColumn, InteractiveCell::Kind::Password,
                password.data(EntryListModel::PasswordRole).toString());
    }
    interactiveFirstRow_ = first;
    interactiveLastRow_ = last;
}

void CompactEntryView::clearInteractiveCells() {
    // Index widgets can outlive a source reset until the event loop has
    // processed their deferred deletion. Drop their logical copies of
    // credentials synchronously before source Entry pointers go away.
    interactivePressTarget_.clear();
    interactiveGestureActive_ = false;
    interactiveGestureDragged_ = false;
    QList<QPair<int, int>> occupied;
    const auto widgets = viewport()->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        auto* cell = dynamic_cast<InteractiveCell*>(widget);
        if (!cell)
            continue;
        if (cell->login())
            cell->login()->clear();
        if (cell->password())
            cell->password()->clear();
        occupied.append({cell->property(kInteractiveRowProperty).toInt(),
                         cell->property("compactEntryColumn").toInt()});
    }
    for (const auto& [row, column] : occupied) {
        const QModelIndex index = compactModel_->index(row, column);
        if (index.isValid())
            setIndexWidget(index, nullptr);
    }
    interactiveFirstRow_ = -1;
    interactiveLastRow_ = -1;
}
