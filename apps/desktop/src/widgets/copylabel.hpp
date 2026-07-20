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

    // Also used by the NIGHTLOCK_TEST_SPOILER hook.
    void copyAndFlash();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString text_;
    QTimer* hold_;
    QVariantAnimation* flash_;
    qreal copied_ = 0;  // 0..1 crossfade to the "Copied" flash
};
