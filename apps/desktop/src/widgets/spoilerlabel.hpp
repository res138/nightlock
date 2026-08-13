#pragma once

#include <QVector>
#include <QWidget>

class QTimer;
class QVariantAnimation;

// Telegram-style spoiler for the password value: a cloud of drifting
// particles (each fading in and out on its own) hides the secret.
// Click reveals it; a second click copies it to the clipboard with a
// brief icon + "Copied" flash; moving the cursor away hides it again.
class SpoilerLabel : public QWidget {
    Q_OBJECT
public:
    explicit SpoilerLabel(QWidget* parent = nullptr);

    // Sets the hidden text and resets to the spoilered state.
    void setSecret(const QString& secret);
    // Stops all secret-related animation and drops the retained QString.
    // This is a best-effort logical clear; QString itself is not secure
    // storage and cannot guarantee zeroization of shared buffers.
    void clear();

    // State drivers (also used by the NIGHTLOCK_TEST_SPOILER hook).
    void reveal();
    void copyAndFlash();
    void conceal();
    void setCoordinatedReveal(bool enabled);
    void setContentAlignment(Qt::Alignment alignment);
    // Uses the primary interface color for both revealed text and the
    // concealed particle cloud. Compact tables opt in for one tone.
    void setPrimaryTextColor(bool enabled);
    // Optional clipping policy for constrained table cells. The detail
    // view keeps the default (no elision).
    void setTextElideMode(Qt::TextElideMode mode);

    QSize sizeHint() const override;

signals:
    void revealRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Particle {
        QPointF pos;
        QPointF velocity;  // px/s
        qreal radius = 1;
        qreal age = 0;      // s
        qreal lifetime = 1; // s
    };

    void animateReveal(qreal target);
    void rebuildParticles();
    void respawn(Particle& particle, bool randomAge);
    void tick();
    void ensureTicking();

    QString secret_;
    QVector<Particle> particles_;
    QTimer* timer_;
    QTimer* copiedHold_;
    QVariantAnimation* revealAnimation_ = nullptr;
    QVariantAnimation* copiedAnimation_ = nullptr;
    bool coordinatedReveal_ = false;
    bool primaryTextColor_ = false;
    Qt::Alignment contentAlignment_ = Qt::AlignRight;
    Qt::TextElideMode elideMode_ = Qt::ElideNone;
    qreal reveal_ = 0;  // 0 = particle cloud, 1 = secret shown
    qreal copied_ = 0;  // 0..1 crossfade to the "Copied" flash
};
