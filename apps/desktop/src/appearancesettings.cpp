#include "appearancesettings.hpp"

#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QSettings>
#include <QStyleHints>

#include "fonts.hpp"

namespace appearancesettings {
namespace {

QString read(const char* key, const char* fallback) {
    return QSettings()
        .value(QStringLiteral("appearance/") + QLatin1String(key), QLatin1String(fallback))
        .toString();
}

void write(const char* key, const QVariant& value) {
    QSettings().setValue(QStringLiteral("appearance/") + QLatin1String(key), value);
}

// One row per @token in style.qss, light and dark side by side. The
// table is ordered so that longer tokens sharing a prefix come first
// (plain string replacement would otherwise eat the prefix).
struct ThemeToken {
    const char* token;
    const char* light;
    const char* dark;
};
// The dark side is true black: pure #000000 surfaces with near-black
// elevations, not the usual washed-out gray.
constexpr ThemeToken kThemeTokens[] = {
    {"@bg-sidebar", "#FBFAFB", "#0A0A0A"},
    {"@bg-card", "#FDFCFD", "#0E0E0E"},
    {"@bg-input-focus", "#FFFFFF", "#1A1A1A"},
    {"@bg-input", "#FBF9FB", "#141414"},
    {"@bg-elevated", "#FFFFFF", "#141414"},
    {"@bg", "#FFFFFF", "#000000"},
    {"@border-strong", "#E6E4E8", "#303030"},
    {"@border-soft", "#EFEFEF", "#242424"},
    {"@border-faint", "#F0F0F0", "#1F1F1F"},
    {"@border-row", "#F5F5F5", "#1C1C1C"},
    {"@border-focus", "#D9D7DC", "#3A3A3A"},
    {"@border", "#EAEAEA", "#2A2A2A"},
    {"@text-strong", "#000000", "#F5F5F5"},
    {"@text-value", "#3C3C3C", "#ECECEC"},
    {"@text-note", "#4A4A4A", "#D6D6D6"},
    {"@text-muted", "#6E6E6E", "#B3B3B3"},
    {"@text-faint", "#8A8792", "#9B9B9B"},
    {"@text", "#111111", "#ECECEC"},
    {"@hover-strong", "#EFEDF1", "#222222"},
    {"@hover-tint", "#F5F3F6", "#1A1A1A"},
    {"@hover", "#F5F5F5", "#1A1A1A"},
    {"@pressed", "#EDEBEF", "#222222"},
    {"@scroll-hover", "#A9A9A9", "#4E4E4E"},
    {"@scroll", "#CFCFCF", "#3A3A3A"},
    {"@disabled", "#BDBDBD", "#3A3A3A"},
    {"@overlay-hover", "rgba(0, 0, 0, 0.07)", "rgba(255, 255, 255, 0.09)"},
    {"@overlay-active", "rgba(0, 0, 0, 0.08)", "rgba(255, 255, 255, 0.11)"},
};

// Menu icon that resolves its tint at paint time: Selected renders
// white (for accent-colored rows), the dark theme lightens the glyph,
// light theme paints the SVG as authored.
class ThemedIconEngine : public QIconEngine {
public:
    explicit ThemedIconEngine(QString path, QColor fixedColor = {})
        : path_(std::move(path)), fixedColor_(std::move(fixedColor)) {}

    QPixmap pixmap(const QSize& size, QIcon::Mode mode,
                   QIcon::State state) override {
        return render(size, 1.0, mode, state);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode,
                         QIcon::State state, qreal scale) override {
        return render(size, scale, mode, state);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State state) override {
        const qreal ratio = painter->device()->devicePixelRatioF();
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, ratio));
    }

