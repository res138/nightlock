#include "spoilerlabel.hpp"

#include <QClipboard>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QTimer>
#include <QVariantAnimation>

#include "appearancesettings.hpp"

#include <cmath>

namespace {

// Tuned after Telegram's spoiler: ~8 particles per 100 px², slow
// drift, each particle living under two seconds with its own fade
// envelope.
constexpr qreal kDensity = 0.08;      // particles per px²
constexpr int kTickMs = 33;
constexpr qreal kMinSpeed = 3.0;      // px/s
constexpr qreal kMaxSpeed = 10.0;
constexpr qreal kMinLifetime = 0.7;   // s
constexpr qreal kMaxLifetime = 1.8;
constexpr qreal kMinRadius = 0.8;
constexpr qreal kMaxRadius = 1.4;
constexpr int kRevealMs = 180;
constexpr int kCopiedFlashMs = 160;
constexpr int kCopiedHoldMs = 900;
constexpr int kHeight = 16;

QColor textColor(bool primary) {
    return primary ? appearancesettings::palette().ink
                   : appearancesettings::palette().value;
}
QColor particleColor(bool primary) {
    if (primary)
        return appearancesettings::palette().ink;
    return appearancesettings::darkActive() ? QColor(0xC8, 0xC6, 0xCD)
                                            : QColor(0x2A, 0x2A, 0x2A);
}

void discardText(QString& text) {
    if (!text.isEmpty())
        text.fill(QChar(u'\0'));
    text.clear();
    text.squeeze();
}

qreal randomIn(qreal from, qreal to) {
    return from + QRandomGenerator::global()->generateDouble() * (to - from);
}

// Per-particle opacity over its life: quick fade-in, hold, fade-out.
qreal fadeEnvelope(qreal t) {
    if (t < 0.25)
        return t / 0.25;
    if (t > 0.7)
        return (1.0 - t) / 0.3;
    return 1.0;
}

}  // namespace

SpoilerLabel::SpoilerLabel(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    QFont f = font();
    f.setPixelSize(12);
    setFont(f);

    timer_ = new QTimer(this);
    timer_->setInterval(kTickMs);
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });

    // One persistent animation per property, retargeted on each
    // transition (DeleteWhenStopped would leave dangling pointers).
    revealAnimation_ = new QVariantAnimation(this);
    revealAnimation_->setDuration(kRevealMs);
    revealAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(revealAnimation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                reveal_ = value.toReal();
                ensureTicking();
                update();
            });

    copiedAnimation_ = new QVariantAnimation(this);
    copiedAnimation_->setDuration(kCopiedFlashMs);
    connect(copiedAnimation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                copied_ = value.toReal();
                update();
            });

    copiedHold_ = new QTimer(this);
    copiedHold_->setSingleShot(true);
    copiedHold_->setInterval(kCopiedHoldMs);
    connect(copiedHold_, &QTimer::timeout, this, [this] {
        copiedAnimation_->stop();
        copiedAnimation_->setStartValue(copied_);
        copiedAnimation_->setEndValue(0.0);
        copiedAnimation_->start();
    });
}

void SpoilerLabel::setSecret(const QString& secret) {
    clear();
    secret_ = secret;
    if (secret_.isEmpty())
        return;
    rebuildParticles();
    ensureTicking();
    update();
}

void SpoilerLabel::clear() {
    // Hard reset, no animation when switching entries or locking.
    timer_->stop();
    copiedHold_->stop();
    copiedAnimation_->stop();
    revealAnimation_->stop();
    copied_ = 0;
    reveal_ = 0;
    discardText(secret_);
    particles_.clear();
    updateGeometry();
    update();
}

QSize SpoilerLabel::sizeHint() const {
    const int text = QFontMetrics(font()).horizontalAdvance(secret_);
    return {qBound(70, text + 8, 240), kHeight};
}

void SpoilerLabel::reveal() {
    animateReveal(1.0);
}

void SpoilerLabel::copyAndFlash() {
    QGuiApplication::clipboard()->setText(secret_);
    copiedAnimation_->stop();
    copiedAnimation_->setStartValue(copied_);
    copiedAnimation_->setEndValue(1.0);
    copiedAnimation_->start();
    copiedHold_->start();
}

void SpoilerLabel::conceal() {
    copiedHold_->stop();
    copiedAnimation_->stop();
    copied_ = 0;
    animateReveal(0.0);
}

void SpoilerLabel::setCoordinatedReveal(bool enabled) {
    coordinatedReveal_ = enabled;
}

void SpoilerLabel::setContentAlignment(Qt::Alignment alignment) {
    if (contentAlignment_ == alignment)
        return;
    contentAlignment_ = alignment;
    update();
}

void SpoilerLabel::setPrimaryTextColor(bool enabled) {
    if (primaryTextColor_ == enabled)
        return;
    primaryTextColor_ = enabled;
    update();
}

