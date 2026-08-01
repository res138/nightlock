#pragma once

#include <QString>

// Where the on-disk resources (icon packs, side-loaded fonts) live.
// Installed builds carry them inside the package — Nightlock.app/
// Contents/Resources on macOS, next to the executable on Windows,
// ../share/nightlock relative to the binary on Linux — while dev
// builds fall back to the source tree the binary was compiled from
// (the NIGHTLOCK_*_DIR compile definitions).
namespace respaths {

// Directory of the icon packs and app art (no trailing slash).
QString iconsDir();
// One file inside iconsDir(): respaths::icon("appicon.png").
QString icon(const QString& name);

// Directory of the side-loaded *.otf/*.ttf fonts.
QString fontsDir();

}  // namespace respaths
