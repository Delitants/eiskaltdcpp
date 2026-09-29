#pragma once

#ifdef USE_TORRENT
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

namespace torrent_source {
// Empty means leave this input to the existing DC/external open handler.
inline QString normalize(const QString &input)
{
    const QString source = input.trimmed();
    if (source.isEmpty() || source.size() > 65536 || source.contains(QChar(0)))
        return {};
    const QUrl url(source, QUrl::StrictMode);
    if (url.scheme().compare(QStringLiteral("magnet"), Qt::CaseInsensitive) == 0) {
        if (!url.isValid())
            return {};
        bool bitTorrent = false;
        for (const auto &item : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
            if (item.first.compare(QStringLiteral("xt"), Qt::CaseInsensitive) != 0)
                continue;
            // Preserve DC ownership of mixed-protocol magnets, including the
            // legacy Tiger/bitprint forms accepted by the existing DC parser.
            if (item.second.startsWith(QStringLiteral("urn:tree:tiger"), Qt::CaseInsensitive) ||
                item.second.startsWith(QStringLiteral("urn:bitprint:"), Qt::CaseInsensitive))
                return {};
            bitTorrent |= item.second.startsWith(QStringLiteral("urn:btih:"), Qt::CaseInsensitive) ||
                item.second.startsWith(QStringLiteral("urn:btmh:"), Qt::CaseInsensitive);
        }
        // Leave full hash validation to the engine, without falling back to DC
        // or an external handler for a malformed BitTorrent exact topic.
        return bitTorrent ? source : QString();
    }
    if (url.isLocalFile()) {
        if (!url.isValid() || url.hasQuery() || url.hasFragment())
            return {};
        const auto local = url.toLocalFile();
        return local.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) ? local : QString();
    }
    const bool windowsDrive = source.size() >= 3 && source.at(0).isLetter() &&
        source.at(1) == QLatin1Char(':') &&
        (source.at(2) == QLatin1Char('\\') || source.at(2) == QLatin1Char('/'));
    if (!url.scheme().isEmpty() && !windowsDrive)
        return {};
    if (!source.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive))
        return {};
    // Do not URL-decode raw paths: '%' and '#' are legal filename characters.
    return windowsDrive ? source : QFileInfo(source).absoluteFilePath();
}
}
#endif
