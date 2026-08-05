#include "entrylistdelegate.hpp"

#include <QPainter>

#include "appearancesettings.hpp"
#include "entrycolors.hpp"
#include "fonts.hpp"
#include "generalsettings.hpp"
#include "models/entrylistmodel.hpp"

namespace {
constexpr int kRowHeight = 66;
}

void EntryListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect rect = option.rect;
    const bool selected = option.state.testFlag(QStyle::State_Selected);

    painter->fillRect(rect, appearancesettings::palette().window);
    painter->setPen(appearancesettings::palette().separator);
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    const QRect content = rect.adjusted(10, 4, -10, -6);
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(appearancesettings::accentColor());
        painter->drawRoundedRect(content, 10, 10);
    } else if (generalsettings::entryColorsEnabled()) {
        const auto color = static_cast<nightlock::EntryColor>(
            index.data(EntryListModel::ColorRole).toInt());
        if (color != nightlock::EntryColor::None) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(entrycolors::subtleFill(color));
            painter->drawRoundedRect(content, 10, 10);
        }
    }

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(content.left() + 12, content.center().y() - 17, 34, 34);
    icon.paint(painter, iconRect);

    const int textLeft = iconRect.right() + 16;
    const int textWidth = content.right() - textLeft - 8;

    QFont nameFont(fonts::resolvedFamily(fonts::Role::Secondary), 14);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(selected ? appearancesettings::accentTextColor()
                             : appearancesettings::palette().ink);
    const QRect nameRect(textLeft, content.top() + 10, textWidth, 20);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(EntryListModel::NameRole).toString());

    QFont loginFont = option.font;
    loginFont.setPointSize(11);
    painter->setFont(loginFont);
    const bool strongSubtitle = index.data(EntryListModel::SubtitleStrongRole).toBool();
    QColor selectedLogin = appearancesettings::accentTextColor();
    if (!strongSubtitle)
        selectedLogin.setAlpha(190);
    painter->setPen(selected ? selectedLogin
                             : strongSubtitle ? appearancesettings::palette().ink
                                              : appearancesettings::palette().muted);
    const QRect loginRect(textLeft, nameRect.bottom() + 2, textWidth, 16);
    painter->drawText(loginRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(EntryListModel::SubtitleRole).toString());

    painter->restore();
}

QSize EntryListDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {0, kRowHeight};
}
