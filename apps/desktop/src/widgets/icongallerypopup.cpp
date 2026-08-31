#include "icongallerypopup.hpp"

#include <QAbstractListModel>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include "appearancesettings.hpp"
#include "frostedpanel.hpp"
#include "iconpackmanager.hpp"
#include "iconreferences.hpp"
#include "overlayscrollbar.hpp"

#include <algorithm>

namespace {

constexpr int kColumns = 12;
constexpr int kCell = 46;
constexpr int kIconSize = 32;
constexpr int kMaxVisibleRows = 9;
constexpr int kViewHeight = kMaxVisibleRows * kCell;
constexpr int kTabHeight = 30;
constexpr int kWrapSlack = 4;
constexpr int kPad = 12;
constexpr int kTopPad = 8;
constexpr int kTabGap = 3;
constexpr int kGridGap = 6;
constexpr int kVisibleChromeHeight =
    kTopPad + 2 * kTabHeight + kTabGap + kGridGap;
constexpr auto kBuiltInPackId = "nightlock-default";

struct GalleryEntry {
    QString path;
    QString value;
    QString title;
};

// QIcon construction defers file loading and QListView only asks for visible
// rows.  Replacing the category is one model reset, which also drops stale
// pixmaps from the previous pack.
class GalleryModel : public QAbstractListModel {
public:
    explicit GalleryModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(entries_.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
            return {};
        const GalleryEntry& entry = entries_[index.row()];
        switch (role) {
        case Qt::DecorationRole: {
            QIcon& icon = cache_[index.row()];
            if (icon.isNull())
                icon = QIcon(entry.path);
            return icon;
        }
        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            return entry.title;
        case Qt::UserRole:
            return entry.value;
        }
        return {};
    }

    void resetEntries(QVector<GalleryEntry> entries) {
        beginResetModel();
        entries_ = std::move(entries);
        cache_.clear();
        endResetModel();
    }