    QIconEngine* clone() const override {
        return new ThemedIconEngine(path_, fixedColor_);
    }

private:
    QPixmap render(const QSize& size, qreal scale, QIcon::Mode mode,
                   QIcon::State state) const {
        // QIcon's DPR overload asks the SVG engine for physical pixels and
        // tags the result with the requested ratio. This matters on Windows
        // at 125/150/175% as well as on integer Retina scaling.
        QPixmap base = QIcon(path_).pixmap(size, scale, mode, state);
        if (base.isNull())
            return base;
        const bool onAccent = mode == QIcon::Selected;
        if (!fixedColor_.isValid() && !onAccent && !darkActive())
            return base;
        QPainter painter(&base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        const QColor tint = fixedColor_.isValid()
                                ? fixedColor_
                                : onAccent ? accentTextColor() : palette().ink;
        painter.fillRect(base.rect(), tint);
        return base;
    }

    QString path_;
    QColor fixedColor_;
};

class ColorSwatchIconEngine : public QIconEngine {
public:
    explicit ColorSwatchIconEngine(QColor color) : color_(std::move(color)) {}

    QPixmap pixmap(const QSize& size, QIcon::Mode mode,
                   QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode,
                         QIcon::State, qreal scale) override {
        const QSize physical(qCeil(size.width() * scale),
                             qCeil(size.height() * scale));
        QPixmap result(physical);
        result.fill(Qt::transparent);
        result.setDevicePixelRatio(scale);
        QPainter painter(&result);
        draw(&painter, QRect(QPoint(), size), mode);
        return result;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State) override {
        draw(painter, rect, mode);
    }

    QIconEngine* clone() const override {
        return new ColorSwatchIconEngine(color_);
    }

private:
    void draw(QPainter* painter, const QRect& rect, QIcon::Mode mode) const {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        QColor color = color_;
        if (mode == QIcon::Disabled)
            color.setAlphaF(color.alphaF() * 0.45);
        painter->setBrush(color);
        const qreal inset =
            qMax<qreal>(1.5, qMin(rect.width(), rect.height()) / 6.0);
        painter->drawEllipse(
            QRectF(rect).adjusted(inset, inset, -inset, -inset));
        painter->restore();
    }

    QColor color_;
};

}  // namespace

QString theme() {
    const QString value = read("theme", "light");
    for (const char* known : kThemes)
        if (value == QLatin1String(known))
            return value;
    return QStringLiteral("light");
}

void setTheme(const QString& theme) {
    write("theme", theme);
    applyStylesheet();
    notifier()->notify();
}

bool darkActive() {
    const QString value = theme();
    if (value == QLatin1String("dark"))
        return true;
    if (value == QLatin1String("system"))
        return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    return false;
}

QString accent() {
    const QString value = read("accent", "black");
    for (const char* known : kAccents)
        if (value == QLatin1String(known))
            return value;
    return QStringLiteral("black");
}

QColor accentColorFor(const QString& accent) {
    // Deep shades: accent paints selection surfaces under white text.
    if (accent == QLatin1String("blue"))
        return QColor(0x2F, 0x5F, 0xB3);
    if (accent == QLatin1String("green"))
        return QColor(0x2F, 0x7D, 0x53);
    // "Black" is really "ink": it flips to pure white on the dark
    // scheme, where a black selection would sink into the background.
    return darkActive() ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x00, 0x00, 0x00);
}

QColor accentColor() {
    return accentColorFor(accent());
}

QColor accentTextColor() {
    const QColor accent = accentColor();
    const qreal luma =
        0.299 * accent.redF() + 0.587 * accent.greenF() + 0.114 * accent.blueF();
    return luma > 0.6 ? QColor(0x00, 0x00, 0x00) : QColor(Qt::white);
}

void setAccent(const QString& accent) {
    write("accent", accent);
    applyStylesheet();
    notifier()->notify();
}

bool folderIcons() {
    return QSettings().value(QStringLiteral("appearance/folder-icons"), true).toBool();
}

void setFolderIcons(bool shown) {
    write("folder-icons", shown);
    notifier()->notify();
}

