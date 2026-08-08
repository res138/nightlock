#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

// Full-window lock screen: the vault hides behind a centered password
// prompt (lock art, title, password field with an arrow button). A
// rejected password shakes the field and shows an error line; the link
// at the bottom leads to the project's security notes.
//
// The screen never verifies anything itself — it emits
// passwordSubmitted() and the owner answers with rejectPassword() or
// hides it. Create mode adds a confirm field plus a location row: the
// vault target starts at the owner-provided default and "Select
// Folder" moves it (the file name stays); a bottom link lets the user
// point at an existing vault file instead. Unlock mode swaps that
// bottom link for "Forgot a password?" — it forgets the remembered
// vault and drops the user back on the first-run screen.
class LockScreen : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Unlock, Create };

    explicit LockScreen(QWidget* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    // The full file path a Create submission should write to.
    void setVaultTarget(const QString& path);
    QString vaultTarget() const { return vaultTarget_; }

    // Clears the fields and error state and focuses the input.
    void reset();

    // Touch ID is offered only for an existing vault that has opted
    // in. Busy state prevents duplicate macOS authentication sheets.
    void setTouchIdAvailable(bool available);
    void setTouchIdBusy(bool busy);
    void showTouchIdError(const QString& message);

    // Error state + shake, e.g. after a failed VaultFile::open. An
    // empty message shows the default "Invalid password" line.
    void rejectPassword(const QString& message = {});

    // Debug hook for NIGHTLOCK_SCREENSHOT_LOCK: submits a wrong
    // password to capture the error state.
    void debugFail();

signals:
    void passwordSubmitted(const QString& password);
    // Create mode's "open an existing vault" link; the owner shows the
    // file dialog and switches.
    void openExistingRequested();
    // Unlock mode's "Forgot a password?" link; the owner forgets the
    // vault and restarts the first-run flow.
    void forgotPasswordRequested();
    void touchIdRequested();

private:
    void submit();
    void selectFolder();
    void setError(bool on, const QString& message = {});
    void shake();

    Mode mode_ = Mode::Unlock;
    QString vaultTarget_;  // full file path shown in the location row
    QLabel* title_;
    QWidget* row_;  // field + arrow button; shaken as one piece
    QLineEdit* field_;
    QWidget* confirmHolder_;  // Create mode only
    QLineEdit* confirm_;
    QWidget* locationHolder_;  // Create mode only
    QLabel* locationLabel_;
    QLabel* error_;
    QPushButton* touchId_;
    QLabel* forgotPassword_;  // Unlock mode only
    QLabel* openExisting_;    // Create mode only
    bool touchIdAvailable_ = false;
};
