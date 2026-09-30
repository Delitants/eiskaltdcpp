#pragma once

#include <QUrl>
#include <QString>

namespace updates {
enum class ResourceKind { Feed, Archive };

// Apply before DNS and again to every redirect, not just the initial URL.
bool allowedUrl(const QUrl& url, ResourceKind kind, bool redirect = false);
bool publicAddress(const QString& numericAddress);
}
