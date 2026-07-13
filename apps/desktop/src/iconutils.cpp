#include "iconutils.hpp"

#include <QImage>

namespace iconutils {

QPixmap pixmapWithWhiteKnockedOut(const QString& path) {
    QImage image(path);
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x];
            if (qRed(px) > 240 && qGreen(px) > 240 && qBlue(px) > 240)
                line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), 0);
        }
    }
    return QPixmap::fromImage(image);
}

QIcon iconWithWhiteKnockedOut(const QString& path) {
    return QIcon(pixmapWithWhiteKnockedOut(path));
}

}  // namespace iconutils
