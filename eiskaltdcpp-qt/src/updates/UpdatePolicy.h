#pragma once
#include "UpdateTypes.h"
#include <QDateTime>

namespace updates {
struct UpdatePolicy {
    static bool mayCheck(const UpdatePreferences&, CheckKind, QDateTime now, QDateTime lastAttempt, bool inFlight);
    static bool mayInstall(const UpdatePreferences&, bool normalQuit, const QString& readyKey, const QString& manualConsentKey);
    static bool accepts(const UpdateIdentity&, const UpdatePreferences&, const UpdateOffer&);
};
}
