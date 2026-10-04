#pragma once
#include "UpdateTypes.h"
#include <QUrl>
#include <QVariantMap>
#include <optional>

namespace updates {
struct UpdateRuntimeConfig {
    UpdateIdentity identity;
    QUrl feedUrl;
    QByteArray publicKeyBase64;
};
// Structural eligibility only, not bundle signature/vendor authentication.
// Caller supplies installed metadata, never a downloaded feed's identity/key.
std::optional<UpdateRuntimeConfig> validateMacUpdateRuntime(const QVariantMap&,
    const QString& compiledCpu, const QVersionNumber& runtimeOs);
// Read only the running process's actual main bundle. No path/profile override,
// network access, scheduler, or installation activation; unsupported returns none.
std::optional<UpdateRuntimeConfig> currentMacUpdateRuntime();
}
