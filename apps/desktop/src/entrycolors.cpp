#include "entrycolors.hpp"

#include <QCoreApplication>

#include "appearancesettings.hpp"

namespace entrycolors {

QList<nightlock::EntryColor> values() {
    using nightlock::EntryColor;
    return {EntryColor::None,   EntryColor::Red,   EntryColor::Orange,
            EntryColor::Yellow, EntryColor::Green, EntryColor::Blue,
            EntryColor::Purple};
}

QString title(nightlock::EntryColor color) {
    using nightlock::EntryColor;
    switch (color) {
        case EntryColor::None:
            return QCoreApplication::translate("EntryColors", "None");
        case EntryColor::Red:
            return QCoreApplication::translate("EntryColors", "Red");
        case EntryColor::Orange:
            return QCoreApplication::translate("EntryColors", "Orange");
        case EntryColor::Yellow:
            return QCoreApplication::translate("EntryColors", "Yellow");
        case EntryColor::Green:
            return QCoreApplication::translate("EntryColors", "Green");
        case EntryColor::Blue:
            return QCoreApplication::translate("EntryColors", "Blue");
        case EntryColor::Purple:
            return QCoreApplication::translate("EntryColors", "Purple");
    }
    return QCoreApplication::translate("EntryColors", "None");
}

QColor swatch(nightlock::EntryColor color) {
    using nightlock::EntryColor;
    switch (color) {
        case EntryColor::Red: return QColor(0xFF, 0x45, 0x3A);
        case EntryColor::Orange: return QColor(0xFF, 0x9F, 0x0A);
        case EntryColor::Yellow: return QColor(0xFF, 0xD6, 0x0A);
        case EntryColor::Green: return QColor(0x30, 0xD1, 0x58);
        case EntryColor::Blue: return QColor(0x0A, 0x84, 0xFF);
        case EntryColor::Purple: return QColor(0xBF, 0x5A, 0xF2);
        case EntryColor::None: return appearancesettings::palette().borderStrong;
    }
    return appearancesettings::palette().borderStrong;
}

QColor subtleFill(nightlock::EntryColor color) {
    QColor result = swatch(color);
    result.setAlphaF(appearancesettings::darkActive() ? 0.18 : 0.11);
    return result;
}

DetailPalette detailPalette(nightlock::EntryColor color) {
    const auto& palette = appearancesettings::palette();
    if (color == nightlock::EntryColor::None)
        return {palette.card, palette.border, palette.separator};

    const QColor hueSource = swatch(color).toHsl();
    const bool dark = appearancesettings::darkActive();
    const auto colorize = [hueSource, dark](const QColor& neutral) {
        // Preserve the theme's existing lightness distance between
        // card, separator and outer border. A small shift away from
        // pure white/black makes the selected hue perceptible without
        // flattening those three semantic levels into one color.
        const qreal lightness = qBound(0.0, neutral.lightnessF() + (dark ? 0.018 : -0.018),
                                       1.0);
        const qreal saturation = dark ? 0.48 : 0.56;
        return QColor::fromHslF(hueSource.hslHueF(), saturation, lightness);
    };
    return {colorize(palette.card), colorize(palette.border),
            colorize(palette.separator)};
}

}  // namespace entrycolors
