#include "nlmenu.hpp"

#include <QAction>
#include <QEasingCurve>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOptionMenuItem>
#include <QVariantAnimation>

namespace {

constexpr int kShadow = 12;      // translucent margin reserved for the shadow
constexpr int kRadius = 10;
constexpr int kItemHeight = 30;
constexpr int kPadLeft = 12;
constexpr int kPadRight = 12;
constexpr int kIconSize = 11;
constexpr int kIconTextGap = 10;
constexpr int kChevronColumn = 14;
constexpr int kBandHeight = 5;
constexpr int kMinItemWidth = 172;

constexpr int kRevealMs = 130;
constexpr qreal kVeilOpacity = 0.85;  // solid color share over the blurred backdrop

// Hover highlight: a rounded pill inset by the same amount on every
// side. The menu has no extra vertical padding (PM_MenuVMargin is 0),
// so the gap stays identical against the panel edges, the separator
// bands and the sides.
constexpr int kHoverInset = 4;
constexpr qreal kHoverRadius = 6.5;

const QColor kTextColor(0x1F, 0x1F, 0x1F);
const QColor kDangerColor(0xFF, 0x3B, 0x30);
const QColor kHoverColor(0, 0, 0, 14);
const QColor kHairlineColor(0, 0, 0, 18);
const QColor kBandColor(0, 0, 0, 14);
const QColor kChevronColor(0x8A, 0x8A, 0x8E);

QRect panelRect(const QWidget* widget) {
    return widget->rect().marginsRemoved(QMargins(kShadow, kShadow, kShadow, kShadow));
}

class NlMenuStyle : public QProxyStyle {
public:
    int pixelMetric(PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override {
        switch (metric) {
        case PM_MenuPanelWidth:
        case PM_MenuHMargin:
        case PM_MenuVMargin:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                           const QSize& contentsSize, const QWidget* widget) const override {
        if (type != CT_MenuItem)
            return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (!item)
            return contentsSize;
        if (item->menuItemType == QStyleOptionMenuItem::Separator)
            return {contentsSize.width(), kBandHeight};
        const QFontMetrics metrics(item->font);
        QString text = item->text;
        text.remove(QLatin1Char('&'));
        const int width = kPadLeft + kIconSize + kIconTextGap +
                          metrics.horizontalAdvance(text) + kChevronColumn + kPadRight;
        return {qMax(width, kMinItemWidth), kItemHeight};
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
        if (element == PE_PanelMenu || element == PE_FrameMenu)
            return;  // the panel is painted by NlMenu itself
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override {
        if (element == CE_MenuEmptyArea)
            return;
        if (element != CE_MenuItem) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        const auto* menu = qobject_cast<const QMenu*>(widget);
        if (!item || !menu)
            return;

        painter->save();
        const auto* nlMenu = qobject_cast<const NlMenu*>(widget);
        const QRect clipPanel = nlMenu ? nlMenu->currentPanelRect() : panelRect(widget);
        QPainterPath clip;
        clip.addRoundedRect(clipPanel, kRadius, kRadius);
        painter->setClipPath(clip);

        const QRect rect = item->rect;

        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            painter->fillRect(rect, kBandColor);
            painter->restore();
            return;
        }

        QAction* action = menu->actionAt(rect.center());
        const bool danger = action && action->property("danger").toBool();
        const bool enabled = item->state & State_Enabled;

        // hairline above the item, unless the item opens a group or the
        // hovered pill sits right next to it (its edge would collide
        // with the rounded highlight).
        if (action) {
            QAction* previous = nullptr;
            for (QAction* a : menu->actions()) {
                if (a == action)
                    break;
                if (a->isVisible())
                    previous = a;
            }
            QAction* hovered = menu->activeAction();
            if (previous && !previous->isSeparator() && action != hovered &&
                previous != hovered) {
                painter->setPen(kHairlineColor);
                painter->drawLine(rect.topLeft(), rect.topRight());
            }
        }

        painter->setRenderHint(QPainter::Antialiasing);

        if ((item->state & State_Selected) && enabled) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(kHoverColor);
            painter->drawRoundedRect(
                QRectF(rect).adjusted(kHoverInset, kHoverInset, -kHoverInset, -kHoverInset),
                kHoverRadius, kHoverRadius);
        }

        const QRect panel = panelRect(widget);
        int x = panel.left() + kPadLeft;
        // item->icon is empty on macOS (the platform theme hides menu
        // icons), so take the icon straight from the action.
        const QIcon icon = action ? action->icon() : item->icon;
        if (!icon.isNull()) {
            QPixmap pixmap = icon.pixmap(QSize(kIconSize, kIconSize),
                                         widget->devicePixelRatio(),
                                         enabled ? QIcon::Normal : QIcon::Disabled);
            if (danger) {
                QPainter tint(&pixmap);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(pixmap.rect(), kDangerColor);
            }
            const QRect iconRect(x, rect.center().y() - kIconSize / 2, kIconSize, kIconSize);
            painter->drawPixmap(iconRect, pixmap);
        }
        x += kIconSize + kIconTextGap;

        QColor textColor = danger ? kDangerColor : kTextColor;
        if (!enabled)
            textColor.setAlpha(90);
        painter->setPen(textColor);
        painter->setFont(item->font);
        QString text = item->text;
        text.remove(QLatin1Char('&'));
        const QRect textRect(x, rect.top(), panel.right() - kChevronColumn - x, rect.height());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);

        if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
            const qreal cx = panel.right() - kPadRight - 3;
            const qreal cy = rect.center().y();
            QPen pen(kChevronColor, 1.6);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            QPainterPath chevron;
            chevron.moveTo(cx - 2.0, cy - 3.5);
            chevron.lineTo(cx + 1.5, cy);
            chevron.lineTo(cx - 2.0, cy + 3.5);
            painter->drawPath(chevron);
        }
        painter->restore();
    }
};

