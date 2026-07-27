#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;

// Full-window lock screen: the vault hides behind a centered password
// prompt (lock art, title, password field with an arrow button). A
// rejected password shakes the field and shows an error line; the link
// at the bottom leads to the project's security notes.
//
// The screen never verifies anything itself — it emits
// passwordSubmitted() and the owner answers with rejectPassword() or
// hides it. Create mode adds a confirm field for the first-run flow.
class LockScreen : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Unlock, Create };

    explicit LockScreen(QWidget* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    // Clears the fields and error state and focuses the input.
    void reset();

    // Error state + shake, e.g. after a failed VaultFile::open. An
    // empty message shows the default "Invalid password" line.
    void rejectPassword(const QString& message = {});

    // Debug hook for NIGHTLOCK_SCREENSHOT_LOCK: submits a wrong
    // password to capture the error state.
    void debugFail();

signals:
    void passwordSubmitted(const QString& password);

private:
    void submit();
    void setError(bool on, const QString& message = {});
    void shake();

    Mode mode_ = Mode::Unlock;
    QLabel* title_;
    QWidget* row_;  // field + arrow button; shaken as one piece
    QLineEdit* field_;
    QWidget* confirmHolder_;  // Create mode only
    QLineEdit* confirm_;
    QLabel* error_;
};
