#include "dcpp/stdinc.h"
#include "dcpp/HttpRequestPolicy.h"
#include "dcpp/HttpConnection.h"
#include "dcpp/ProxyResolver.h"
#include "dcpp/SettingsManager.h"
#include "UpdateTransport.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QPointer>
#include <QThread>
#include <atomic>
#include <thread>
#include <stdexcept>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace updates {
namespace {
void require(bool condition) {
    if(!condition) throw std::runtime_error("Update transport requirement failed");
}
bool revoked(const std::shared_ptr<const dcpp::ProxyRouteSnapshot>& route) {
    return !route || !route->valid || !route->revoked || route->revoked->load();
}

struct Listener final : dcpp::HttpConnectionListener {
    transport_detail::Sink sink;
    bool complete = false;
    explicit Listener(transport_detail::Sink value) : sink(std::move(value)) {}
    void on(Data, dcpp::HttpConnection*, const uint8_t* data, size_t size) override { sink(data, size); }
    void on(Complete, dcpp::HttpConnection*, const std::string&) override { complete = true; }
};

void privateDestination(const QString& destination) {
    const QFileInfo file(destination), parent(file.absolutePath());
    require(file.isAbsolute() && !file.fileName().isEmpty() && !file.exists() && !file.isSymLink());
    require(parent.isDir() && !parent.isSymLink());
#ifndef _WIN32
    require(parent.ownerId() == getuid());
#endif
    require(!(parent.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
        QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)));
}
}

struct UpdateTransport::Impl {
    struct Job {
        quint64 id = 0;
        QString destination, error;
        std::shared_ptr<const dcpp::ProxyRouteSnapshot> route;
        std::atomic_bool cancelled{false}, progressPending{false};
        std::atomic<quint64> bytes{0};
        std::shared_ptr<dcpp::WakeNotifier> notifier = std::make_shared<dcpp::WakeNotifier>();
        std::thread worker;
        bool created = false;
        void stop() { cancelled.store(true); notifier->notify(); }
        void join() { if(worker.joinable()) worker.join(); }
    };
    transport_detail::Backend backend;
    std::shared_ptr<Job> job;
    quint64 sequence = 0;
    bool stopped = false, stopSucceeded = false;
};

UpdateTransport::UpdateTransport(dcpp::DCContext& context, QObject* parent)
    : UpdateTransport(context, {}, parent) {}
UpdateTransport::UpdateTransport(dcpp::DCContext& context, transport_detail::Backend backend, QObject* parent)
    : QObject(parent), impl(std::make_unique<Impl>()) {
    impl->backend = backend ? std::move(backend) : transport_detail::Backend(
        [&context](const auto& policy, const auto& url, const auto& sink) {
            Listener listener(sink);
            dcpp::HttpConnection connection(context, policy, {});
            connection.addListener(&listener);
            connection.downloadFile(url);
            require(listener.complete);
        });
}

UpdateTransport::~UpdateTransport() {
    if(impl->job) {
        impl->job->stop();
        impl->job->join();
        if(impl->job->created) QFile::remove(impl->job->destination);
    }
}

