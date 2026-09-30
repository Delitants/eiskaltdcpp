#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

namespace updates {
struct ArchiveExpectation {
    qint64 size = 0;
    QByteArray sha256;
    QByteArray signatureBase64;
};

// Worker-only. The key comes from the application, the expectation from an
// authenticated feed. The caller exclusively owns the private, immutable file.
// Success is not install consent: reverify the file before the quit handoff.
bool verifyUpdateArchive(const QString& path, const ArchiveExpectation&,
    const QByteArray& pinnedPublicKeyBase64, const std::function<bool()>& cancelled = {});
}
