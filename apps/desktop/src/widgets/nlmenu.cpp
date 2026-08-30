#include "nlmenu.hpp"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEasingCurve>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProxyStyle>
#include <QRegion>
#include <QStyleOptionMenuItem>
#include <QTextEdit>
#include <QVariantAnimation>

#include "appearancesettings.hpp"

#include "frostedpanel.hpp"

namespace {

using frosted::kRadius;
using frosted::kShadow;
using frosted::panelRect;

constexpr int kPadLeft = 12;
constexpr int kPadRight = 12;
constexpr int kIconTextGap = 10;
constexpr int kChevronColumn = 14;
constexpr int kBandHeight = 5;

// Segoe UI and the one-pixel menu SVG strokes need a little more room at
// Windows' common 100% scale than their Retina/macOS counterparts.
constexpr int kItemHeight = frosted::kUseOpaquePopupSurface ? 32 : 30;
constexpr int kIconSize = frosted::kUseOpaquePopupSurface ? 14 : 11;
constexpr int kMinItemWidth = frosted::kUseOpaquePopupSurface ? 180 : 172;
constexpr int kFontPixelSize = frosted::kUseOpaquePopupSurface ? 12 : 11;

constexpr int kRevealMs = 130;

// Hover highlight: a rounded pill inset by the same amount on every
// side. The menu has no extra vertical padding (PM_MenuVMargin is 0),
// so the gap stays identical against the panel edges, the separator
// bands and the sides.
constexpr int kHoverInset = 4;
constexpr qreal kHoverRadius = 6.5;

const QColor kDangerColor(0xFF, 0x3B, 0x30);

// Theme-following paints; the soft overlays flip to white-based in
// the dark scheme so they stay visible on the dark veil.
QColor textColor() { return appearancesettings::palette().ink; }
QColor chevronColor() { return appearancesettings::palette().muted; }
QColor softOverlay(int alpha) {
    return appearancesettings::darkActive() ? QColor(255, 255, 255, alpha + 8)
                                            : QColor(0, 0, 0, alpha);
}
QColor hoverColor() { return softOverlay(14); }
QColor hairlineColor() { return softOverlay(18); }
QColor bandColor() { return softOverlay(14); }

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
            painter->fillRect(rect, bandColor());
            painter->restore();
            return;
        }

        QAction* action = menu->actionAt(rect.center());
        const bool danger = action && action->property("danger").toBool();
        const bool enabled = item->state & State_Enabled;

        // hairline above the item, unless the item opens a group. Kept
        // visible under hover too: the pill is inset by kHoverInset, so
        // every hairline/band/edge sits at the same distance from it.
        if (action) {
            QAction* previous = nullptr;
            for (QAction* a : menu->actions()) {
                if (a == action)
                    break;
                if (a->isVisible())
                    previous = a;
            }
            if (previous && !previous->isSeparator()) {
                painter->setPen(hairlineColor());
                painter->drawLine(rect.topLeft(), rect.topRight());
            }
        }

        painter->setRenderHint(QPainter::Antialiasing);

        if ((item->state & State_Selected) && enabled) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(hoverColor());
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
                                         widget->devicePixelRatioF(),
                                         enabled ? QIcon::Normal : QIcon::Disabled);
            if (danger) {
                QPainter tint(&pixmap);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(pixmap.rect(), kDangerColor);
            }
            const QRect iconRect(x, rect.center().y() - kIconSize / 2, kIconSize, kIconSize);
            painter->drawPixmap(iconRect, pixmap);
        } else if (item->checked && item->checkType != QStyleOptionMenuItem::NotCheckable) {
            // QProxyStyle normally supplies this indicator.  CE_MenuItem is
            // fully custom here, so checked text-only actions (notably the
            // pattern picker) otherwise have no visible selected state.
            QColor checkColor = textColor();
            if (!enabled)
                checkColor.setAlpha(90);
            QPen checkPen(checkColor, 1.7);
            checkPen.setCapStyle(Qt::RoundCap);
            checkPen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(checkPen);
            painter->setBrush(Qt::NoBrush);
            const qreal cx = x + kIconSize / 2.0;
            const qreal cy = rect.center().y();
            QPainterPath check;
            check.moveTo(cx - 4.0, cy);
            check.lineTo(cx - 1.2, cy + 2.8);
            check.lineTo(cx + 4.2, cy - 3.0);
            painter->drawPath(check);
        }
        x += kIconSize + kIconTextGap;

        QColor itemColor = danger ? kDangerColor : textColor();
        if (!enabled)
            itemColor.setAlpha(90);
        painter->setPen(itemColor);
        painter->setFont(item->font);
        QString text = item->text;
        text.remove(QLatin1Char('&'));
        const QRect textRect(x, rect.top(), panel.right() - kChevronColumn - x, rect.height());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);

        if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
            const qreal cx = panel.right() - kPadRight - 3;
            const qreal cy = rect.center().y();
            QPen pen(chevronColor(), 1.6);
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

void copySubmenuPresentation(const QAction* source, QAction* target) {
    target->setText(source->text());
    target->setIcon(source->icon());
    target->setEnabled(source->isEnabled());
    target->setVisible(source->isVisible());
    target->setCheckable(source->isCheckable());
    target->setChecked(source->isChecked());
    target->setToolTip(source->toolTip());
    target->setStatusTip(source->statusTip());
    target->setWhatsThis(source->whatsThis());
    target->setData(source->data());
    target->setObjectName(source->objectName());
    target->setShortcut(source->shortcut());
}