quint64 UpdateTransport::fetch(const QUrl& url, ResourceKind kind, const QString& destination,
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(impl->stopped) return 0;
    const auto id = ++impl->sequence;
    if(impl->job) {
        QMetaObject::invokeMethod(this, [this, id] {
            if(!impl->stopped) emit failed(id, tr("An update download is already running."));
        }, Qt::QueuedConnection);
        return id;
    }
    const auto job = std::make_shared<Impl::Job>();
    job->id = id;
    job->destination = destination;
    job->route = std::move(route);
    impl->job = job;
    const auto finish = [this, job] {
        job->join();
        if(impl->stopped || impl->job != job) return;
        if(job->created && !job->cancelled.load() && !revoked(job->route) && job->error.isEmpty()) {
            QPointer<UpdateTransport> alive(this);
            emit progress(job->id, job->bytes.load(), job->bytes.load());
            if(!alive) return;
            if(impl->stopped || impl->job != job) return;
        }
        const bool cancelled = job->cancelled.load() || revoked(job->route);
        const bool success = job->created && !cancelled && job->error.isEmpty();
        if(!success && job->created) QFile::remove(job->destination);
        impl->job.reset();
        if(success) {
            emit finished(job->id, job->destination);
        } else {
            emit failed(job->id, cancelled ? tr("Update download cancelled or proxy settings changed.") : job->error);
        }
    };
    try {
        job->worker = std::thread([this, job, url, kind, backend = impl->backend, finish] {
            const auto cancelled = [job] { return job->cancelled.load() || revoked(job->route); };
            try {
                require(!cancelled() && allowedUrl(url, kind));
                privateDestination(job->destination);
                QSaveFile output(job->destination);
                output.setDirectWriteFallback(false);
                require(output.open(QIODevice::WriteOnly));
                require(output.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
                auto policy = transport_detail::policy(kind, job->route);
                policy.cancelled = cancelled;
                policy.cancellationNotifier = job->notifier;
                backend(policy, url.toEncoded().toStdString(), [this, job, &output, &policy, &cancelled](const uint8_t* data, size_t size) {
                    require(!cancelled());
                    const auto before = job->bytes.load();
                    require(before <= policy.limits.bodyBytes && size <= policy.limits.bodyBytes - before);
                    require(output.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size)) == static_cast<qint64>(size));
                    job->bytes.store(before + size);
                    if(!job->progressPending.exchange(true)) {
                        QMetaObject::invokeMethod(this, [this, job] {
                            job->progressPending.store(false);
                            if(impl->job == job && !job->cancelled.load() && !revoked(job->route))
                                emit progress(job->id, job->bytes.load(), 0);
                        }, Qt::QueuedConnection);
                    }
                });
                require(!cancelled());
                privateDestination(job->destination);
                require(output.commit());
                job->created = true;
            } catch(...) {
                job->error = tr("Update download failed. No direct-connection fallback was used.");
            }
            QMetaObject::invokeMethod(this, finish, Qt::QueuedConnection);
        });
    } catch(...) {
        job->error = tr("Unable to start the update download worker.");
        QMetaObject::invokeMethod(this, finish, Qt::QueuedConnection);
    }
    return id;
}

void UpdateTransport::cancel(quint64 id) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(impl->job && impl->job->id == id) impl->job->stop();
}

bool UpdateTransport::stopWorkersForQuit() {
    if(QThread::currentThread() != thread()) return false;
    if(impl->stopped) return impl->stopSucceeded;
    impl->stopped = true;
    const auto job = impl->job;
    try {
        if(job) {
            job->stop();
            job->join();
            // Only joined workers publish created/error or release QSaveFile.
            const bool removed = !job->created || !QFileInfo::exists(job->destination) || QFile::remove(job->destination);
            impl->job.reset();
            if(!removed) return false;
        }
        impl->stopSucceeded = true;
    } catch(...) {
        // Keep an unjoined job and its owner alive; refusal is permanently
        // cached, and no completion may masquerade as a successful fence.
    }
    return impl->stopSucceeded;
}

dcpp::HttpRequestPolicy transport_detail::policy(ResourceKind kind,
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route, Resolver resolver) {
    dcpp::HttpRequestPolicy result;
    result.route = std::move(route);
    result.limits.bodyBytes = kind == ResourceKind::Feed ? 1024ULL * 1024 : 512ULL * 1024 * 1024;
    if(!resolver) resolver = dcpp::resolveDirectEndpoint;
    result.prepare = [kind, resolver](const std::string& next, const std::string& previous,
        const dcpp::ProxyRouteSnapshot& route, uint32_t timeout, const std::function<bool()>& cancelled) {
        require(!cancelled() && route.valid && route.revoked && !route.revoked->load());
        const QUrl candidate = QUrl::fromEncoded(QByteArray::fromStdString(next), QUrl::StrictMode);
        const QUrl url = previous.empty() ? candidate : QUrl(QString::fromStdString(previous)).resolved(candidate);
        require(allowedUrl(url, kind, !previous.empty()));
        // Remote SOCKS resolution does not disclose a verifiable destination IP.
        // Until a route-aware resolver is available, do not query local DNS or dial.
        require(route.mode == dcpp::SettingsManager::OUTGOING_DIRECT && route.proxy.type == dcpp::Socket::StreamProxyConfig::None);
        const auto addresses = resolver(url.host().toStdString(), timeout, cancelled);
        require(!cancelled() && !route.revoked->load() && !addresses.empty());
        for(const auto& address : addresses) require(publicAddress(QString::fromStdString(address)));
        auto path = url.path(QUrl::FullyEncoded);
        if(url.hasQuery()) path += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
        return dcpp::HttpRequestPolicy::Target{url.toEncoded().toStdString(), url.host().toStdString(),
            "443", path.toStdString(), addresses.front()};
    };
    return result;
}
}
