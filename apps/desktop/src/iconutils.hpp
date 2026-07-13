#pragma once

#include <QIcon>
#include <QPixmap>

namespace iconutils {

// Loads an image and knocks out its near-white background — for icons
// exported as JPEG without an alpha channel.
QPixmap pixmapWithWhiteKnockedOut(const QString& path);
QIcon iconWithWhiteKnockedOut(const QString& path);

}  // namespace iconutils
