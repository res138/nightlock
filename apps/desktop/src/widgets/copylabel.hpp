#pragma once

#include <QWidget>

class QTimer;
class QVariantAnimation;

// Click-to-copy value label (used for the login row): the text is
// always visible; clicking copies it to the clipboard and briefly
// crossfades to a copy icon + "Copied" flash, like the password
// spoiler does.
class CopyLabel : public QWidget {
    Q_OBJECT
public:
    explicit CopyLabel(QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return text_; }
    void setClipboardText(const QString& text);
    // Drops both the displayed and copy-only values immediately.  Qt's
    // QString storage cannot promise cryptographic zeroization, but the
    // widget must not retain a logical copy after the vault is locked.
    void clear();
    void setLeadingIconVisible(bool visible);
    void setContentAlignment(Qt::Alignment alignment);
    // Optional clipping policy for constrained table cells. The detail
    // view keeps the default (no elision).
    void setTextElideMode(Qt::TextElideMode mode);

    // Also used by the NIGHTLOCK_TEST_SPOILER hook.
    void copyAndFlash();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString text_;
    QString clipboardText_;
    QTimer* hold_;
    QVariantAnimation* flash_;
    bool leadingIconVisible_ = false;
    Qt::Alignment contentAlignment_ = Qt::AlignRight;
    Qt::TextElideMode elideMode_ = Qt::ElideNone;
    qreal copied_ = 0;  // 0..1 crossfade to the "Copied" flash
};
