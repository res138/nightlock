#include "icongallerypopup.hpp"

#include <QAbstractListModel>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QListView>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "standardicons.hpp"
#include "frostedpanel.hpp"

namespace {

constexpr int kColumns = 8;
constexpr int kCell = 46;
constexpr int kIconSize = 32;
constexpr int kPadding = 10;   // between the panel edge and the grid
constexpr int kViewHeight = 380;

// Lazy list of pack icons: QIcon construction defers file loading, and
// QListView only asks for the visible rows, so ~1800 icons open fast.
class GalleryModel : public QAbstractListModel {
public:
    explicit GalleryModel(QStringList paths, QObject* parent = nullptr)
        : QAbstractListModel(parent), paths_(std::move(paths)) {}

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(paths_.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= paths_.size())
            return {};
        switch (role) {
        case Qt::DecorationRole: {
            QIcon& icon = cache_[index.row()];
            if (icon.isNull())
                icon = QIcon(paths_[index.row()]);
            return icon;
        }
        case Qt::ToolTipRole:
            return QFileInfo(paths_[index.row()]).completeBaseName();
        }
        return {};
    }

    QString path(const QModelIndex& index) const {
        return index.isValid() && index.row() < paths_.size() ? paths_[index.row()] : QString();
    }

private:
    QStringList paths_;
    mutable QHash<int, QIcon> cache_;
};

}  // namespace

IconGalleryPopup::IconGalleryPopup(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* model = new GalleryModel(standardicons::galleryIconPaths(), this);

    auto* view = new QListView;
    view->setObjectName(QStringLiteral("iconGallery"));
    view->setModel(model);
    view->setViewMode(QListView::IconMode);
    view->setMovement(QListView::Static);
    view->setResizeMode(QListView::Adjust);
    view->setUniformItemSizes(true);
    view->setIconSize(QSize(kIconSize, kIconSize));
    view->setGridSize(QSize(kCell, kCell));
    view->setSelectionMode(QAbstractItemView::NoSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFrameShape(QFrame::NoFrame);
    view->setAttribute(Qt::WA_MacShowFocusRect, false);
    view->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    view->viewport()->setAutoFillBackground(false);

    auto* layout = new QVBoxLayout(this);
    const int margin = frosted::kShadow + kPadding;
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->addWidget(view);

    // Width: the grid plus room for the overlay scrollbar.
    setFixedSize(kColumns * kCell + 14 + 2 * margin, kViewHeight + 2 * margin);

    connect(view, &QListView::clicked, this, [this, model](const QModelIndex& index) {
        const QString path = model->path(index);
        close();
        if (!path.isEmpty())
            emit iconSelected(path);
    });
}

void IconGalleryPopup::popupAt(const QPoint& globalPos) {
    QPoint corner = globalPos - QPoint(frosted::kShadow, frosted::kShadow);
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect available = screen->availableGeometry();
        corner.setX(qBound(available.left() - frosted::kShadow, corner.x(),
                           available.right() - width() + frosted::kShadow));
        corner.setY(qBound(available.top() - frosted::kShadow, corner.y(),
                           available.bottom() - height() + frosted::kShadow));
    }
    move(corner);
    show();
}

void IconGalleryPopup::paintEvent(QPaintEvent*) {
    frosted::paintPanel(this, frosted::panelRect(this), 1.0, backdrop_, backdropOffset_);
}

void IconGalleryPopup::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    backdrop_ = frosted::captureBackdrop(this, &backdropOffset_);

    setWindowOpacity(0.0);
    auto* fade = new QVariantAnimation(this);
    fade->setDuration(130);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    connect(fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { setWindowOpacity(value.toReal()); });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}
