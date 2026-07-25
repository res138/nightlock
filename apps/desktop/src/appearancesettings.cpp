#include "appearancesettings.hpp"

#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QSettings>
#include <QStyleHints>

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
constexpr ThemeToken kThemeTokens[] = {
    {"@bg-sidebar", "#FBFAFB", "#202023"},
    {"@bg-card", "#FDFCFD", "#212125"},
    {"@bg-input-focus", "#FFFFFF", "#2C2C32"},
    {"@bg-input", "#FBF9FB", "#26262B"},
    {"@bg-elevated", "#FFFFFF", "#26262B"},
    {"@bg", "#FFFFFF", "#1B1B1E"},
    {"@border-strong", "#E6E4E8", "#414147"},
    {"@border-soft", "#EFEFEF", "#333339"},
    {"@border-faint", "#F0F0F0", "#2C2C31"},
    {"@border-row", "#F5F5F5", "#2A2A2F"},
    {"@border-focus", "#D9D7DC", "#4A4A52"},
    {"@border", "#EAEAEA", "#3A3A40"},
    {"@text-strong", "#000000", "#F2F0F5"},
    {"@text-value", "#3C3C3C", "#B8B6BD"},
    {"@text-note", "#4A4A4A", "#ABA9B0"},
    {"@text-muted", "#6E6E6E", "#8F8D94"},
    {"@text-faint", "#8A8792", "#7C7A82"},
    {"@text", "#111111", "#E8E6EB"},
    {"@hover-strong", "#EFEDF1", "#34343A"},
    {"@hover-tint", "#F5F3F6", "#2E2E34"},
    {"@hover", "#F5F5F5", "#2E2E34"},
    {"@pressed", "#EDEBEF", "#34343A"},
    {"@scroll-hover", "#A9A9A9", "#5A5A60"},
    {"@scroll", "#CFCFCF", "#46464C"},
    {"@disabled", "#BDBDBD", "#47474B"},
    {"@overlay-hover", "rgba(0, 0, 0, 0.07)", "rgba(255, 255, 255, 0.09)"},
    {"@overlay-active", "rgba(0, 0, 0, 0.08)", "rgba(255, 255, 255, 0.11)"},
};

// Menu icon that resolves its tint at paint time: Selected renders
// white (for accent-colored rows), the dark theme lightens the glyph,
// light theme paints the SVG as authored.
class ThemedIconEngine : public QIconEngine {
public:
    explicit ThemedIconEngine(QString path) : path_(std::move(path)) {}

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State) override {
        QPixmap base = QIcon(path_).pixmap(size);
        const bool onAccent = mode == QIcon::Selected;
        if (!onAccent && !darkActive())
            return base;
        QPainter painter(&base);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(base.rect(), onAccent ? accentTextColor() : palette().ink);
        return base;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State state) override {
        const qreal ratio = painter->device()->devicePixelRatio();
        QPixmap drawn = pixmap(rect.size() * ratio, mode, state);
        drawn.setDevicePixelRatio(ratio);
        painter->drawPixmap(rect, drawn);
    }

    QIconEngine* clone() const override { return new ThemedIconEngine(path_); }

private:
    QString path_;
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
    // "Black" is really "ink": it flips to light on the dark scheme,
    // where a black selection would sink into the background.
    return darkActive() ? QColor(0xE8, 0xE6, 0xEB) : QColor(0x00, 0x00, 0x00);
}

QColor accentColor() {
    return accentColorFor(accent());
}

QColor accentTextColor() {
    const QColor accent = accentColor();
    const qreal luma =
        0.299 * accent.redF() + 0.587 * accent.greenF() + 0.114 * accent.blueF();
    return luma > 0.6 ? QColor(0x1B, 0x1B, 0x1E) : QColor(Qt::white);
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
    static const Palette dark = {
        QColor(0x1B, 0x1B, 0x1E),  // window
        QColor(0x20, 0x20, 0x24),  // canvas
        QColor(0x21, 0x21, 0x25),  // card
        QColor(0x26, 0x26, 0x2B),  // input
        QColor(0x2E, 0x2E, 0x34),  // inputHover
        QColor(0x3A, 0x3A, 0x40),  // border
        QColor(0x41, 0x41, 0x47),  // borderStrong
        QColor(0x2C, 0x2C, 0x31),  // separator
        QColor(0xE8, 0xE6, 0xEB),  // ink
        QColor(0xB8, 0xB6, 0xBD),  // value
        QColor(0x8F, 0x8D, 0x94),  // muted
        QColor(0x7C, 0x7A, 0x82),  // faint
        QColor(0x46, 0x46, 0x4C),  // scroll
        QColor(0x5A, 0x5A, 0x60),  // scrollHover
        QColor(0x4A, 0x4A, 0x50),  // toggleOff
        QColor(0x26, 0x26, 0x2B),  // veil
    };
    return darkActive() ? dark : light;
}

QIcon themedMenuIcon(const QString& name) {
    return QIcon(new ThemedIconEngine(QStringLiteral(":/icons/menu/%1.svg").arg(name)));
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
    qApp->setStyleSheet(sheet);
}

Notifier* notifier() {
    static Notifier instance;
    return &instance;
}

}  // namespace appearancesettings
