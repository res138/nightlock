#pragma once

#include <QList>
#include <QScrollArea>

class CopyLabel;
class PatternBackdrop;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class SpoilerLabel;

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

    // Toggles the floating-window look: transparent base with a
    // rounded panel and the traffic-light controls in the corner.
    void setFloatingMode(bool floating);

    // Grip callbacks (used by the internal drag handle).
    void gripPressed(const QPoint& globalPos);
    void gripDragged(const QPoint& globalPos);
    void gripReleased(const QPoint& globalPos);

    // Debug hook for NIGHTLOCK_TEST_SPOILER: "reveal" or "copied".
    void debugSpoiler(const QString& state);

signals:
    // The pencil button in the top-right corner was clicked.
    void editRequested();
    // The "Показать в графе" button under the meta card was clicked.
    void graphRequested();
    // The grip was dragged far enough while docked: float me.
    void detachRequested(const QPoint& globalPos);
    // The floating window was dropped at globalPos: dock me back if
    // this lands on the main window.
    void dropped(const QPoint& globalPos);
    // The red traffic-light button of the floating window was clicked.
    void dockRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct FieldRow {
        QFrame* frame = nullptr;
        QFrame* separator = nullptr;
        QLabel* name = nullptr;
        QLabel* value = nullptr;
        QHBoxLayout* layout = nullptr;
    };
    FieldRow makeRow(QVBoxLayout* cardLayout, const QString& label, bool last = false);
    void applyPresetLabels(int preset);
    void clearAdditionalFields();
    void refreshUrlText();
    void refreshLastVisibleRow();
    void updatePatternGeometry();

    QWidget* content_;
    QWidget* grip_;
    QToolButton* editButton_;
    QWidget* floatingControls_;
    QWidget* floatingBackdrop_;
    QPoint pressGlobal_;
    QPoint grabOffset_;
    QLabel* iconLabel_;
    QLabel* titleLabel_;
    QLabel* noteLabel_;
    QFrame* fieldsCard_;
    QVBoxLayout* fieldsLayout_;
    QWidget* seedSection_;
    QLabel* seedHeader_;
    QFrame* seedCard_;
    CopyLabel* seedCopy_;
    QVBoxLayout* seedFieldsLayout_;
    QWidget* customSection_;
    QVBoxLayout* customFieldsLayout_;
    QLabel* metaHeader_;  // pattern-zone anchor when the card hides
    PatternBackdrop* patternBackdrop_;
    FieldRow loginRow_;
    CopyLabel* loginCopy_;
    FieldRow passwordRow_;
    SpoilerLabel* passwordSpoiler_;
    FieldRow urlRow_;
    QString url_;  // shown link, re-colored on a theme switch
    FieldRow codeRow_;
    QList<FieldRow> additionalRows_;
    QList<FieldRow> seedRows_;
    QList<SpoilerLabel*> seedSpoilers_;
    QList<FieldRow> customRows_;
    FieldRow createdRow_;
    FieldRow modifiedRow_;
    QPushButton* graphButton_;
};
