#pragma once

#include <QObject>
#include <QString>

// NetGraph availability and numeric force-simulation knobs, edited on
// the Settings → NetGraph page. Values persist via QSettings and
// notifier() lets every affected view update immediately.
namespace graphsettings {

// Field keys — also the QSettings suffixes under "netgraph/".
inline constexpr const char* kCenterForce = "center-force";
inline constexpr const char* kRepelForce = "repel-force";
inline constexpr const char* kLinkForce = "link-force";
inline constexpr const char* kLinkDistance = "link-distance";

// Availability switches. Disabling NetGraph forces both presentation
// switches on and prevents them from being cleared until it is enabled
// again.
bool disabled();
bool hideIcon();
bool hideButton();
void setDisabled(bool disabled);
void setHideIcon(bool hidden);
void setHideButton(bool hidden);

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
