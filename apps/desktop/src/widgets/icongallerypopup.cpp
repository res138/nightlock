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
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "standardicons.hpp"
#include "frostedpanel.hpp"
#include "overlayscrollbar.hpp"

namespace {

constexpr int kColumns = 6;
constexpr int kCell = 46;
constexpr int kIconSize = 32;
// The grid runs the full panel height — icons slide under the very
// top/bottom edges and dissolve there.
constexpr int kViewHeight = 9 * kCell;

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

// Blur band over the grid's top/bottom edge: icons near the rim are
// genuinely blurred — full blur at the very edge, easing to sharp on
// the grid side. Nothing fades or dissolves; blur only.
class EdgeFade : public QWidget {
public:
    EdgeFade(bool top, QListView* view, QWidget* parent)
        : QWidget(parent), top_(top), view_(view) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(34);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        // Live slice of the grid under this band, blurred cheaply
        // (downscale, smooth upscale).
        QWidget* viewport = view_->viewport();
        const QPoint inViewport = viewport->mapFromGlobal(mapToGlobal(QPoint(0, 0)));
        const QPixmap strip = viewport->grab(QRect(inViewport, size()));
        if (strip.isNull())
            return;
        QImage soft = strip.toImage();
        const QSize full = soft.size();
        soft = soft.scaled(qMax(1, full.width() / 8), qMax(1, full.height() / 8),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                   .scaled(full, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPixmap blurred = QPixmap::fromImage(soft);
        blurred.setDevicePixelRatio(strip.devicePixelRatio());

        // Blur ramp: full at the rim, none on the grid side.
        QLinearGradient ramp(0, top_ ? 0 : height(), 0, top_ ? height() : 0);
        {
            QPainter mask(&blurred);
            mask.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            ramp.setColorAt(0.0, QColor(0, 0, 0, 255));
            ramp.setColorAt(1.0, QColor(0, 0, 0, 0));
            mask.fillRect(QRect(QPoint(), size()), ramp);
        }
        QPainter painter(this);
        painter.drawPixmap(0, 0, blurred);
    }

private:
    bool top_;
    QListView* view_;
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

    // Horizontal air is symmetric (the 4px wrap slack rides on the
    // right, so the right padding gives it back); vertically the grid
    // runs edge to edge — the dissolve bands own the rims.
    constexpr int kWrapSlack = 4;
    constexpr int kPad = 12;

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(frosted::kShadow + kPad, frosted::kShadow,
                               frosted::kShadow + kPad - kWrapSlack, frosted::kShadow);
    layout->addWidget(view);

    setFixedSize(2 * (frosted::kShadow + kPad) + kColumns * kCell,
                 kViewHeight + 2 * frosted::kShadow);

    new OverlayScrollBar(view);

    // Dissolve bands hugging the panel rims, exactly as wide as the
    // viewport.
    topFade_ = new EdgeFade(true, view, this);
    topFade_->setGeometry(frosted::kShadow + kPad, frosted::kShadow,
                          kColumns * kCell + kWrapSlack, topFade_->height());
    topFade_->raise();
    bottomFade_ = new EdgeFade(false, view, this);
    bottomFade_->setGeometry(frosted::kShadow + kPad,
                             height() - frosted::kShadow - bottomFade_->height(),
                             kColumns * kCell + kWrapSlack, bottomFade_->height());
    bottomFade_->raise();

    // The bands re-blur whatever scrolls beneath them.
    connect(view->verticalScrollBar(), &QAbstractSlider::valueChanged, this, [this] {
        topFade_->update();
        bottomFade_->update();
    });

    connect(view, &QListView::clicked, this, [this, model](const QModelIndex& index) {
        const QString path = model->path(index);
        close();
        if (!path.isEmpty())
            emit iconSelected(path);
    });

    // Debug hook: NIGHTLOCK_GALLERY_SCROLL=<px> opens the grid
    // scrolled, for screenshotting the mid-scroll state.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_GALLERY_SCROLL")) {
        const int value = qEnvironmentVariable("NIGHTLOCK_GALLERY_SCROLL").toInt();
        QTimer::singleShot(60, view, [view, value] {
            view->verticalScrollBar()->setValue(value);
        });
    }
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