    QString value(const QModelIndex& index) const {
        return index.isValid() && index.row() >= 0 && index.row() < entries_.size()
                   ? entries_[index.row()].value
                   : QString();
    }

private:
    QVector<GalleryEntry> entries_;
    mutable QHash<int, QIcon> cache_;
};

QString currentTabId(const QTabBar* tabs) {
    const int index = tabs->currentIndex();
    return index >= 0 ? tabs->tabData(index).toString() : QString();
}

int tabIndexForId(const QTabBar* tabs, const QString& id) {
    for (int index = 0; index < tabs->count(); ++index) {
        if (tabs->tabData(index).toString() == id)
            return index;
    }
    return -1;
}

void clearTabs(QTabBar* tabs) {
    while (tabs->count() > 0)
        tabs->removeTab(tabs->count() - 1);
}

void configureTabs(QTabBar* tabs, const QString& objectName,
                   const QString& accessibleName,
                   const QString& accessibleDescription) {
    tabs->setObjectName(objectName);
    tabs->setAccessibleName(accessibleName);
    tabs->setAccessibleDescription(accessibleDescription);
    tabs->setFocusPolicy(Qt::StrongFocus);
    tabs->setDrawBase(false);
    tabs->setDocumentMode(true);
    tabs->setExpanding(false);
    tabs->setMovable(false);
    tabs->setUsesScrollButtons(true);
    // Keep normalized category names readable. When many packs/categories
    // exceed the row width, QTabBar's scroll buttons provide navigation.
    tabs->setElideMode(Qt::ElideNone);
    tabs->setChangeCurrentOnDrag(false);
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tabs->setFixedHeight(kTabHeight);
}

void applyTabStyle(QTabBar* tabs) {
    const auto& palette = appearancesettings::palette();
    tabs->setStyleSheet(
        QStringLiteral(
            "QTabBar { background: transparent; border: none; }"
            "QTabBar::tab { background: transparent; color: %1; border: 1px solid "
            "transparent; border-radius: 8px; min-height: 18px; padding: 4px 10px; "
            "margin: 1px 2px 1px 0; }"
            "QTabBar::tab:hover:!selected { background: %2; }"
            "QTabBar::tab:selected { background: %3; color: %4; border-color: %3; }"
            "QTabBar::scroller { width: 42px; }"
            "QTabBar QToolButton { color: %1; background: %2; border: none; "
            "border-radius: 6px; width: 20px; }")
            .arg(palette.ink.name(QColor::HexArgb),
                 palette.inputHover.name(QColor::HexArgb),
                 appearancesettings::accentColor().name(QColor::HexArgb),
                 appearancesettings::accentTextColor().name(QColor::HexArgb)));
}

QString iconTitle(const iconpacks::Icon& icon) {
    if (!icon.title.trimmed().isEmpty())
        return icon.title.trimmed();
    return QFileInfo(icon.filePath).completeBaseName();
}

QString tabLabel(QString title) {
    // QTabBar treats '&' as a mnemonic marker. Pack/category names are data,
    // not shortcuts, so escape it to keep names such as "Folders & Places"
    // visible verbatim.
    return title.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

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
    setObjectName(QStringLiteral("iconGalleryPopup"));
    setAccessibleName(tr("Icon gallery"));

    model_ = new GalleryModel(this);

    packTabs_ = new QTabBar;
    configureTabs(packTabs_, QStringLiteral("iconGalleryPackTabs"),
                  tr("Icon pack"),
                  tr("Choose an installed icon pack. Use Left and Right Arrow to move."));
    categoryTabs_ = new QTabBar;
    configureTabs(categoryTabs_, QStringLiteral("iconGalleryCategoryTabs"),
                  tr("Icon category"),
                  tr("Choose an icon category. Use Left and Right Arrow to move."));
    applyTabStyle(packTabs_);
    applyTabStyle(categoryTabs_);

    view_ = new QListView;
    view_->setObjectName(QStringLiteral("iconGallery"));
    view_->setAccessibleName(tr("Icons"));
    view_->setAccessibleDescription(
        tr("Choose an icon. Use the arrow keys to move and Enter to select."));
    view_->setModel(model_);
    view_->setViewMode(QListView::IconMode);
    view_->setFlow(QListView::LeftToRight);
    view_->setWrapping(true);
    view_->setMovement(QListView::Static);
    view_->setResizeMode(QListView::Adjust);
    view_->setLayoutMode(QListView::Batched);
    view_->setBatchSize(kColumns * 4);
    view_->setUniformItemSizes(true);
    view_->setIconSize(QSize(kIconSize, kIconSize));
    view_->setGridSize(QSize(kCell, kCell));
    view_->setSpacing(0);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setSelectionBehavior(QAbstractItemView::SelectItems);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setFocusPolicy(Qt::StrongFocus);
    view_->setMouseTracking(true);
    view_->setAttribute(Qt::WA_MacShowFocusRect, false);
    view_->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    view_->viewport()->setAutoFillBackground(false);

    emptyLabel_ = new QLabel(view_->viewport());
    emptyLabel_->setObjectName(QStringLiteral("iconGalleryEmpty"));
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    emptyLabel_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; padding: 24px; }")
            .arg(appearancesettings::palette().faint.name(QColor::HexArgb)));

    // The four pixels of wrap slack keep QListView at exactly twelve columns
    // even after style/layout rounding.  The grid reaches the bottom panel rim;
    // the selector rows receive a small, regular top inset.
    const int contentWidth = kColumns * kCell + kWrapSlack;

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(frosted::kShadow + kPad, frosted::kShadow + kTopPad,
                               frosted::kShadow + kPad - kWrapSlack,
                               frosted::kShadow);
    layout->setSpacing(0);
    layout->addWidget(packTabs_);
    layout->addSpacing(kTabGap);
    layout->addWidget(categoryTabs_);
    layout->addSpacing(kGridGap);
    layout->addWidget(view_);

    setFixedSize(2 * (frosted::kShadow + kPad) + kColumns * kCell,
                 2 * frosted::kShadow + kTopPad + 2 * kTabHeight + kTabGap +
                     kGridGap + kViewHeight);
    view_->setFixedSize(contentWidth, kViewHeight);

    new OverlayScrollBar(view_);

    // Blur bands hug the grid rims, not the two selectors above it.
    topFade_ = new EdgeFade(true, view_, this);
    topFade_->raise();
    bottomFade_ = new EdgeFade(false, view_, this);
    bottomFade_->raise();
    layout->activate();
    placeFades();

    // The bands re-blur whatever scrolls beneath them.
    connect(view_->verticalScrollBar(), &QAbstractSlider::valueChanged, this, [this] {
        topFade_->update();
        bottomFade_->update();
    });

    connect(packTabs_, &QTabBar::currentChanged, this,
            [this](int) { reloadCategoryTabs(); });
    connect(categoryTabs_, &QTabBar::currentChanged, this,
            [this](int) { reloadIcons(); });
    connect(view_, &QListView::clicked, this,
            [this](const QModelIndex& index) { activateIcon(index); });
    connect(view_, &QListView::activated, this,
            [this](const QModelIndex& index) { activateIcon(index); });

    auto* manager = iconpacks::IconPackManager::instance();
    connect(manager, &iconpacks::IconPackManager::catalogChanged, this,
            [this] { reloadPackTabs(); });
    connect(manager, &iconpacks::IconPackManager::packChanged, this,
            [this](const QString&) { reloadPackTabs(); });
    connect(appearancesettings::notifier(), &appearancesettings::Notifier::changed,
            this, [this] {
                applyTabStyle(packTabs_);
                applyTabStyle(categoryTabs_);
                emptyLabel_->setStyleSheet(
                    QStringLiteral(
                        "QLabel { color: %1; background: transparent; padding: 24px; }")
                        .arg(appearancesettings::palette().faint.name(QColor::HexArgb)));
                update();
            });

    reloadPackTabs();

    // Debug hook: NIGHTLOCK_GALLERY_SCROLL=<px> opens the grid
    // scrolled, for screenshotting the mid-scroll state.
    if (qEnvironmentVariableIsSet("NIGHTLOCK_GALLERY_SCROLL")) {
        const int value = qEnvironmentVariable("NIGHTLOCK_GALLERY_SCROLL").toInt();
        QTimer::singleShot(60, view_, [this, value] {
            view_->verticalScrollBar()->setValue(value);
        });
    }
}

