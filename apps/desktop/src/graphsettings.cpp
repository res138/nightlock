#include "graphsettings.hpp"

#include <QSettings>

namespace graphsettings {
namespace {

struct Default {
    const char* key;
    qreal value;
};
constexpr Default kDefaults[] = {
    {kCenterForce, 0.26},
    {kRepelForce, 13.38},
    {kLinkForce, 1.0},
    {kLinkDistance, 100.0},
};

qreal defaultFor(const QString& key) {
    for (const Default& entry : kDefaults)
        if (key == QLatin1String(entry.key))
            return entry.value;
    return 0.0;
}

qreal readValue(const QString& key) {
    return QSettings()
        .value(QStringLiteral("netgraph/") + key, defaultFor(key))
        .toDouble();
}

Config& cache() {
    static Config config = {readValue(kCenterForce), readValue(kRepelForce),
                            readValue(kLinkForce), readValue(kLinkDistance)};
    return config;
}

}  // namespace

Config config() {
    return cache();
}

qreal value(const QString& key) {
    return readValue(key);
}

void setValue(const QString& key, qreal value) {
    QSettings().setValue(QStringLiteral("netgraph/") + key, value);
    Config& config = cache();
    if (key == QLatin1String(kCenterForce))
        config.centerForce = value;
    else if (key == QLatin1String(kRepelForce))
        config.repelForce = value;
    else if (key == QLatin1String(kLinkForce))
        config.linkForce = value;
    else if (key == QLatin1String(kLinkDistance))
        config.linkDistance = value;
    notifier()->notify();
}

Notifier* notifier() {
    static Notifier instance;
    return &instance;
}

}  // namespace graphsettings
