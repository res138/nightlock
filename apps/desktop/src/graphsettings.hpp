#pragma once

#include <QObject>
#include <QString>

// Numeric knobs of the NetGraph force simulation, edited on the
// Settings → NetGraph page. Values persist via QSettings and reach a
// running simulation immediately: it reads config() every tick, and
// notifier() lets views re-warm their layout on a change.
namespace graphsettings {

// Field keys — also the QSettings suffixes under "netgraph/".
inline constexpr const char* kCenterForce = "center-force";
inline constexpr const char* kRepelForce = "repel-force";
inline constexpr const char* kLinkForce = "link-force";
inline constexpr const char* kLinkDistance = "link-distance";

struct Config {
    qreal centerForce;   // pull toward the canvas center
    qreal repelForce;    // node-node repulsion
    qreal linkForce;     // spring strength of every link
    qreal linkDistance;  // spring rest length, scene units
};

Config config();

// Current value / default for one field key.
qreal value(const QString& key);

// Persists the field and pings notifier().
void setValue(const QString& key, qreal value);

class Notifier : public QObject {
    Q_OBJECT
public:
    void notify() { emit changed(); }
signals:
    void changed();
};
Notifier* notifier();

}  // namespace graphsettings