void IconGalleryPopup::reloadPackTabs() {
    const QString previousId = currentTabId(packTabs_);
    QVector<iconpacks::Pack> packs =
        iconpacks::IconPackManager::instance()->installedPacks();

    // The manager contract already puts Built-in first. Keep the UI invariant
    // defensive if a future source returns a different installed order.
    const auto builtIn = std::find_if(
        packs.begin(), packs.end(), [](const iconpacks::Pack& pack) {
            return pack.id == QLatin1String(kBuiltInPackId);
        });
    if (builtIn != packs.end() && builtIn != packs.begin())
        std::rotate(packs.begin(), builtIn, builtIn + 1);

    QSignalBlocker blocker(packTabs_);
    clearTabs(packTabs_);
    for (const iconpacks::Pack& pack : packs) {
        const QString title = pack.title.trimmed().isEmpty()
                                  ? pack.id == QLatin1String(kBuiltInPackId)
                                        ? tr("Built-in")
                                        : pack.id
                                  : pack.title.trimmed();
        const int index = packTabs_->addTab(tabLabel(title));
        packTabs_->setTabData(index, pack.id);
        packTabs_->setTabToolTip(
            index, pack.description.trimmed().isEmpty()
                       ? title
                       : tr("%1 — %2").arg(title, pack.description.trimmed()));
    }

    int selected = tabIndexForId(packTabs_, previousId);
    if (selected < 0)
        selected = tabIndexForId(packTabs_, QLatin1String(kBuiltInPackId));
    if (selected < 0 && packTabs_->count() > 0)
        selected = 0;
    packTabs_->setCurrentIndex(selected);
    blocker.unblock();
    reloadCategoryTabs();
}

