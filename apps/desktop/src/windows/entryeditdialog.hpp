#pragma once

#include <QDialog>

#include <nightlock/entry.hpp>

#include <vector>

class QCheckBox;
class EntryColorPicker;
class IconPicker;
class QLabel;
class PatternPicker;
class QAbstractButton;
class QHBoxLayout;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

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
    struct ExtraFieldEditor {
        QWidget* row = nullptr;
        QLineEdit* labelEdit = nullptr;
        QLineEdit* valueEdit = nullptr;
        QCheckBox* secretToggle = nullptr;
        QPushButton* removeButton = nullptr;
        QString fixedLabel;
        bool fixedSecret = false;
        bool custom = false;
    };

    QWidget* makeField(const QString& label, QWidget* editor, bool required = false,
                       QLabel** captionOut = nullptr);
    void setPreset(nightlock::EntryPreset preset, bool resetValues);
    void addPresetFields(nightlock::EntryPreset preset);
    void addCryptoFields(const QString& assetId);
    void setCryptoAsset(const QString& assetId);
    void refreshSaveAvailability();
    void addExtraField(const QString& label, bool secret, bool custom,
                       const QString& value = {});
    void clearExtraFields();
    void removeExtraField(QWidget* row);

    Mode mode_;
    bool presetsEnabled_ = false;
    bool allowCustomFields_ = false;
    nightlock::EntryPreset preset_ = nightlock::EntryPreset::Classic;
    QAbstractButton* presetButton_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    IconPicker* iconPicker_;
    PatternPicker* patternPicker_;
    EntryColorPicker* colorPicker_;
    QWidget* colorField_;
    QLineEdit* nameEdit_;
    QLineEdit* loginEdit_;
    QLineEdit* passwordEdit_;
    QLineEdit* urlEdit_;
    QWidget* credentialsField_;
    QWidget* loginField_;
    QWidget* passwordField_;
    QWidget* urlField_;
    QWidget* noteField_;
    QLabel* loginCaption_;
    QLabel* passwordCaption_;
    QLabel* urlCaption_;
    QWidget* extraFieldsContainer_;
    QVBoxLayout* extraFieldsLayout_;
    QPushButton* addFieldButton_ = nullptr;
    std::vector<ExtraFieldEditor> extraFields_;
    std::vector<QWidget*> extraLayoutRows_;
    QHBoxLayout* pendingPresetRow_ = nullptr;
    QPlainTextEdit* noteEdit_;
    QPushButton* saveButton_;
};
