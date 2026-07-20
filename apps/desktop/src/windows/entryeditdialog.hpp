#pragma once

#include <QDialog>

class IconPicker;
class PatternPicker;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;

namespace nightlock {
struct Entry;
}

// Standalone window for creating or editing a password entry. Only the
// name is required — Save stays disabled until it is non-empty; every
// other field may be left blank. The icon row offers the standard icon
// catalog (two icons today, designed to grow).
class EntryEditDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Add, Edit };

    explicit EntryEditDialog(Mode mode, QWidget* parent = nullptr);

    // Prefills the form from an existing entry (Edit mode).
    void setEntry(const nightlock::Entry& entry);
    // Writes the form back into `entry`; timestamps are the caller's job.
    void applyTo(nightlock::Entry& entry) const;

    // Debug hook for NIGHTLOCK_SCREENSHOT_PATTERN_MENU: opens the
    // pattern dropdown and returns it for grabbing.
    QMenu* openPatternMenuForScreenshot();

private:
    QWidget* makeField(const QString& label, QWidget* editor, bool required = false);

    IconPicker* iconPicker_;
    PatternPicker* patternPicker_;
    QLineEdit* nameEdit_;
    QLineEdit* loginEdit_;
    QLineEdit* passwordEdit_;
    QLineEdit* urlEdit_;
    QPlainTextEdit* noteEdit_;
    QPushButton* saveButton_;
};
