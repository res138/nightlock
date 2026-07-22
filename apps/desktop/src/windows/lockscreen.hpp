#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;

// Full-window lock screen: the vault hides behind a centered password
// prompt (lock art, title, password field with an arrow button). A
// wrong guess shakes the field and shows an error line; the link at
// the bottom leads to the project's security notes.
class LockScreen : public QWidget {
    Q_OBJECT
public:
    explicit LockScreen(QWidget* parent = nullptr);

    // Clears the field and error state and focuses the input.
    void reset();

    // Debug hook for NIGHTLOCK_SCREENSHOT_LOCK: submits a wrong
    // password to capture the error state.
    void debugFail();

signals:
    void unlocked();

private:
    void tryUnlock();
    void setError(bool on);
    void shake();

    QWidget* row_;  // field + arrow button; shaken as one piece
    QLineEdit* field_;
    QLabel* error_;
};