const Palette& palette() {
    static const Palette light = {
        QColor(0xFF, 0xFF, 0xFF),  // window
        QColor(0xFB, 0xF9, 0xFB),  // canvas
        QColor(0xFD, 0xFC, 0xFD),  // card
        QColor(0xFB, 0xF9, 0xFB),  // input
        QColor(0xF5, 0xF3, 0xF6),  // inputHover
        QColor(0xEA, 0xEA, 0xEA),  // border
        QColor(0xE6, 0xE4, 0xE8),  // borderStrong
        QColor(0xF0, 0xF0, 0xF0),  // separator
        QColor(0x11, 0x11, 0x11),  // ink
        QColor(0x3C, 0x3C, 0x3C),  // value
        QColor(0x6E, 0x6E, 0x6E),  // muted
        QColor(0x8A, 0x87, 0x92),  // faint
        QColor(0xCF, 0xCF, 0xCF),  // scroll
        QColor(0xA9, 0xA9, 0xA9),  // scrollHover
        QColor(0xD9, 0xD7, 0xDC),  // toggleOff
        QColor(0xFF, 0xFF, 0xFF),  // veil
    };
    // True black, matching the stylesheet's dark tokens.
    static const Palette dark = {
        QColor(0x00, 0x00, 0x00),  // window
        QColor(0x00, 0x00, 0x00),  // canvas
        QColor(0x0E, 0x0E, 0x0E),  // card
        QColor(0x14, 0x14, 0x14),  // input
        QColor(0x1A, 0x1A, 0x1A),  // inputHover
        QColor(0x2A, 0x2A, 0x2A),  // border
        QColor(0x30, 0x30, 0x30),  // borderStrong
        QColor(0x1F, 0x1F, 0x1F),  // separator
        QColor(0xEC, 0xEC, 0xEC),  // ink
        QColor(0xEC, 0xEC, 0xEC),  // value
        QColor(0xB3, 0xB3, 0xB3),  // muted
        QColor(0x9B, 0x9B, 0x9B),  // faint
        QColor(0x3A, 0x3A, 0x3A),  // scroll
        QColor(0x4E, 0x4E, 0x4E),  // scrollHover
        QColor(0x3A, 0x3A, 0x3A),  // toggleOff
        QColor(0x14, 0x14, 0x14),  // veil
    };
    return darkActive() ? dark : light;
}

QIcon themedMenuIcon(const QString& name) {
    return QIcon(new ThemedIconEngine(QStringLiteral(":/icons/menu/%1.svg").arg(name)));
}

QIcon tintedMenuIcon(const QString& name, const QColor& color) {
    return QIcon(new ThemedIconEngine(QStringLiteral(":/icons/menu/%1.svg").arg(name),
                                      color));
}

QIcon colorSwatchIcon(const QColor& color) {
    return QIcon(new ColorSwatchIconEngine(color));
}

void applyStylesheet() {
    // "System" follows the OS: hook the scheme signal once, so a
    // macOS light/dark flip restyles a running app.
    static bool watching = false;
    if (!watching) {
        watching = true;
        QObject::connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                         notifier(), [] {
                             if (theme() == QLatin1String("system")) {
                                 applyStylesheet();
                                 notifier()->notify();
                             }
                         });
    }

    QFile qss(QStringLiteral(":/style.qss"));
    if (!qss.open(QIODevice::ReadOnly))
        return;
    QString sheet = QString::fromUtf8(qss.readAll());
    const bool dark = darkActive();
    for (const ThemeToken& token : kThemeTokens)
        sheet.replace(QLatin1String(token.token), QLatin1String(dark ? token.dark : token.light));
    const QColor accent = accentColor();
    // Black's hover keeps the classic lift to #262626; colored accents
    // darken instead. Longer tokens first — they contain the shorter.
    const QColor hover =
        accent == QColor(Qt::black) ? QColor(0x26, 0x26, 0x26) : accent.darker(115);
    sheet.replace(QStringLiteral("@accent-text"), accentTextColor().name());
    sheet.replace(QStringLiteral("@accent-hover"), hover.name());
    sheet.replace(QStringLiteral("@accent"), accent.name());
    sheet.replace(QStringLiteral("@font-secondary"),
                  fonts::resolvedFamily(fonts::Role::Secondary));
    qApp->setStyleSheet(sheet);
}

Notifier* notifier() {
    static Notifier instance;
    return &instance;
}

}  // namespace appearancesettings