void IconGalleryPopup::reloadCategoryTabs() {
    const QString previousId = currentTabId(categoryTabs_);
    const auto pack = iconpacks::IconPackManager::instance()->pack(
        currentTabId(packTabs_));

    QSignalBlocker blocker(categoryTabs_);
    clearTabs(categoryTabs_);
    if (pack) {
        for (const QString& normalizedId : iconpacks::normalizedCategoryIds()) {
            const auto category = std::find_if(
                pack->categories.cbegin(), pack->categories.cend(),
                [&normalizedId](const iconpacks::Category& candidate) {
                    return candidate.id == normalizedId;
                });
            if (category == pack->categories.cend())
                continue;
            QString title = iconpacks::canonicalCategoryTitle(category->id);
            if (title.isEmpty())
                title = category->title.trimmed().isEmpty() ? category->id
                                                             : category->title.trimmed();
            const int index = categoryTabs_->addTab(tabLabel(title));
            categoryTabs_->setTabData(index, category->id);
            categoryTabs_->setTabToolTip(
                index,
                category->icons.size() == 1
                    ? tr("%1 · 1 icon").arg(title)
                    : tr("%1 · %2 icons").arg(title).arg(category->icons.size()));
        }
    }

    int selected = tabIndexForId(categoryTabs_, previousId);
    if (selected < 0 && categoryTabs_->count() > 0)
        selected = 0;
    categoryTabs_->setCurrentIndex(selected);
    categoryTabs_->setEnabled(categoryTabs_->count() > 0);
    blocker.unblock();
    reloadIcons();
}

void IconGalleryPopup::reloadIcons() {
    QVector<GalleryEntry> entries;
    QString emptyText;
    QString accessibleTitle = tr("Icons");

    const auto pack = iconpacks::IconPackManager::instance()->pack(
        currentTabId(packTabs_));
    if (!pack) {
        emptyText = tr("No installed icon packs.");
    } else {
        const QString categoryId = currentTabId(categoryTabs_);
        const auto category = std::find_if(
            pack->categories.cbegin(), pack->categories.cend(),
            [&categoryId](const iconpacks::Category& candidate) {
                return candidate.id == categoryId;
            });
        if (category == pack->categories.cend()) {
            emptyText = tr("This icon pack has no categories.");
        } else {
            entries.reserve(category->icons.size());
            for (const iconpacks::Icon& icon : category->icons) {
                const QString value = iconreferences::build(
                    pack->id, category->id, icon.id);
                const QString path = iconreferences::resolve(value);
                if (!path.isEmpty())
                    entries.push_back({path, value, iconTitle(icon)});
            }
            accessibleTitle = tr("%1 icons").arg(
                iconpacks::canonicalCategoryTitle(category->id));
            if (entries.isEmpty())
                emptyText = tr("No icons in this category.");
        }
    }

    auto* model = static_cast<GalleryModel*>(model_);
    model->resetEntries(std::move(entries));
    selectionCommitted_ = false;
    view_->verticalScrollBar()->setValue(0);
    view_->clearSelection();
    if (model_->rowCount() > 0) {
        view_->setCurrentIndex(model_->index(0, 0));
        view_->clearSelection();
    }
    view_->setAccessibleName(accessibleTitle);
    emptyLabel_->setText(emptyText);
    emptyLabel_->setAccessibleName(emptyText);
    emptyLabel_->setVisible(model_->rowCount() == 0);
    topFade_->setVisible(model_->rowCount() > 0);
    bottomFade_->setVisible(model_->rowCount() > 0);
    QTimer::singleShot(0, this, [this] {
        placeFades();
        topFade_->update();
        bottomFade_->update();
    });
}

void IconGalleryPopup::activateIcon(const QModelIndex& index) {
    if (selectionCommitted_)
        return;
    const QString value = static_cast<GalleryModel*>(model_)->value(index);
    if (value.isEmpty())
        return;
    selectionCommitted_ = true;
    close();
    emit iconSelected(value);
}

