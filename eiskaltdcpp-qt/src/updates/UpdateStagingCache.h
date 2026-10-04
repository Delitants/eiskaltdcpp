#pragma once
#include "UpdateFeedVerifier.h"
#include <functional>

namespace updates {
// Worker-only optimization, never an offer or installation-consent source.
// The caller exclusively owns existing private directories and keeps the fresh
// selected offer subject to installed identity/channel/route policy. Restore
// authenticates both feeds and copies/reverifies into a new private stage.
// Cache reserves appcast.xml/update.zip and .stage-feed/.stage-archive only.
bool saveVerifiedStage(const QString& cacheDirectory, const QByteArray& signedFeed,
    const SignedUpdate& selected, const QByteArray& pinnedPublicKey,
    const QString& archive, const std::function<bool()>& cancelled = {});
bool restoreVerifiedStage(const QString& cacheDirectory, const QByteArray& currentSignedFeed,
    const SignedUpdate& selected, const QByteArray& pinnedPublicKey,
    const QString& destinationArchive, const std::function<bool()>& cancelled = {});
}