void adoptStandardMenuContents(QMenu* source, NlMenu* target) {
    target->setTitle(source->title());
    target->setIcon(source->icon());
    const QList<QAction*> actions = source->actions();
    for (QAction* action : actions) {
        if (QMenu* sourceSubmenu = action->menu()) {
            auto* submenu = new NlMenu(target);
            // The source menu owns its menuAction, so copy presentation before
            // recursively consuming (and deleting) that source submenu.
            copySubmenuPresentation(action, submenu->menuAction());
            adoptStandardMenuContents(sourceSubmenu, submenu);
            target->addMenu(submenu);
            continue;
        }

        // createStandardContextMenu() has already connected each action to
        // the editor. Moving the QAction preserves those slots, shortcuts,
        // enabled states and accessibility semantics verbatim.
        source->removeAction(action);
        action->setParent(target);
        target->addAction(action);
    }
    delete source;
}

// Returns the nearest text editor owning the object that received the context
// event. QPlainTextEdit/QTextEdit receive mouse events through their viewport,
// while QLineEdit receives them directly.
QWidget* textEditorFor(QObject* watched) {
    auto* widget = qobject_cast<QWidget*>(watched);
    for (QWidget* candidate = widget; candidate; candidate = candidate->parentWidget()) {
        if (qobject_cast<QLineEdit*>(candidate) ||
            qobject_cast<QPlainTextEdit*>(candidate) ||
            qobject_cast<QTextEdit*>(candidate)) {
            return candidate;
        }
        if (candidate->isWindow())
            break;
    }
    return nullptr;
}

QMenu* standardTextMenu(QWidget* editor, const QPoint& globalPos) {
    if (auto* lineEdit = qobject_cast<QLineEdit*>(editor))
        return lineEdit->createStandardContextMenu();
    if (auto* plainTextEdit = qobject_cast<QPlainTextEdit*>(editor))
        return plainTextEdit->createStandardContextMenu(
            plainTextEdit->viewport()->mapFromGlobal(globalPos));
    if (auto* textEdit = qobject_cast<QTextEdit*>(editor))
        return textEdit->createStandardContextMenu(
            textEdit->viewport()->mapFromGlobal(globalPos));
    return nullptr;
}

class TextContextMenuAdapter final : public QObject {
public:
    explicit TextContextMenuAdapter(QApplication* application)
        : QObject(application) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::ContextMenu)
            return QObject::eventFilter(watched, event);

        QWidget* editor = textEditorFor(watched);
        if (!editor || editor->contextMenuPolicy() != Qt::DefaultContextMenu)
            return QObject::eventFilter(watched, event);

        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        QMenu* standard = standardTextMenu(editor, contextEvent->globalPos());
        if (!standard)
            return QObject::eventFilter(watched, event);

        auto* menu = NlMenu::fromStandardMenu(standard, editor);
        QObject::connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->popupAt(contextEvent->globalPos());
        contextEvent->accept();
        return true;
    }
};

}  // namespace

NlMenu::NlMenu(QWidget* parent) : QMenu(parent) {
    setWindowFlag(Qt::FramelessWindowHint);
#ifdef Q_OS_WIN
    // Per-pixel layered HWNDs are excluded from Windows 11's native corner
    // policy.  A transparent DWM-extended client surface gives us Acrylic and
    // system shadows without that limitation.
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
#else
    setWindowFlag(Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
#endif
    setStyle(sharedStyle());
    setContentsMargins(kShadow, kShadow, kShadow, kShadow);
    QFont f = font();
    f.setPixelSize(kFontPixelSize);
    f.setWeight(QFont::DemiBold);
    setFont(f);
}

NlMenu* NlMenu::fromStandardMenu(QMenu* standardMenu, QWidget* parent) {
    auto* menu = new NlMenu(parent);
    if (!standardMenu)
        return menu;
    adoptStandardMenuContents(standardMenu, menu);
    return menu;
}

void NlMenu::installTextContextMenuAdapter(QApplication* application) {
    if (!application || application->property("nightlockTextMenuAdapter").toBool())
        return;
    application->setProperty("nightlockTextMenuAdapter", true);
    application->installEventFilter(new TextContextMenuAdapter(application));
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
    const auto material = nativeBackdropActive_
                              ? frosted::PanelMaterial::NativeBackdrop
                              : frosted::PanelMaterial::CapturedBlur;
    frosted::paintPanel(this, currentPanelRect(), reveal_, backdrop_, backdropOffset_,
                        material);
    QMenu::paintEvent(event);
}

void NlMenu::showEvent(QShowEvent* event) {
    QMenu::showEvent(event);
#ifdef Q_OS_WIN
    // showEvent runs after QMenu has materialized its popup HWND. This reaches
    // every NlMenu, including recursively-built submenus and standard editor
    // menus adapted above.
    nativeBackdropActive_ =
        frosted::enableNativeMenuBackdrop(this, appearancesettings::darkActive());
    if (nativeBackdropActive_) {
        clearMask();
        // Native Desktop Acrylic is primary. A faint software capture remains
        // under it so a DWM/remote-session false positive never produces a
        // raw transparent popup.
        captureBackdrop();
    } else {
        // Windows 10 / early Windows 11 / disabled transparency: retain the
        // same Nightlock surface and a real blurred parent snapshot. A mask
        // keeps the fallback rounded without requiring a layered HWND.
        QPainterPath rounded;
        rounded.addRoundedRect(rect(), kRadius, kRadius);
        setMask(QRegion(rounded.toFillPolygon().toPolygon()));
        captureBackdrop();
    }
    reveal_ = 1.0;
    setWindowOpacity(1.0);
    update();
#else
    nativeBackdropActive_ = false;
    captureBackdrop();
    startRevealAnimation();
#endif
}

void NlMenu::captureBackdrop() {
    backdrop_ = frosted::captureBackdrop(this, &backdropOffset_, true);
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
