#include "entrylistdelegate.hpp"

#include <QAbstractItemView>
#include <QPainter>

#include "appearancesettings.hpp"
#include "entrycolors.hpp"
#include "fonts.hpp"
#include "generalsettings.hpp"
#include "models/entrylistmodel.hpp"

EntryListDelegate::EntryListDelegate(QObject* parentObject)
    : QStyledItemDelegate(parentObject) {
    connect(appearancesettings::notifier(),
            &appearancesettings::Notifier::entryListItemSizeChanged, this,
            [this] {
                auto* view = qobject_cast<QAbstractItemView*>(this->parent());
                if (!view)
                    return;
                // QListView caches delegate size hints; force one layout pass
                // so existing rows shrink/grow as soon as the dropdown moves.
                view->doItemsLayout();
                view->updateGeometry();
                view->viewport()->update();
            });
}

void EntryListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect rect = option.rect;
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const appearancesettings::EntryListItemMetrics metrics =
        appearancesettings::entryListItemMetrics();
    const bool small = metrics.rowHeight < 66;

    painter->fillRect(rect, appearancesettings::palette().window);
    painter->setPen(appearancesettings::palette().separator);
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    const QRect content = small ? rect.adjusted(8, 3, -8, -4)
                                : rect.adjusted(10, 4, -10, -6);
    const int cornerRadius = small ? 8 : 10;
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(appearancesettings::accentColor());
        painter->drawRoundedRect(content, cornerRadius, cornerRadius);
    } else if (generalsettings::entryColorsEnabled()) {
        const auto color = static_cast<nightlock::EntryColor>(
            index.data(EntryListModel::ColorRole).toInt());
        if (color != nightlock::EntryColor::None) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(entrycolors::subtleFill(color));
            painter->drawRoundedRect(content, cornerRadius, cornerRadius);
        }
    }

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(content.left() + (small ? 10 : 12),
                         content.center().y() - metrics.iconExtent / 2,
                         metrics.iconExtent, metrics.iconExtent);
    icon.paint(painter, iconRect);

    const int textLeft = iconRect.right() + (small ? 12 : 16);
    const int textWidth = content.right() - textLeft - (small ? 6 : 8);

    // QFont's constructor takes points, while every surrounding metric
    // in this delegate is expressed in logical pixels. At Windows' 96 DPI,
    // 14pt becomes roughly 19px and crowds the fixed 20px title row.
    QFont nameFont(fonts::resolvedFamily(fonts::Role::Secondary));
    nameFont.setPixelSize(metrics.nameFontPixelSize);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(selected ? appearancesettings::accentTextColor()
                             : appearancesettings::palette().ink);
    const QRect nameRect(textLeft, content.top() + (small ? 5 : 10), textWidth,
                         small ? 18 : 20);
    const QString name = index.data(EntryListModel::NameRole).toString();
    const bool expired = index.data(EntryListModel::ExpiredRole).toBool();
    if (!expired) {
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(nameFont).elidedText(name, Qt::ElideRight,
                                                          nameRect.width()));
    } else {
        QFont expiredFont(fonts::resolvedFamily(fonts::Role::Primary));
        expiredFont.setPixelSize(metrics.subtitleFontPixelSize);
        expiredFont.setItalic(true);
        const QString suffix = tr("(expired)");
        const int suffixWidth = QFontMetrics(expiredFont).horizontalAdvance(suffix);
        const int suffixGap = small ? 4 : 5;
        const int nameWidth = qMax(0, nameRect.width() - suffixWidth - suffixGap);
        const QString visibleName =
            QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, nameWidth);
        painter->drawText(QRect(nameRect.left(), nameRect.top(), nameWidth,
                                nameRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, visibleName);
        const int suffixLeft = nameRect.left() +
                               QFontMetrics(nameFont).horizontalAdvance(visibleName) +
                               suffixGap;
        painter->setFont(expiredFont);
        painter->setPen(QColor(QStringLiteral("#FF2D2D")));
        painter->drawText(QRect(suffixLeft, nameRect.top(), suffixWidth,
                                nameRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, suffix);
    }

    QFont loginFont = option.font;
    loginFont.setPixelSize(metrics.subtitleFontPixelSize);
    painter->setFont(loginFont);
    const bool strongSubtitle = index.data(EntryListModel::SubtitleStrongRole).toBool();
    QColor selectedLogin = appearancesettings::accentTextColor();
    if (!strongSubtitle)
        selectedLogin.setAlpha(190);
    painter->setPen(selected ? selectedLogin
                             : strongSubtitle ? appearancesettings::palette().ink
                                              : appearancesettings::palette().muted);
    const QRect loginRect(textLeft, nameRect.bottom() + (small ? 1 : 2), textWidth,
                          small ? 14 : 16);
    painter->drawText(loginRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(EntryListModel::SubtitleRole).toString());

    painter->restore();
}

QSize EntryListDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {0, appearancesettings::entryListItemMetrics().rowHeight};
}
