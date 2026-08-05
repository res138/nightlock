#pragma once

#include <QColor>
#include <QList>
#include <QString>

#include <nightlock/entry.hpp>

// Shared presentation data for the per-entry color picker and the
// subtle Entry List background.
namespace entrycolors {

struct DetailPalette {
    QColor background;
    QColor border;
    QColor separator;
};

QList<nightlock::EntryColor> values();
QString title(nightlock::EntryColor color);
QColor swatch(nightlock::EntryColor color);
QColor subtleFill(nightlock::EntryColor color);
DetailPalette detailPalette(nightlock::EntryColor color);

}  // namespace entrycolors
