#include "patternbackdrop.hpp"

#include <QIcon>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <functional>

namespace {

constexpr qreal kGlowSoftAlpha = 0.17;   // blob center alpha, "barely there"
constexpr qreal kGlowBoldAlpha = 0.36;   // blob center alpha, "noticeable"
constexpr int kGlowBlobs = 5;
constexpr qreal kTileAlpha = 0.09;
constexpr qreal kTileMaxTilt = 0.35;     // radians of per-tile jitter
constexpr int kRippleRings = 9;
constexpr qreal kRippleAlpha = 0.18;     // first ring, then × kRippleDecay
constexpr qreal kRippleDecay = 0.82;
constexpr qreal kRippleWidth = 1.4;

// Deterministic splitmix64: the same seed must reproduce the same
// geometry on every run (std::rand would depend on global state).
struct SeededRng {
    quint64 state;

    quint64 next() {
        state += 0x9E3779B97F4A7C15ULL;
        quint64 z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    qreal real(qreal lo, qreal hi) {
        return lo + (hi - lo) * qreal(next() >> 11) / qreal(1ULL << 53);
    }

    int range(int lo, int hi) {  // inclusive bounds
        return lo + int(next() % quint64(hi - lo + 1));
    }
};

// Up to four accent colors pulled out of the icon: shrink to 8×8, drop
// transparent and almost-white pixels, order by saturation and dedup by
// RGB distance. A grayscale icon yields gray accents — that is the
// point: the pattern always matches its icon.
QVector<QColor> extractPalette(const QImage& icon) {
    QVector<QColor> candidates;
    if (!icon.isNull()) {
        const QImage tiny = icon.convertToFormat(QImage::Format_ARGB32)
                                .scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < tiny.height(); ++y) {
            for (int x = 0; x < tiny.width(); ++x) {
                const QColor c = tiny.pixelColor(x, y);
                if (c.alpha() < 200)
                    continue;  // icon edges blend into the backdrop
                if (c.value() > 230 && c.saturation() < 25)
                    continue;  // almost-white would vanish on the panel
                candidates.append(c);
            }
        }
    }

    // Most saturated first; the remaining keys only pin a stable order.
    std::sort(candidates.begin(), candidates.end(), [](const QColor& a, const QColor& b) {
        if (a.saturation() != b.saturation())
            return a.saturation() > b.saturation();
        if (a.value() != b.value())
            return a.value() > b.value();
        return a.rgb() < b.rgb();
    });

    const auto distance = [](const QColor& a, const QColor& b) {
        const int dr = a.red() - b.red();
        const int dg = a.green() - b.green();
        const int db = a.blue() - b.blue();
        return dr * dr + dg * dg + db * db;
    };

    QVector<QColor> palette;
    for (const QColor& c : candidates) {
        const bool dup = std::any_of(palette.begin(), palette.end(), [&](const QColor& kept) {
            return distance(c, kept) < 64 * 64;
        });
        if (!dup)
            palette.append(c);
        if (palette.size() == 4)
            break;
    }

    if (palette.isEmpty())
        palette.append(QColor(0x8A, 0x8A, 0x8E));  // all-white icon: neutral gray
    return palette;
}

void paintGlow(QPainter& painter, const QSizeF& size, const QVector<QColor>& palette,
               SeededRng& rng, qreal centerAlpha) {
    for (int i = 0; i < kGlowBlobs; ++i) {
        QColor color = palette[i % palette.size()];
        const QPointF center(rng.real(0.10, 0.90) * size.width(),
                             rng.real(0.08, 0.85) * size.height());
        const qreal radius = rng.real(0.18, 0.38) * size.width();
        QRadialGradient blob(center, radius);
        color.setAlphaF(centerAlpha);
        blob.setColorAt(0.0, color);
        color.setAlphaF(0.0);
        blob.setColorAt(1.0, color);
        painter.fillRect(QRectF(QPointF(0, 0), size), blob);
    }
}

void paintIconTile(QPainter& painter, const QSizeF& size, const QPixmap& icon,
                   SeededRng& rng) {
    const qreal tile = rng.real(26, 34);
    const qreal stepX = tile * rng.real(1.8, 2.4);
    const qreal stepY = tile * rng.real(1.6, 2.1);
    const qreal phaseX = rng.real(0, stepX);
    const qreal phaseY = rng.real(0, stepY);

    painter.setOpacity(kTileAlpha);
    int row = 0;
    for (qreal y = -phaseY; y < size.height() + tile; y += stepY, ++row) {
        // Every second row slides half a step, like brickwork.
        const qreal rowShift = (row % 2) ? stepX / 2 : 0;
        for (qreal x = -phaseX + rowShift; x < size.width() + tile; x += stepX) {
            painter.save();
            painter.translate(x, y);
            painter.rotate(qRadiansToDegrees(rng.real(-kTileMaxTilt, kTileMaxTilt)));
            painter.drawPixmap(QRectF(-tile / 2, -tile / 2, tile, tile), icon,
                               icon.rect());
            painter.restore();
        }
    }
    painter.setOpacity(1.0);
}

void paintRipple(QPainter& painter, const QSizeF& size, const QVector<QColor>& palette,
                 SeededRng& rng, qreal centerY) {
    const QPointF center(size.width() / 2, centerY);
    qreal radius = rng.real(26, 34);
    const qreal spacing = rng.real(16, 22);
    qreal alpha = kRippleAlpha;

    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i < kRippleRings; ++i) {
        // The two dominant colors of the icon take turns.
        QColor color = palette[i % qMin<qsizetype>(2, palette.size())];
        color.setAlphaF(alpha);
        painter.setPen(QPen(color, kRippleWidth));
        painter.drawEllipse(center, radius, radius);
        radius += spacing;
        alpha *= kRippleDecay;
    }
}

}  // namespace

