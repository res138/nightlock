#pragma once

#include <QTreeView>

// Directory tree with animated drag-and-drop: smooth expand/collapse,
// auto-expand of the hovered folder while dragging, a clean black drop
// indicator and a short landing flash after a drop.
class GroupTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit GroupTreeView(QWidget* parent = nullptr);

    // Briefly highlights a row (used right after a drop).
    void flashRow(const QModelIndex& index);
};