void SpoilerLabel::setTextElideMode(Qt::TextElideMode mode) {
    if (elideMode_ == mode)
        return;
    elideMode_ = mode;
    update();
}

void SpoilerLabel::animateReveal(qreal target) {
    if (qFuzzyCompare(reveal_, target))
        return;
    revealAnimation_->stop();
    revealAnimation_->setStartValue(reveal_);
    revealAnimation_->setEndValue(target);
    revealAnimation_->start();
    ensureTicking();
}

void SpoilerLabel::rebuildParticles() {
    const int count = qMax(30, qRound(width() * height() * kDensity));
    particles_.resize(count);
    for (Particle& particle : particles_)
        respawn(particle, true);  // random ages: the cloud starts full
}

void SpoilerLabel::respawn(Particle& particle, bool randomAge) {
    particle.pos = QPointF(randomIn(0, width()), randomIn(1, height() - 1));
    const qreal angle = randomIn(0, 2 * M_PI);
    const qreal speed = randomIn(kMinSpeed, kMaxSpeed);
    particle.velocity = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
    particle.radius = randomIn(kMinRadius, kMaxRadius);
    particle.lifetime = randomIn(kMinLifetime, kMaxLifetime);
    particle.age = randomAge ? randomIn(0, particle.lifetime) : 0;
}

void SpoilerLabel::tick() {
    if (reveal_ >= 1.0 && revealAnimation_->state() != QAbstractAnimation::Running) {
        timer_->stop();  // fully revealed: nothing animates
        return;
    }
    const qreal dt = kTickMs / 1000.0;
    for (Particle& particle : particles_) {
        particle.age += dt;
        if (particle.age >= particle.lifetime) {
            respawn(particle, false);
            continue;
        }
        particle.pos += particle.velocity * dt;
        // Drifting off one edge re-enters on the opposite one.
        if (particle.pos.x() < 0)
            particle.pos.setX(width());
        else if (particle.pos.x() > width())
            particle.pos.setX(0);
        if (particle.pos.y() < 0)
            particle.pos.setY(height());
        else if (particle.pos.y() > height())
            particle.pos.setY(0);
    }
    update();
}

void SpoilerLabel::ensureTicking() {
    if (reveal_ < 1.0 && isVisible() && !timer_->isActive())
        timer_->start();
}

void SpoilerLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (reveal_ < 1.0) {
        painter.setPen(Qt::NoPen);
        for (const Particle& particle : particles_) {
            const qreal alpha =
                fadeEnvelope(particle.age / particle.lifetime) * (1.0 - reveal_);
            if (alpha <= 0.01)
                continue;
            QColor color = particleColor(primaryTextColor_);
            color.setAlphaF(alpha * 0.85);
            painter.setBrush(color);
            painter.drawEllipse(particle.pos, particle.radius, particle.radius);
        }
    }

    if (reveal_ > 0.0 && copied_ < 1.0) {
        painter.setOpacity(reveal_ * (1.0 - copied_));
        painter.setPen(textColor(primaryTextColor_));
        const QString visible = QFontMetrics(font()).elidedText(
            secret_, elideMode_, width());
        painter.drawText(rect(), contentAlignment_ | Qt::AlignVCenter |
                                     Qt::TextSingleLine,
                         visible);
    }

    if (copied_ > 0.0) {
        painter.setOpacity(copied_);
        const QString label = QStringLiteral("Copied");
        const QFontMetrics metrics(font());
        constexpr int kIconSize = 13;
        constexpr int kGap = 5;
        const int textWidth = metrics.horizontalAdvance(label);
        const qreal slide = (1.0 - copied_) * 4.0;  // gentle rise-in
        const int flashWidth = textWidth + kGap + kIconSize;
        const int x = contentAlignment_.testFlag(Qt::AlignLeft)
                          ? 0
                          : contentAlignment_.testFlag(Qt::AlignHCenter)
                                ? (width() - flashWidth) / 2
                                : width() - flashWidth;
        const QRectF iconRect(x, (height() - kIconSize) / 2.0 + slide, kIconSize, kIconSize);
        static const QIcon copyIcon = appearancesettings::themedMenuIcon(QStringLiteral("copy"));
        copyIcon.paint(&painter, iconRect.toRect());
        painter.setPen(textColor(primaryTextColor_));
        painter.drawText(QRectF(x + kIconSize + kGap, slide, textWidth, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
    }
}

void SpoilerLabel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    rebuildParticles();
}

void SpoilerLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    if (coordinatedReveal_) {
        emit revealRequested();
        return;
    }
    if (reveal_ < 0.5)
        reveal();
    else
        copyAndFlash();
}

void SpoilerLabel::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (!coordinatedReveal_)
        conceal();
}

void SpoilerLabel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer_->stop();
}

void SpoilerLabel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureTicking();
}
