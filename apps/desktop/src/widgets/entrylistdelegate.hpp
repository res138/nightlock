#pragma once

#include <QStyledItemDelegate>

// Renders an entry row: icon, bold name, preset-aware subtitle and a
// bottom separator; the selected row gets a rounded accent plate.
class EntryListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};
