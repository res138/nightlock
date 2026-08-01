#include "respaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace respaths {
namespace {

// Installed layout first, source tree last: a from-source build has
// no bundled copies, an installed app should never read the (absent)
// build machine's source tree.
QString resolveDir(const char* subdir, const char* devPath) {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#if defined(Q_OS_MACOS)
        appDir + QStringLiteral("/../Resources/") + QLatin1String(subdir),
#elif defined(Q_OS_WIN)
        appDir + QLatin1Char('/') + QLatin1String(subdir),
#else
        // The .deb keeps the binary in /usr/lib/nightlock (Qt rides
        // along there); a plain bin/ install is the second form.
        appDir + QStringLiteral("/../../share/nightlock/") + QLatin1String(subdir),
        appDir + QStringLiteral("/../share/nightlock/") + QLatin1String(subdir),
#endif
        QLatin1String(devPath),
    };
    for (const QString& candidate : candidates)
        if (QFileInfo::exists(candidate))
            return QDir(candidate).absolutePath();
    return QLatin1String(devPath);
}

}  // namespace

QString iconsDir() {
    static const QString dir = resolveDir("icons", NIGHTLOCK_ICONS_DIR);
    return dir;
}

QString icon(const QString& name) {
    return iconsDir() + QLatin1Char('/') + name;
}

QString fontsDir() {
    static const QString dir = resolveDir("fonts", NIGHTLOCK_FONTS_DIR);
    return dir;
}

}  // namespace respaths
