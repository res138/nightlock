#include "icongallerypopup.hpp"

#include <QAbstractListModel>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QScrollBar>
#include <QScreen>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "standardicons.hpp"
#include "frostedpanel.hpp"
#include "scrollbarfader.hpp"

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
            if (icon.isNull()) {
                // Preferably from the preloaded cache (decoded at app
                // start on a background thread), so fast scrolling
                // doesn't hitch on file loads.
                const QImage image = standardicons::cachedGalleryImage(paths_[index.row()]);
                icon = image.isNull() ? QIcon(paths_[index.row()])
                                      : QIcon(QPixmap::fromImage(image));
            }
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

// Soft white gradient over the grid's top/bottom edge, so scrolled
// icons fade out instead of being cut off.
class EdgeFade : public QWidget {
public:
    EdgeFade(bool top, QWidget* parent) : QWidget(parent), top_(top) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(26);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QLinearGradient gradient(0, top_ ? 0 : height(), 0, top_ ? height() : 0);
        gradient.setColorAt(0.0, QColor(255, 255, 255, 240));
        gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
        QPainter painter(this);
        painter.fillRect(rect(), gradient);
    }

private:
    bool top_;
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

    // The viewport must hold kColumns full cells AFTER the scrollbar
    // takes its slice, with slack for the icon-flow wrap math —
    // otherwise a row wraps one column early, leaving a dead white
    // strip on the right. The left padding mirrors everything sitting
    // right of the grid (slack + scrollbar + edge padding), so both
    // flanks read identically.
    const int scrollBarWidth = view->verticalScrollBar()->sizeHint().width();
    constexpr int kWrapSlack = 16;
    constexpr int kRightPad = 2;
    const int leftPad = kWrapSlack + scrollBarWidth + kRightPad;

    auto* layout = new QVBoxLayout(this);
    const int vMargin = frosted::kShadow + kPadding;
    layout->setContentsMargins(frosted::kShadow + leftPad, vMargin,
                               frosted::kShadow + kRightPad, vMargin);
    layout->addWidget(view);

    setFixedSize(2 * frosted::kShadow + leftPad + kColumns * kCell + kWrapSlack +
                     scrollBarWidth + kRightPad,
                 kViewHeight + 2 * vMargin);

    new ScrollBarFader(view);

    // Icons fade out under a gradient at both edges of the grid.
    const int fadeX = frosted::kShadow + 2;
    const int fadeWidth = width() - 2 * frosted::kShadow - 4;
    auto* topFade = new EdgeFade(true, this);
    topFade->setGeometry(fadeX, vMargin, fadeWidth, topFade->height());
    topFade->raise();
    auto* bottomFade = new EdgeFade(false, this);
    bottomFade->setGeometry(fadeX, height() - vMargin - bottomFade->height(), fadeWidth,
                            bottomFade->height());
    bottomFade->raise();

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
