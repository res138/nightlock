#include "entrylistdelegate.hpp"

#include <QPainter>

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

    painter->fillRect(rect, Qt::white);
    painter->setPen(QColor(0xE4, 0xE4, 0xE4));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    const QRect content = rect.adjusted(10, 4, -10, -6);
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(Qt::black);
        painter->drawRoundedRect(content, 10, 10);
    }

    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(content.left() + 12, content.center().y() - 17, 34, 34);
    icon.paint(painter, iconRect);

    const int textLeft = iconRect.right() + 16;
    const int textWidth = content.right() - textLeft - 8;

    QFont nameFont(QStringLiteral("Georgia"), 14);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(selected ? Qt::white : Qt::black);
    const QRect nameRect(textLeft, content.top() + 10, textWidth, 20);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(EntryListModel::NameRole).toString());

    QFont loginFont = option.font;
    loginFont.setPointSize(11);
    painter->setFont(loginFont);
    painter->setPen(selected ? QColor(0xCF, 0xCF, 0xCF) : QColor(0x77, 0x77, 0x77));
    const QRect loginRect(textLeft, nameRect.bottom() + 2, textWidth, 16);
    painter->drawText(loginRect, Qt::AlignLeft | Qt::AlignVCenter,
                      index.data(EntryListModel::LoginRole).toString());

    painter->restore();
}

QSize EntryListDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {0, kRowHeight};
}
