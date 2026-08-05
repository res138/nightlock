#include "generalsettings.hpp"

#include <QSettings>

namespace generalsettings {
namespace {
constexpr auto kEnablePresetsKey = "general/enable-presets";
constexpr auto kAllowCustomFieldsKey = "general/allow-custom-fields";
constexpr auto kHideSearchIconKey = "general/hide-search-icon";
constexpr auto kHideLockButtonKey = "general/hide-lock-button";
constexpr auto kHideNewFolderButtonKey = "general/hide-new-folder-button";

bool readBool(const char* key) {
    return QSettings().value(QLatin1String(key), false).toBool();
}

void writeBool(const char* key, bool value) {
    QSettings settings;
    if (settings.value(QLatin1String(key), false).toBool() == value)
        return;
    settings.setValue(QLatin1String(key), value);
    notifier()->notify();
}
}

bool presetsEnabled() {
    return readBool(kEnablePresetsKey);
}

void setPresetsEnabled(bool enabled) {
    writeBool(kEnablePresetsKey, enabled);
}

bool allowCustomFields() {
    return readBool(kAllowCustomFieldsKey);
}

void setAllowCustomFields(bool allowed) {
    writeBool(kAllowCustomFieldsKey, allowed);
}

bool hideSearchIcon() {
    return readBool(kHideSearchIconKey);
}

void setHideSearchIcon(bool hidden) {
    writeBool(kHideSearchIconKey, hidden);
}

bool hideLockButton() {
    return readBool(kHideLockButtonKey);
}

void setHideLockButton(bool hidden) {
    writeBool(kHideLockButtonKey, hidden);
}

bool hideNewFolderButton() {
    return readBool(kHideNewFolderButtonKey);
}

void setHideNewFolderButton(bool hidden) {
    writeBool(kHideNewFolderButtonKey, hidden);
}

Notifier* notifier() {
    static Notifier instance;
    return &instance;
}

}  // namespace generalsettings
