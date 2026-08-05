#include "generalsettings.hpp"

#include <QSettings>

namespace generalsettings {
namespace {
constexpr auto kEnablePresetsKey = "general/enable-presets";
constexpr auto kAllowCustomFieldsKey = "general/allow-custom-fields";
}

bool presetsEnabled() {
    return QSettings().value(QLatin1String(kEnablePresetsKey), false).toBool();
}

void setPresetsEnabled(bool enabled) {
    QSettings().setValue(QLatin1String(kEnablePresetsKey), enabled);
}

bool allowCustomFields() {
    return QSettings().value(QLatin1String(kAllowCustomFieldsKey), false).toBool();
}

void setAllowCustomFields(bool allowed) {
    QSettings().setValue(QLatin1String(kAllowCustomFieldsKey), allowed);
}

}  // namespace generalsettings