PatternBackdrop::PatternBackdrop(QWidget* parent) : QWidget(parent) {
    // Purely decorative: clicks fall through to whatever is above.
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PatternBackdrop::setEntry(const nightlock::Entry* entry) {
    const auto kind = entry ? entry->pattern : nightlock::Pattern::None;
    QString iconPath;
    quint64 seed = 0;
    if (entry && kind != nightlock::Pattern::None) {
        iconPath = entry->icon.empty() ? QStringLiteral(":/icons/entry.png")
                                       : QString::fromStdString(entry->icon);
        seed = std::hash<long long>{}(
            entry->created.time_since_epoch().count());
    }
    if (kind == kind_ && iconPath == iconPath_ && seed == seed_)
        return;
    kind_ = kind;
    iconPath_ = iconPath;
    seed_ = seed;
    update();
}

void PatternBackdrop::setIconCenterY(int y) {
    if (iconCenterY_ == y)
        return;
    iconCenterY_ = y;
    cache_.clear();  // the ripple center moved
    update();
}

void PatternBackdrop::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    cache_.clear();  // patterns are laid out for the exact zone size
}

void PatternBackdrop::paintEvent(QPaintEvent*) {
    if (kind_ == nightlock::Pattern::None || width() <= 0 || height() <= 0)
        return;
    const quint64 key =
        seed_ ^ (quint64(kind_) * 0x9E3779B97F4A7C15ULL) ^ qHash(iconPath_);
    auto it = cache_.constFind(key);
    if (it == cache_.constEnd())
        it = cache_.insert(key, generate());
    QPainter(this).drawPixmap(0, 0, it.value());
}

QPixmap PatternBackdrop::generate() const {
    const qreal dpr = devicePixelRatioF();
    QImage image(qCeil(width() * dpr), qCeil(height() * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    const QIcon icon(iconPath_);
    // 64px is plenty for an 8×8 palette probe and matches how the pack
    // .ico files resolve their best sub-image.
    const QVector<QColor> palette = extractPalette(icon.pixmap(64, 64).toImage());

    SeededRng rng{seed_};
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QSizeF zone(width(), height());
    switch (kind_) {
    case nightlock::Pattern::GlowSoft:
        paintGlow(painter, zone, palette, rng, kGlowSoftAlpha);
        break;
    case nightlock::Pattern::GlowBold:
        paintGlow(painter, zone, palette, rng, kGlowBoldAlpha);
        break;
    case nightlock::Pattern::IconTile:
        paintIconTile(painter, zone, icon.pixmap(QSize(48, 48), dpr), rng);
        break;
    case nightlock::Pattern::Ripple:
        paintRipple(painter, zone, palette, rng, iconCenterY_);
        break;
    case nightlock::Pattern::None:
        break;
    }

    // Dissolve every edge: keep alpha inside an ellipse centered a bit
    // above the middle of the zone, fade to nothing at its rim.
    QRadialGradient mask(QPointF(0.5, 0.42), 0.55);
    mask.setCoordinateMode(QGradient::ObjectMode);
    mask.setColorAt(0.0, QColor(255, 255, 255, 255));
    mask.setColorAt(0.6, QColor(255, 255, 255, 220));
    mask.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.fillRect(QRectF(QPointF(0, 0), zone), mask);
    painter.end();

    return QPixmap::fromImage(image);
}
