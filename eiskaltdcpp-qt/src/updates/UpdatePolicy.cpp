#include "UpdatePolicy.h"
namespace updates {
bool UpdatePolicy::mayCheck(const UpdatePreferences& preferences, CheckKind kind,
    QDateTime now, QDateTime lastAttempt, bool inFlight) {
    if(inFlight || !now.isValid()) return false;
    if(kind == CheckKind::Manual) return true;
    return kind == CheckKind::Automatic && preferences.checks &&
        (!lastAttempt.isValid() || lastAttempt.secsTo(now) >= 86400);
}
bool UpdatePolicy::mayInstall(const UpdatePreferences& preferences, bool normalQuit,
    const QString& readyKey, const QString& manualConsentKey) {
    return normalQuit && !readyKey.isEmpty() &&
        (readyKey == manualConsentKey || (preferences.checks && preferences.autoInstall));
}
bool UpdatePolicy::accepts(const UpdateIdentity& identity, const UpdatePreferences& preferences, const UpdateOffer& offer) {
    return identity.build > 0 && offer.build > identity.build && !identity.bundleId.isEmpty() &&
        (identity.cpu == "arm64" || identity.cpu == "x86_64") &&
        offer.bundleId == identity.bundleId && offer.cpu == identity.cpu &&
        !identity.os.isNull() && !offer.minimumOs.isNull() && offer.minimumOs.majorVersion() > 0 &&
        QVersionNumber::compare(offer.minimumOs, identity.os) <= 0 &&
        !offer.version.trimmed().isEmpty() && offer.sha256.size() == 32 &&
        offer.size > 0 && offer.size <= 512LL * 1024 * 1024 &&
        (preferences.channel == Channel::Stable || preferences.channel == Channel::Preview) &&
        (offer.channel == Channel::Stable || (offer.channel == Channel::Preview && preferences.channel == Channel::Preview));
}
}
