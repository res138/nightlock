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
    void setLeadingIconVisible(bool visible);
    void setContentAlignment(Qt::Alignment alignment);

    // Also used by the NIGHTLOCK_TEST_SPOILER hook.
    void copyAndFlash();

    QSize sizeHint() const override;
    int naturalTextWidth() const;

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
    qreal copied_ = 0;  // 0..1 crossfade to the "Copied" flash
};
