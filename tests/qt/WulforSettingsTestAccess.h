#pragma once
#include "WulforSettings.h"

struct WulforSettingsTestAccess {
    static QSettings* storage(WulforSettings& owner) { return owner.settings.get(); }
};
