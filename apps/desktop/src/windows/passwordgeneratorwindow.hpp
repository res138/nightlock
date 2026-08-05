#pragma once

#include <QDialog>

class QCheckBox;
class CopyLabel;
class QLabel;
class QPushButton;
class QSlider;
class StrengthBar;

// Independent, non-modal password generator. It uses the OS-backed
// QRandomGenerator source and keeps no connection to the open vault.
class PasswordGeneratorWindow : public QDialog {
    Q_OBJECT
public:
    explicit PasswordGeneratorWindow(QWidget* parent = nullptr);

private:
    void generatePassword();
    void updateStrength();
    void updateAvailability();

    CopyLabel* password_;
    QPushButton* generateButton_;
    QLabel* strengthLabel_;
    StrengthBar* strengthBar_;
    QLabel* lengthValue_;
    QSlider* lengthSlider_;
    QCheckBox* uppercase_;
    QCheckBox* lowercase_;
    QCheckBox* numbers_;
    QCheckBox* symbols_;
    QCheckBox* extendedAscii_;
};
