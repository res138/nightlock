#pragma once

// General application preferences shared by Settings and entry forms.
namespace generalsettings {

bool presetsEnabled();
void setPresetsEnabled(bool enabled);

bool allowCustomFields();
void setAllowCustomFields(bool allowed);

}  // namespace generalsettings
