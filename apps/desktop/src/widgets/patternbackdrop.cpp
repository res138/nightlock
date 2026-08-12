#include "patternbackdrop.hpp"

#include <QIcon>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr qreal kGlowSoftAlpha = 0.17;   // blob center alpha, "barely there"
constexpr qreal kGlowBoldAlpha = 0.36;   // blob center alpha, "noticeable"
constexpr int kGlowBlobs = 5;
constexpr qreal kTileAlpha = 0.09;
constexpr qreal kTileAlphaV2 = kTileAlpha * 1.25;  // v2 reads a notch stronger
constexpr qreal kTileMaxTilt = 0.35;     // radians of per-tile jitter (v1)
constexpr qreal kTileScaleMin = 0.70;    // per-tile size range of v3
constexpr qreal kTileScaleMax = 1.4;
constexpr int kRippleRings = 9;
constexpr qreal kRippleAlpha = 0.18;     // first ring, then × kRippleDecay
constexpr qreal kRippleDecay = 0.82;
constexpr qreal kRippleWidth = 1.4;
constexpr int kStars = 16;
constexpr qreal kLinkAlpha = 0.13;       // constellation edges
constexpr qreal kStarAlpha = 0.55;
constexpr int kGlowStars = 3;            // bigger soft "stars"
constexpr int kRibbons = 3;
constexpr qreal kRibbonAlpha = 0.13;
constexpr qreal kHaloDotAlpha = 0.38;
constexpr int kHaloStep = 13;            // dot grid pitch

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