void IconGalleryPopup::placeFades() {
    if (!view_ || !view_->viewport() || !topFade_ || !bottomFade_)
        return;
    const QPoint origin = view_->viewport()->mapTo(this, QPoint(0, 0));
    const QSize viewportSize = view_->viewport()->size();
    topFade_->setGeometry(origin.x(), origin.y(), viewportSize.width(),
                          topFade_->height());
    bottomFade_->setGeometry(origin.x(),
                             origin.y() + viewportSize.height() - bottomFade_->height(),
                             viewportSize.width(), bottomFade_->height());
    emptyLabel_->setGeometry(view_->viewport()->rect());
    topFade_->raise();
    bottomFade_->raise();
    emptyLabel_->raise();
}

void IconGalleryPopup::fitHeightToAvailableGeometry(const QRect& available) {
    // Keep all twelve columns, but expose only as many complete grid rows as
    // the current screen can hold. One row is the usable minimum; the corner
    // clamp below still behaves deterministically on surfaces too small even
    // for the selector chrome plus that row.
    const int availableGridHeight =
        qMax(0, available.height() - kVisibleChromeHeight);
    const int visibleRows =
        qMax(1, qMin(kMaxVisibleRows, availableGridHeight / kCell));
    const int viewHeight = visibleRows * kCell;

    view_->setFixedHeight(viewHeight);
    setFixedHeight(2 * frosted::kShadow + kVisibleChromeHeight + viewHeight);
    layout()->activate();
}

QPoint IconGalleryPopup::clampedCorner(const QPoint& desiredCorner,
                                       const QRect& available,
                                       const QSize& popupSize) {
    const int minimumX = available.left() - frosted::kShadow;
    const int minimumY = available.top() - frosted::kShadow;
    const int maximumX = available.x() + available.width() - popupSize.width() +
                         frosted::kShadow;
    const int maximumY = available.y() + available.height() - popupSize.height() +
                         frosted::kShadow;

    // A very small screen can be narrower than the fixed twelve-column grid,
    // or shorter than the minimum one-row layout. In that case there is no
    // valid interval to clamp into, so align the visible panel to the leading
    // edge instead of passing reversed bounds to qBound().
    const auto clampCoordinate = [](int desired, int minimum, int maximum) {
        if (maximum < minimum)
            return minimum;
        return qMax(minimum, qMin(desired, maximum));
    };
    return {clampCoordinate(desiredCorner.x(), minimumX, maximumX),
            clampCoordinate(desiredCorner.y(), minimumY, maximumY)};
}

void IconGalleryPopup::popupAt(const QPoint& globalPos) {
    QPoint corner = globalPos - QPoint(frosted::kShadow, frosted::kShadow);
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        fitHeightToAvailableGeometry(available);
        corner = clampedCorner(corner, available, size());
    }
    move(corner);
    show();
}

void IconGalleryPopup::paintEvent(QPaintEvent*) {
    frosted::paintPanel(this, frosted::panelRect(this), 1.0, backdrop_, backdropOffset_);
}

void IconGalleryPopup::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { placeFades(); });
}

void IconGalleryPopup::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    backdrop_ = frosted::captureBackdrop(this, &backdropOffset_);
    QTimer::singleShot(0, this, [this] {
        placeFades();
        if (model_->rowCount() > 0)
            view_->setFocus(Qt::PopupFocusReason);
    });

    if constexpr (frosted::kUseOpaquePopupSurface) {
        // Avoid a second alpha pass over the translucent Win32 popup.  The shared
        // Windows panel path is already opaque inside its rounded outline.
        setWindowOpacity(1.0);
        update();
    } else {
        setWindowOpacity(0.0);
        auto* fade = new QVariantAnimation(this);
        fade->setDuration(130);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        connect(fade, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& value) { setWindowOpacity(value.toReal()); });
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    }
}
