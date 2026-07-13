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
class EntryDetailView : public QScrollArea {
    Q_OBJECT
public:
    explicit EntryDetailView(QWidget* parent = nullptr);

    void setEntry(const nightlock::Entry* entry);

private:
    struct FieldRow {
        QFrame* frame = nullptr;
        QLabel* value = nullptr;
        QHBoxLayout* layout = nullptr;
    };
    FieldRow makeRow(QVBoxLayout* cardLayout, const QString& label, bool last = false);
    void refreshLastVisibleRow();

    QWidget* content_;
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
