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

    // State drivers (also used by the NIGHTLOCK_TEST_SPOILER hook).
    void reveal();
    void copyAndFlash();

    QSize sizeHint() const override;

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

    void conceal();
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
    qreal reveal_ = 0;  // 0 = particle cloud, 1 = secret shown
    qreal copied_ = 0;  // 0..1 crossfade to the "Copied" flash
};