NlMenuStyle* sharedStyle() {
    static auto* style = new NlMenuStyle;
    return style;
}

}  // namespace

NlMenu::NlMenu(QWidget* parent) : QMenu(parent) {
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowFlag(Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyle(sharedStyle());
    setContentsMargins(kShadow, kShadow, kShadow, kShadow);
    QFont f = font();
    f.setPixelSize(11);  // 13px of the middle-pane counter, scaled down 1.2×
    f.setWeight(QFont::DemiBold);
    setFont(f);
}

void NlMenu::popupAt(const QPoint& globalPos) {
    popup(globalPos - QPoint(kShadow, kShadow));
}

QRect NlMenu::currentPanelRect() const {
    const QRect full = panelRect(this);
    if (reveal_ >= 1.0)
        return full;
    const qreal f = 0.30 + 0.70 * reveal_;
    return {full.topLeft(), QSize(qRound(full.width() * f), qRound(full.height() * f))};
}

void NlMenu::paintEvent(QPaintEvent* event) {
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        const QRectF panel = currentPanelRect();

        for (int i = kShadow; i >= 1; --i) {
            const qreal t = 1.0 - static_cast<qreal>(i) / kShadow;
            const qreal alpha = (1 + 4 * t * t) * reveal_;
            painter.setBrush(QColor(0, 0, 0, static_cast<int>(alpha)));
            painter.drawRoundedRect(panel.adjusted(-i, -i + 2, i, i + 2),
                                    kRadius + i * 0.6, kRadius + i * 0.6);
        }

        // Frosted glass: blurred backdrop showing through a white veil.
        QPainterPath path;
        path.addRoundedRect(panel, kRadius, kRadius);
        painter.save();
        painter.setClipPath(path);
        painter.fillRect(panel, Qt::white);
        if (!backdrop_.isNull())
            painter.drawPixmap(panel.topLeft() + backdropOffset_, backdrop_);
        painter.fillRect(panel, QColor(255, 255, 255, qRound(kVeilOpacity * 255)));
        painter.restore();
    }
    QMenu::paintEvent(event);
}

void NlMenu::showEvent(QShowEvent* event) {
    QMenu::showEvent(event);
    captureBackdrop();
    startRevealAnimation();
}

void NlMenu::captureBackdrop() {
    backdrop_ = QPixmap();
    backdropOffset_ = QPoint();

    // Walk up to the first non-popup window (submenus are parented to
    // their parent menu, which is itself a popup).
    QWidget* source = parentWidget();
    while (source && (!source->isWindow() || source->windowType() == Qt::Popup))
        source = source->parentWidget();
    if (!source)
        return;

    const QRect panel = panelRect(this);
    const QRect inSource(source->mapFromGlobal(mapToGlobal(panel.topLeft())), panel.size());
    const QRect clipped = inSource.intersected(source->rect());
    if (clipped.isEmpty())
        return;

    const QPixmap grabbed = source->grab(clipped);
    if (grabbed.isNull())
        return;

    // Cheap blur: heavy downscale, then smooth upscale.
    const QSize coarse(qMax(1, grabbed.width() / 12), qMax(1, grabbed.height() / 12));
    const QImage blurred = grabbed.toImage()
                               .scaled(coarse, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .scaled(grabbed.size(), Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
    backdrop_ = QPixmap::fromImage(blurred);
    backdrop_.setDevicePixelRatio(grabbed.devicePixelRatio());
    backdropOffset_ = clipped.topLeft() - inSource.topLeft();
}

void NlMenu::startRevealAnimation() {
    if (revealAnimation_) {
        revealAnimation_->stop();
        revealAnimation_->deleteLater();
    }
    // No window resizing here: the window keeps its final geometry and
    // the growth is painted (clipped), which stays smooth.
    reveal_ = 0.0;
    setWindowOpacity(0.0);
    revealAnimation_ = new QVariantAnimation(this);
    revealAnimation_->setDuration(kRevealMs);
    revealAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    revealAnimation_->setStartValue(0.0);
    revealAnimation_->setEndValue(1.0);
    connect(revealAnimation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                reveal_ = value.toReal();
                setWindowOpacity(qMin<qreal>(1.0, reveal_ * 1.8));
                update();
            });
    connect(revealAnimation_, &QVariantAnimation::finished, this, [this] {
        reveal_ = 1.0;
        setWindowOpacity(1.0);
        update();
    });
    revealAnimation_->start();
}
