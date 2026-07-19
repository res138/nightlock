#include "grouptreeview.hpp"

#include <QEasingCurve>
#include <QPainter>
#include <QProxyStyle>
#include <QVariantAnimation>

namespace {

// Insertion line between rows / rounded outline around the target folder.
class DropIndicatorStyle : public QProxyStyle {
public:
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
        if (element != PE_IndicatorItemViewItemDrop) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        if (option->rect.isNull())
            return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (option->rect.height() <= 2) {
            const qreal y = option->rect.top();
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::black);
            painter->drawEllipse(QPointF(option->rect.left() + 3, y), 3, 3);
            QPen pen(Qt::black, 2.4);
            pen.setCapStyle(Qt::RoundCap);
            painter->setPen(pen);
            painter->drawLine(QPointF(option->rect.left() + 7, y),
                              QPointF(option->rect.right() - 2, y));
        } else {
            QPen pen(Qt::black, 2);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(1, 1, -1, -1), 6, 6);
        }
        painter->restore();
    }
};

DropIndicatorStyle* sharedStyle() {
    static auto* style = new DropIndicatorStyle;
    return style;
}

// Rounded highlight that fades out over the landed row.
class FlashOverlay : public QWidget {
public:
    FlashOverlay(const QColor& color, QWidget* parent) : QWidget(parent), color_(color) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* animation = new QVariantAnimation(this);
        animation->setDuration(280);
        animation->setStartValue(1.0);
        animation->setEndValue(0.0);
        animation->setEasingCurve(QEasingCurve::OutQuad);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            opacity_ = value.toReal();
            update();
        });
        connect(animation, &QVariantAnimation::finished, this, &QWidget::deleteLater);
        animation->start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor color = color_;
        color.setAlphaF(color.alphaF() * opacity_);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(rect(), 6, 6);
    }

private:
    QColor color_;
    qreal opacity_ = 1.0;
};

}  // namespace

GroupTreeView::GroupTreeView(QWidget* parent) : QTreeView(parent) {
    setObjectName(QStringLiteral("groupTree"));
    setHeaderHidden(true);
    setIndentation(33);
    setRootIsDecorated(false);
    setIconSize(QSize(22, 22));
    setSelectionMode(QAbstractItemView::SingleSelection);
    // Renaming starts only from the context menu (via edit()); a plain
    // double-click keeps toggling expand/collapse.
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    setDragDropMode(QAbstractItemView::InternalMove);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setAnimated(true);         // smooth expand/collapse of branches
    setAutoExpandDelay(350);   // hovered folders unfold during a drag
    setStyle(sharedStyle());
}

void GroupTreeView::flashRow(const QModelIndex& index) {
    if (!index.isValid())
        return;
    const QRect rowRect = visualRect(index);
    if (rowRect.isEmpty())
        return;
    const bool selected = selectionModel() && selectionModel()->isSelected(index);
    const QColor color = selected ? QColor(255, 255, 255, 110) : QColor(0, 0, 0, 50);
    auto* overlay = new FlashOverlay(color, viewport());
    overlay->setGeometry(QRect(QPoint(0, rowRect.top()), QSize(viewport()->width(), rowRect.height())));
    overlay->show();
}
