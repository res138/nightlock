#pragma once

#include <QObject>

// General application preferences shared by Settings and entry forms.
namespace generalsettings {

bool presetsEnabled();
void setPresetsEnabled(bool enabled);

bool allowCustomFields();
void setAllowCustomFields(bool allowed);

bool hideSearchIcon();
void setHideSearchIcon(bool hidden);

bool hideLockButton();
void setHideLockButton(bool hidden);

bool hideNewFolderButton();
void setHideNewFolderButton(bool hidden);

class Notifier : public QObject {
    Q_OBJECT
public:
    void notify() { emit changed(); }
signals:
    void changed();
};
Notifier* notifier();

}  // namespace generalsettings
