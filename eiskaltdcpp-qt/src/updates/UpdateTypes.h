#pragma once
#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QVersionNumber>

namespace updates {
enum class Channel { Stable, Preview };
enum class CheckKind { Automatic, Manual };
enum class FailureCategory { Network, Verification, Storage, Unsupported };
struct UpdatePreferences {
    bool checks = true, notifications = true, autoInstall = false;
    Channel channel = Channel::Stable;
};
struct UpdateIdentity {
    quint64 build = 0;
    Channel defaultChannel = Channel::Stable;
    QString bundleId, cpu;
    QVersionNumber os;
};
// Only the verifying backend may produce an actionable offer. No commands or
// untrusted public key are carried across the controller/UI boundary.
struct UpdateOffer {
    quint64 build = 0;
    QString version, notes, bundleId, cpu;
    QVersionNumber minimumOs;
    Channel channel = Channel::Stable;
    qint64 size = 0;
    QByteArray sha256;
    QString key() const { return QString::number(build) + ":" + QString::fromLatin1(sha256.toHex()); }
};
}
Q_DECLARE_METATYPE(updates::UpdateOffer)
Q_DECLARE_METATYPE(updates::Channel)
Q_DECLARE_METATYPE(updates::CheckKind)
Q_DECLARE_METATYPE(updates::FailureCategory)
