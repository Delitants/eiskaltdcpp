#pragma once

#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QRegularExpression>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#ifdef USE_TORRENT
#include <QDir>
#include <QFileInfo>
#endif
#include <functional>
#include <utility>

class PendingOpenEvents : public QObject
{
public:
    using Handler = std::function<void(const QString &)>;
    using DeliveryGate = std::function<bool()>;
    explicit PendingOpenEvents(QObject *parent = nullptr) : QObject(parent)
    {
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, &PendingOpenEvents::drainOne);
    }

    static QString nativeSource(const QString &file, const QUrl &url)
    {
        return file.isEmpty() ? url.toString(QUrl::FullyEncoded) : file;
    }

    bool enqueue(const QString &source)
    {
        if (pending_.size() >= MaxPendingRequests)
            return false;
        const QString normalized = normalize(source);
        if (normalized.isEmpty())
            return false;
        pending_.enqueue(normalized);
        schedule();
        return true;
    }

    // A second launch without arguments requests activation, not a file open.
    void enqueueActivation()
    {
        if (pending_.size() >= MaxPendingRequests)
            return;
        pending_.enqueue(QString());
        schedule();
    }

    void setHandler(Handler handler, DeliveryGate gate = {})
    {
        handler_ = std::move(handler);
        gate_ = std::move(gate);
        schedule();
    }

    void setReady(bool ready)
    {
        ready_ = ready;
        if (!ready)
            timer_.stop();
        else
            schedule();
    }

private:
    // Reject newest arrivals at capacity; never evict already accepted opens.
    static constexpr int MaxPendingRequests = 128;

    static bool containsControl(const QString &source)
    {
        for (const QChar ch : source) {
            if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
                return true;
        }
        return false;
    }

    static QString normalize(const QString &input)
    {
        if (input.size() > 65536 || containsControl(input))
            return {};
        const QString source = input.trimmed();
        if (source.isEmpty())
            return {};
#ifdef USE_TORRENT
        // Native paths are not URLs: literal '%' and '#' must survive unchanged.
        if (QDir::isAbsolutePath(source) &&
            source.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive))
            return source;
#endif
        QUrl url(source, QUrl::StrictMode);
#ifdef USE_TORRENT
        if (url.scheme().isEmpty() &&
            source.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive))
            return QFileInfo(source).absoluteFilePath();
#endif
        if (!url.isValid() || containsControl(QUrl::fromPercentEncoding(source.toUtf8())))
            return {};
        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("magnet")) {
            if (!url.authority().isEmpty() || !url.path().isEmpty() || url.hasFragment())
                return {};
            static const QRegularExpression dcTopic(
                QStringLiteral("^urn:(?:tree:tiger(?:/|/1024)?:[A-Z2-7]{39}|bitprint:[A-Z2-7]{32}\\.[A-Z2-7]{39})$"),
                QRegularExpression::CaseInsensitiveOption);
#ifdef USE_TORRENT
            static const QRegularExpression btTopic(
                QStringLiteral("^urn:(?:btih:(?:[0-9a-f]{40}|[a-z2-7]{32})|btmh:1220[0-9a-f]{64})$"),
                QRegularExpression::CaseInsensitiveOption);
#endif
            bool validTopic = false, keyword = false, torrentTopic = false;
            for (const auto &item : QUrlQuery(url).queryItems(QUrl::FullyDecoded)) {
                const QString key = item.first.toLower();
                if (key == QStringLiteral("kt"))
                    keyword |= !item.second.trimmed().isEmpty();
                if (key != QStringLiteral("xt") && key != QStringLiteral("xs") && key != QStringLiteral("as"))
                    continue;
                validTopic |= dcTopic.match(item.second).hasMatch();
                if (key != QStringLiteral("xt"))
                    continue;
                torrentTopic |= item.second.startsWith(QStringLiteral("urn:btih:"), Qt::CaseInsensitive) ||
                    item.second.startsWith(QStringLiteral("urn:btmh:"), Qt::CaseInsensitive);
#ifdef USE_TORRENT
                validTopic |= btTopic.match(item.second).hasMatch();
#endif
            }
            // Preserve DC keyword-search links, but never reinterpret an
            // invalid/disabled torrent exact topic as a DC keyword search.
            if (validTopic || (keyword && !torrentTopic)) {
                url.setScheme(scheme);
                return url.toString(QUrl::FullyEncoded);
            }
            return {};
        }
        if (scheme == QStringLiteral("adc") || scheme == QStringLiteral("adcs") ||
            scheme == QStringLiteral("dchub") || scheme == QStringLiteral("nmdcs")) {
            if (url.host().isEmpty())
                return {};
            url.setScheme(scheme);
            return url.toString(QUrl::FullyEncoded);
        }
#ifdef USE_TORRENT
        if (url.isLocalFile() && !url.hasQuery() && !url.hasFragment()) {
            const QString path = url.toLocalFile();
            if (path.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) &&
                !containsControl(path))
                return path;
        }
#endif
        return {};
    }

    void schedule()
    {
        if (ready_ && handler_ && !pending_.isEmpty() && !delivering_ && !timer_.isActive())
            timer_.start(0);
    }

    void drainOne()
    {
        if (!ready_ || !handler_ || pending_.isEmpty() || delivering_)
            return;
        if (gate_ && !gate_()) {
            timer_.start(50);
            return;
        }
        // A consumer may run a modal event loop or enqueue more opens. Keep
        // those events queued until it returns, then yield before the next one.
        const QString source = pending_.dequeue();
        const Handler handler = handler_;
        const QPointer<PendingOpenEvents> alive(this);
        delivering_ = true;
        handler(source);
        if (!alive)
            return;
        delivering_ = false;
        schedule();
    }

    QQueue<QString> pending_;
    QTimer timer_;
    Handler handler_;
    DeliveryGate gate_;
    bool ready_ = false;
    bool delivering_ = false;
};
