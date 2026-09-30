#pragma once
#include "UpdateTypes.h"
#include <QUrl>
#include <QVector>
#include <functional>
#include <optional>

namespace updates {
struct SignedUpdate {
    UpdateOffer offer;
    QUrl archiveUrl;
    QByteArray archiveSignatureBase64;
};
struct VerifiedUpdateFeed {
    QByteArray signedBytes;
    QVector<SignedUpdate> releases;
};
// Authenticate the complete bounded feed before parsing any release metadata.
// The pinned key comes from the app, never from the feed. No I/O is performed.
std::optional<VerifiedUpdateFeed> verifyUpdateFeed(const QByteArray& bytes,
    const QByteArray& pinnedPublicKeyBase64, const std::function<bool()>& cancelled = {});
std::optional<SignedUpdate> selectUpdate(const VerifiedUpdateFeed& feed,
    const UpdateIdentity& installed, Channel channel);
}
