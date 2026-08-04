#include "fonts.hpp"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>

#include <vector>

#include "appearancesettings.hpp"
#include "respaths.hpp"

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
    const QDir dir(respaths::fontsDir());
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

// One catalog for both roles — the Primary and Secondary pickers list
// exactly the same fonts (sans, then serif, then mono), so any font
// can play either part. Each entry is a canonical name plus the
// platform aliases / metric-compatible stand-ins that make it resolve
// on macOS, Windows and Linux alike.
const std::vector<RawOption>& rawOptions() {
    static const std::vector<RawOption> catalog = {
        {"san-francisco", "San Francisco",
         {"SF Pro Text", "SF Pro Display", "SF Pro", "San Francisco", ".AppleSystemUIFont",
          ".SF NS Text"}},
        {"helvetica-neue", "Helvetica Neue", {"Helvetica Neue", "Helvetica"}},
        {"segoe-ui", "Segoe UI", {"Segoe UI"}},
        // The runs-everywhere option: macOS and Windows ship Arial,
        // Linux covers it with the metric-compatible Liberation Sans.
        {"arial", "Arial", {"Arial", "Liberation Sans"}},
        {"inter", "Inter", {"Inter", "Inter Variable", "Inter Display"}},
        {"roboto", "Roboto", {"Roboto", "Roboto Flex"}},
        {"verdana", "Verdana", {"Verdana", "DejaVu Sans"}},
        {"tahoma", "Tahoma", {"Tahoma"}},
        {"trebuchet-ms", "Trebuchet MS", {"Trebuchet MS"}},
        {"georgia", "Georgia", {"Georgia"}},
        // The runs-everywhere option: Liberation Serif covers Linux.
        {"times-new-roman", "Times New Roman",
         {"Times New Roman", "Liberation Serif", "Times"}},
        {"palatino", "Palatino", {"Palatino", "Palatino Linotype", "Book Antiqua", "P052"}},
        {"charter", "Charter", {"Charter", "Bitstream Charter", "XCharter"}},
        {"garamond", "Garamond",
         {"Garamond", "EB Garamond", "Adobe Garamond Pro", "URW Garamond"}},
        {"baskerville", "Baskerville",
         {"Baskerville", "Baskerville Old Face", "Libre Baskerville"}},
        {"menlo", "Menlo",
         {"Menlo", "SF Mono", "Consolas", "DejaVu Sans Mono", "Liberation Mono"}},
    };
    return catalog;
}

// Distinct defaults keep the roles' characters apart even though the
// catalogs match: the interface stays sans, the display serif.
const char* defaultId(Role role) {
    return role == Role::Primary ? "san-francisco" : "georgia";
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
    Q_UNUSED(role);  // both roles share the one catalog
    loadBundledFonts();
    QList<Option> list;
    for (const RawOption& raw : rawOptions()) {
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
    const auto indexOfAvailable = [&catalog](const QString& id) {
        for (int i = 0; i < catalog.size(); ++i)
            if (catalog[i].id == id && catalog[i].available)
                return i;
        return -1;
    };
    const QString stored =
        QSettings()
            .value(QLatin1String(settingsKey(role)), QLatin1String(defaultId(role)))
            .toString();
    if (const int index = indexOfAvailable(stored); index >= 0)
        return index;
    // The stored font is gone from this system: fall back to the
    // role's own default first — a serif role should not degrade into
    // the catalog's leading sans — then to whatever is available.
    if (const int index = indexOfAvailable(QLatin1String(defaultId(role))); index >= 0)
        return index;
    for (int i = 0; i < catalog.size(); ++i)
        if (catalog[i].available)
            return i;
    return 0;
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
                 qPrintable(respaths::fontsDir()));
    QFont font = QApplication::font();
    // The whole alias chain goes in, so glyphs missing from one cut
    // (e.g. ⌘) resolve from the next instead of a random substitute.
    font.setFamilies(option.families);
    QApplication::setFont(font);
}

}  // namespace fonts
