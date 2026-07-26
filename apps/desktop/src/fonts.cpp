#include "fonts.hpp"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>

#include <vector>

#include "appearancesettings.hpp"

namespace fonts {
namespace {

// Side-loaded fonts for non-Apple platforms; the directory ships
// empty (plus a README) because the SF Pro license does not allow
// redistributing the files.
void loadBundledFonts() {
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    const QDir dir(QStringLiteral(NIGHTLOCK_FONTS_DIR));
    const QStringList files =
        dir.entryList({QStringLiteral("*.otf"), QStringLiteral("*.ttf")}, QDir::Files);
    for (const QString& file : files)
        QFontDatabase::addApplicationFont(dir.filePath(file));
}

struct RawOption {
    const char* id;
    const char* title;
    std::vector<const char*> families;
};

const std::vector<RawOption>& rawOptions(Role role) {
    static const std::vector<RawOption> primary = {
        {"san-francisco", "San Francisco",
         {"SF Pro Text", "SF Pro Display", "SF Pro", "San Francisco", ".AppleSystemUIFont",
          ".SF NS Text"}},
        {"helvetica-neue", "Helvetica Neue", {"Helvetica Neue", "Helvetica"}},
        {"segoe-ui", "Segoe UI", {"Segoe UI"}},
        // The runs-everywhere option: macOS and Windows ship Arial,
        // Linux covers it with the metric-compatible Liberation Sans.
        {"arial", "Arial", {"Arial", "Liberation Sans"}},
    };
    static const std::vector<RawOption> secondary = {
        {"georgia", "Georgia", {"Georgia"}},
        // The runs-everywhere option: Liberation Serif covers Linux.
        {"times-new-roman", "Times New Roman",
         {"Times New Roman", "Liberation Serif", "Times"}},
        {"palatino", "Palatino", {"Palatino", "Palatino Linotype", "Book Antiqua", "P052"}},
        {"charter", "Charter", {"Charter", "Bitstream Charter", "XCharter"}},
    };
    return role == Role::Primary ? primary : secondary;
}

bool anyFamilyPresent(const RawOption& raw) {
    for (const char* family : raw.families)
        if (QFontDatabase::hasFamily(QLatin1String(family)))
            return true;
#ifdef Q_OS_MACOS
    // San Francisco is the macOS system font even when the database
    // hides the dot-prefixed private families.
    if (QLatin1String(raw.id) == QLatin1String("san-francisco"))
        return true;
#endif
    return false;
}

const char* settingsKey(Role role) {
    return role == Role::Primary ? "appearance/font-primary" : "appearance/font-secondary";
}

int slot(Role role) {
    return role == Role::Primary ? 0 : 1;
}

// Bumped on every setSelected; lets resolvedFamily() stay O(1) on the
// hot paint paths instead of re-querying the font database.
int generation = 1;

}  // namespace

QList<Option> options(Role role) {
    loadBundledFonts();
    QList<Option> list;
    for (const RawOption& raw : rawOptions(role)) {
        Option option;
        option.id = QLatin1String(raw.id);
        option.title = QLatin1String(raw.title);
        for (const char* family : raw.families)
            option.families.append(QLatin1String(family));
        option.available = anyFamilyPresent(raw);
        list.append(option);
    }
    return list;
}

int selectedIndex(Role role) {
    const QList<Option> catalog = options(role);
    const QString stored =
        QSettings().value(QLatin1String(settingsKey(role)), catalog.first().id).toString();
    int firstAvailable = 0;
    for (int i = 0; i < catalog.size(); ++i) {
        if (catalog[i].available) {
            firstAvailable = i;
            break;
        }
    }
    for (int i = 0; i < catalog.size(); ++i)
        if (catalog[i].id == stored)
            return catalog[i].available ? i : firstAvailable;
    return firstAvailable;
}

QString resolvedFamily(Role role) {
    static int cachedGeneration[2] = {0, 0};
    static QString cachedFamily[2];
    if (cachedGeneration[slot(role)] == generation)
        return cachedFamily[slot(role)];

    const Option option = options(role).at(selectedIndex(role));
    QString family = option.families.first();
    for (const QString& alias : option.families) {
        if (QFontDatabase::hasFamily(alias)) {
            family = alias;
            break;
        }
    }
    cachedGeneration[slot(role)] = generation;
    cachedFamily[slot(role)] = family;
    return family;
}

void setSelected(Role role, const QString& id) {
    QSettings().setValue(QLatin1String(settingsKey(role)), id);
    ++generation;
    if (role == Role::Primary)
        applyApplicationFont();
    // The stylesheet carries the secondary family, and reinstalling
    // it repolishes every widget, so both fonts land immediately.
    appearancesettings::applyStylesheet();
    appearancesettings::notifier()->notify();
}

void applyApplicationFont() {
    loadBundledFonts();
    const Option option = options(Role::Primary).at(selectedIndex(Role::Primary));
    if (!option.available)
        qWarning("None of the interface fonts are available: drop the SF Pro *.otf "
                 "files into %s (https://developer.apple.com/fonts/). Falling back "
                 "to the platform font.",
                 NIGHTLOCK_FONTS_DIR);
    QFont font = QApplication::font();
    // The whole alias chain goes in, so glyphs missing from one cut
    // (e.g. ⌘) resolve from the next instead of a random substitute.
    font.setFamilies(option.families);
    QApplication::setFont(font);
}

}  // namespace fonts