// The three tile versions share one brickwork walk: v1 tilts every
// tile by ±maxTilt, v2 keeps them straight, v3 keeps them straight but
// scales each one somewhere in [scaleMin, scaleMax] of the base size.
void paintIconTile(QPainter& painter, const QSizeF& size, const QPixmap& icon,
                   SeededRng& rng, qreal maxTilt, qreal scaleMin, qreal scaleMax,
                   qreal alpha) {
    const qreal tile = rng.real(26, 34);
    const qreal stepX = tile * rng.real(1.8, 2.4);
    const qreal stepY = tile * rng.real(1.6, 2.1);
    const qreal phaseX = rng.real(0, stepX);
    const qreal phaseY = rng.real(0, stepY);

    painter.setOpacity(alpha);
    int row = 0;
    for (qreal y = -phaseY; y < size.height() + tile; y += stepY, ++row) {
        // Every second row slides half a step, like brickwork.
        const qreal rowShift = (row % 2) ? stepX / 2 : 0;
        for (qreal x = -phaseX + rowShift; x < size.width() + tile; x += stepX) {
            painter.save();
            painter.translate(x, y);
            if (maxTilt > 0)
                painter.rotate(qRadiansToDegrees(rng.real(-maxTilt, maxTilt)));
            const qreal drawn =
                scaleMin < scaleMax ? tile * rng.real(scaleMin, scaleMax) : tile;
            painter.drawPixmap(QRectF(-drawn / 2, -drawn / 2, drawn, drawn), icon,
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

// A tiny night sky: seeded stars, each linked to its two nearest
// neighbors with a hairline, plus a few larger soft glows. Some edges
// draw twice (A→B and B→A) — the doubled alpha reads as depth.
void paintConstellation(QPainter& painter, const QSizeF& size,
                        const QVector<QColor>& palette, SeededRng& rng) {
    QVector<QPointF> stars;
    stars.reserve(kStars);
    for (int i = 0; i < kStars; ++i)
        stars.append(QPointF(rng.real(0.06, 0.94) * size.width(),
                             rng.real(0.05, 0.95) * size.height()));

    QColor link = *std::min_element(
        palette.begin(), palette.end(),
        [](const QColor& a, const QColor& b) { return a.value() < b.value(); });
    link.setAlphaF(kLinkAlpha);
    painter.setPen(QPen(link, 1.0));
    for (const QPointF& star : stars) {
        QVector<QPointF> others = stars;
        others.removeOne(star);
        std::partial_sort(others.begin(), others.begin() + 2, others.end(),
                          [&](const QPointF& a, const QPointF& b) {
                              return QLineF(star, a).length() < QLineF(star, b).length();
                          });
        painter.drawLine(star, others[0]);
        painter.drawLine(star, others[1]);
    }

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < stars.size(); ++i) {
        QColor c = palette[i % palette.size()];
        c.setAlphaF(kStarAlpha);
        painter.setBrush(c);
        const qreal radius = rng.real(1.2, 2.6);
        painter.drawEllipse(stars[i], radius, radius);
    }

    for (int i = 0; i < kGlowStars; ++i) {
        const QPointF center(rng.real(0.15, 0.85) * size.width(),
                             rng.real(0.10, 0.80) * size.height());
        const qreal radius = rng.real(10, 20);
        QRadialGradient glow(center, radius);
        QColor c = palette[i % qMin<qsizetype>(2, palette.size())];
        c.setAlphaF(0.22);
        glow.setColorAt(0.0, c);
        c.setAlphaF(0.0);
        glow.setColorAt(1.0, c);
        painter.setBrush(glow);
        painter.drawEllipse(center, radius, radius);
    }
}

// Wide translucent bezier ribbons flowing left to right, each stroked
// with a gradient between two neighboring palette colors.
void paintAurora(QPainter& painter, const QSizeF& size,
                 const QVector<QColor>& palette, SeededRng& rng) {
    painter.setBrush(Qt::NoBrush);
    for (int k = 0; k < kRibbons; ++k) {
        const qreal y0 = rng.real(0.15, 0.80) * size.height();
        const qreal y1 = y0 + rng.real(-0.20, 0.20) * size.height();
        const qreal y2 = y0 + rng.real(-0.20, 0.20) * size.height();
        const qreal y3 = y0 + rng.real(-0.25, 0.25) * size.height();

        QLinearGradient silk(QPointF(0, 0), QPointF(size.width(), 0));
        QColor c = palette[k % palette.size()];
        c.setAlphaF(kRibbonAlpha);
        silk.setColorAt(0.0, c);
        c = palette[(k + 1) % palette.size()];
        c.setAlphaF(kRibbonAlpha);
        silk.setColorAt(1.0, c);

        QPen pen(QBrush(silk), rng.real(16, 30));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        // Ends start off-canvas so the caps never show inside the zone.
        QPainterPath ribbon(QPointF(-24, y0));
        ribbon.cubicTo(QPointF(size.width() * 0.30, y1),
                       QPointF(size.width() * 0.62, y2),
                       QPointF(size.width() + 24, y3));
        painter.drawPath(ribbon);
    }
}

// Halftone dot grid whose dot size follows a gaussian of the distance
// to the icon center — the dots swell on a ring around the icon and
// vanish elsewhere. The two dominant colors alternate checkerboard.
void paintHalo(QPainter& painter, const QSizeF& size, const QVector<QColor>& palette,
               SeededRng& rng, qreal centerY) {
    const qreal ring = rng.real(58, 76);   // radius the dots peak at
    const qreal soft = rng.real(28, 38);   // gaussian width of the swell
    const QPointF center(size.width() / 2, centerY);

    painter.setPen(Qt::NoPen);
    int row = 0;
    for (qreal y = 6; y < size.height(); y += kHaloStep, ++row) {
        int col = 0;
        for (qreal x = 6; x < size.width(); x += kHaloStep, ++col) {
            const qreal d = QLineF(QPointF(x, y), center).length();
            const qreal radius = 3.1 * std::exp(-(d - ring) * (d - ring) / (2 * soft * soft));
            if (radius < 0.55)
                continue;
            QColor c = palette[(row + col) % qMin<qsizetype>(2, palette.size())];
            c.setAlphaF(kHaloDotAlpha);
            painter.setBrush(c);
            painter.drawEllipse(QPointF(x, y), radius, radius);
        }
    }
}

}  // namespace

PatternBackdrop::PatternBackdrop(QWidget* parent) : QWidget(parent) {
    // Purely decorative: clicks fall through to whatever is above.
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void PatternBackdrop::setEntry(const nightlock::Entry* entry) {
    if (!entry) {
        clear();
        return;
    }
    const auto kind = entry->pattern;
    QString iconPath;
    quint64 seed = 0;
    if (kind != nightlock::Pattern::None) {
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

void PatternBackdrop::clear() {
    kind_ = nightlock::Pattern::None;
    if (!iconPath_.isEmpty())
        iconPath_.fill(QChar(u'\0'));
    iconPath_.clear();
    iconPath_.squeeze();
    seed_ = 0;
    cache_.clear();
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
        paintIconTile(painter, zone, icon.pixmap(QSize(48, 48), dpr), rng,
                      kTileMaxTilt, 1.0, 1.0, kTileAlpha);
        break;
    case nightlock::Pattern::IconTileV2:
        paintIconTile(painter, zone, icon.pixmap(QSize(48, 48), dpr), rng,
                      0.0, 1.0, 1.0, kTileAlphaV2);
        break;
    case nightlock::Pattern::IconTileV3:
        paintIconTile(painter, zone, icon.pixmap(QSize(48, 48), dpr), rng,
                      0.0, kTileScaleMin, kTileScaleMax, kTileAlpha);
        break;
    case nightlock::Pattern::Ripple:
        paintRipple(painter, zone, palette, rng, iconCenterY_);
        break;
    case nightlock::Pattern::Constellation:
        paintConstellation(painter, zone, palette, rng);
        break;
    case nightlock::Pattern::Aurora:
        paintAurora(painter, zone, palette, rng);
        break;
    case nightlock::Pattern::Halo:
        paintHalo(painter, zone, palette, rng, iconCenterY_);
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
