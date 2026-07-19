#pragma once

#include <QScrollArea>

class QFrame;
class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace nightlock {
struct Entry;
}

// Right panel: icon, title, note and the field/meta cards of one entry.
// A six-dot grip at the top-center lets the panel be dragged out of the
// main window into an independent floating window and dropped back.
class EntryDetailView : public QScrollArea {
    Q_OBJECT
public:
    explicit EntryDetailView(QWidget* parent = nullptr);

    void setEntry(const nightlock::Entry* entry);

    // Called by the owner right after reparenting the view into a
    // floating window mid-drag, so the drag continues seamlessly.
    void beginFloatingDrag(const QPoint& globalPos);

    // Grip callbacks (used by the internal drag handle).
    void gripPressed(const QPoint& globalPos);
    void gripDragged(const QPoint& globalPos);
    void gripReleased(const QPoint& globalPos);

signals:
    // The grip was dragged far enough while docked: float me.
    void detachRequested(const QPoint& globalPos);
    // The floating window was dropped at globalPos: dock me back if
    // this lands on the main window.
    void dropped(const QPoint& globalPos);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct FieldRow {
        QFrame* frame = nullptr;
        QLabel* value = nullptr;
        QHBoxLayout* layout = nullptr;
    };
    FieldRow makeRow(QVBoxLayout* cardLayout, const QString& label, bool last = false);
    void refreshLastVisibleRow();

    QWidget* content_;
    QWidget* grip_;
    QPoint pressGlobal_;
    QPoint grabOffset_;
    QLabel* iconLabel_;
    QLabel* titleLabel_;
    QLabel* noteLabel_;
    FieldRow loginRow_;
    FieldRow passwordRow_;
    FieldRow urlRow_;
    FieldRow codeRow_;
    FieldRow createdRow_;
    FieldRow modifiedRow_;
};
