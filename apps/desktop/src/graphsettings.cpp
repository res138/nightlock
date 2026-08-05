#include "graphsettings.hpp"

#include <QSettings>

namespace graphsettings {
namespace {

constexpr auto kDisabledKey = "netgraph/disabled";
constexpr auto kHideIconKey = "netgraph/hide-icon";
constexpr auto kHideButtonKey = "netgraph/hide-button";

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

bool disabled() {
    return QSettings().value(QLatin1String(kDisabledKey), false).toBool();
}

bool hideIcon() {
    return disabled() || QSettings().value(QLatin1String(kHideIconKey), false).toBool();
}

bool hideButton() {
    return disabled() || QSettings().value(QLatin1String(kHideButtonKey), false).toBool();
}

void setDisabled(bool isDisabled) {
    QSettings settings;
    bool changed = settings.value(QLatin1String(kDisabledKey), false).toBool() != isDisabled;
    if (changed)
        settings.setValue(QLatin1String(kDisabledKey), isDisabled);
    if (isDisabled) {
        if (!settings.value(QLatin1String(kHideIconKey), false).toBool()) {
            settings.setValue(QLatin1String(kHideIconKey), true);
            changed = true;
        }
        if (!settings.value(QLatin1String(kHideButtonKey), false).toBool()) {
            settings.setValue(QLatin1String(kHideButtonKey), true);
            changed = true;
        }
    }
    if (changed)
        notifier()->notify();
}

void setHideIcon(bool hidden) {
    if (disabled() && !hidden)
        return;
    QSettings settings;
    if (settings.value(QLatin1String(kHideIconKey), false).toBool() == hidden)
        return;
    settings.setValue(QLatin1String(kHideIconKey), hidden);
    notifier()->notify();
}

void setHideButton(bool hidden) {
    if (disabled() && !hidden)
        return;
    QSettings settings;
    if (settings.value(QLatin1String(kHideButtonKey), false).toBool() == hidden)
        return;
    settings.setValue(QLatin1String(kHideButtonKey), hidden);
    notifier()->notify();
}

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
