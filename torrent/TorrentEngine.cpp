#include "TorrentEngine.h"
#include "TorrentSettings.h"
#include "SocksUdpBootstrap.h"
#include "TorrentPeerPolicy.h"
#include "SelectedRecheck.h"
#include "RecheckJournal.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QUrl>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#if EISKALT_SELECTED_RECHECK_AVAILABLE
#include <libtorrent/eiskalt_selected_recheck.hpp>
#endif
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace eiskalt::torrent {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr int maxJobs = 512;
constexpr int maxFiles = 10000;
constexpr int maxMetadata = 16 * 1024 * 1024;
class TorrentEngineText {
    Q_DECLARE_TR_FUNCTIONS(TorrentEngine)
};
QString text(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
QString socks5Failure(lt::operation_t operation, const lt::tcp::endpoint &endpoint, const lt::error_code &error) {
    const auto ip = text(endpoint.address().to_string());
    const auto address = (endpoint.address().is_v6() ? '[' + ip + ']' : ip) + ':' + QString::number(endpoint.port());
    if (operation == lt::operation_t::sock_read) {
        // libtorrent's UDP SOCKS5 relay posts sock_read when its established
        // TCP control channel closes, then schedules retry_connection().
        return TorrentEngineText::tr("Last SOCKS5 UDP relay warning (%1) at proxy %2: %3 [%4:%5]. The relay control connection closed; libtorrent will retry. DHT, UDP trackers and uTP may be interrupted; TCP peer connections may continue. Direct fallback remains disabled.")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate), address, text(error.message()),
                 QString::fromLatin1(error.category().name())).arg(error.value());
    }
    return TorrentEngineText::tr("Torrent SOCKS5 %1 failed at proxy %2: %3 [%4:%5]. Other connections may continue. Direct fallback remains disabled.")
        .arg(QString::fromLatin1(lt::operation_name(operation)), address, text(error.message()),
             QString::fromLatin1(error.category().name())).arg(error.value());
}

ProxyConfig effectiveProxy(const Settings &s, const ProxyConfig &p) {
    return s.proxyMode == ProxyMode::Direct ? ProxyConfig{} : p;
}
QString routeError(const Settings &s, const ProxyConfig &input) {
    const auto validation = validateSettings(s);
    if (!validation.isEmpty()) return validation;
    if (!s.enabled) return TorrentEngineText::tr("Torrent support is disabled.");
    const auto p = effectiveProxy(s, input);
    if (p.type == ProxyType::Direct) {
        if (s.proxyMode == ProxyMode::RequireProxy || s.proxyMode == ProxyMode::Custom)
            return TorrentEngineText::tr("Torrent requires a configured proxy. No direct fallback is allowed.");
        return {};
    }
    if (p.type != ProxyType::Socks5)
        return TorrentEngineText::tr("This Torrent proxy transport requires a ready SOCKS5 adapter. No direct fallback is allowed.");
    if (p.host.trimmed().isEmpty() || p.host.size() > 253 || p.host.contains(QChar(0)) ||
        p.host.contains('/') || p.host.contains('@') || p.host.contains(' ') ||
        p.port < 1 || p.port > 65535 || p.user.toUtf8().size() > 255 || p.password.toUtf8().size() > 255 ||
        p.user.contains(QChar(0)) || p.password.contains(QChar(0)) || (p.user.isEmpty() && !p.password.isEmpty()))
        return TorrentEngineText::tr("Invalid Torrent SOCKS5 endpoint or credentials.");
    if (!p.remoteDns) return TorrentEngineText::tr("Torrent proxy routing requires proxy-side hostname resolution.");
    if (!p.udp && !s.tcp) return TorrentEngineText::tr("This Torrent proxy has no UDP support; enable TCP.");
    return {};
}

lt::settings_pack sessionSettings(const Settings &s, const ProxyConfig &p) {
    lt::settings_pack out;
    using P = lt::settings_pack;
    const bool proxy = p.type == ProxyType::Socks5;
    const bool udp = !proxy || p.udp;
    out.set_str(P::user_agent, "EiskaltDC++ Torrent/1");
    out.set_int(P::alert_mask, static_cast<std::uint32_t>(lt::alert_category::error | lt::alert_category::status |
                                               lt::alert_category::storage));
    out.set_int(P::alert_queue_size, 2048);
    out.set_int(P::max_metadata_size, maxMetadata);
    out.set_int(P::max_piece_count, 524288);
    // WebRTC/STUN is outside this milestone's TCP/UDP proxy contract.
    out.set_int(P::max_webtorrent_offers, 0);
    out.set_str(P::webtorrent_stun_server, "");
    out.set_int(P::connections_limit, s.connectionLimit);
    out.set_int(P::active_downloads, s.activeDownloads);
    out.set_int(P::active_seeds, maxJobs);
    out.set_int(P::active_limit, maxJobs);
    out.set_int(P::download_rate_limit, s.downloadLimitKiB * 1024);
    out.set_int(P::upload_rate_limit, s.uploadLimitKiB * 1024);
    out.set_int(P::stop_tracker_timeout, 0);
    out.set_int(P::auto_manage_startup, 1);
    out.set_int(P::auto_manage_interval, 1);
    const int encryption = s.encryptionMode == EncryptionMode::Disabled ? P::pe_disabled :
        s.encryptionMode == EncryptionMode::Required ? P::pe_forced : P::pe_enabled;
    out.set_int(P::in_enc_policy, encryption);
    out.set_int(P::out_enc_policy, encryption);
    out.set_int(P::allowed_enc_level, s.encryptionMode == EncryptionMode::Required ? P::pe_rc4 : P::pe_both);
    // Keep both fixed and randomized ports stable rather than silently rebinding elsewhere.
    out.set_int(P::max_retry_port_bind, 0);
    out.set_bool(P::listen_system_port_fallback, false);
    out.set_bool(P::enable_dht, udp && s.dht);
    // An empty bootstrap list must really be empty, never the library's public defaults.
    auto bootstrap = s.bootstrapNodes.split(',', Qt::SkipEmptyParts);
    if (proxy) {
        // Libtorrent resolves bootstrap names locally, even with proxy_hostnames.
        // createSession obtains numeric endpoints via the SOCKS5 UDP relay first.
        // A Direct configuration permits only numeric normalization in this
        // helper, never any lookup. Do not retain Qt-tolerated whitespace.
        bootstrap = proxyBootstrapNodes(ProxyConfig{}, bootstrap);
    }
    out.set_str(P::dht_bootstrap_nodes, bootstrap.join(',').toStdString());
    out.set_bool(P::enable_lsd, !proxy && s.localDiscovery);
    out.set_bool(P::enable_upnp, !proxy && s.portMapping);
    out.set_bool(P::enable_natpmp, !proxy && s.portMapping);
    out.set_bool(P::enable_incoming_tcp, !proxy && s.tcp);
    out.set_bool(P::enable_outgoing_tcp, s.tcp);
    out.set_bool(P::enable_incoming_utp, !proxy && s.utp);
    out.set_bool(P::enable_outgoing_utp, udp && s.utp);
    out.set_bool(P::announce_to_all_trackers, false);
    out.set_bool(P::anonymous_mode, proxy);
    out.set_bool(P::proxy_peer_connections, true);
    out.set_bool(P::proxy_tracker_connections, true);
    out.set_bool(P::proxy_hostnames, true);
    out.set_bool(P::validate_https_trackers, true);
    QStringList binds, outgoing;
    if (!proxy) {
        if (!s.bindAddress.isEmpty()) {
            binds << s.bindAddress + ':' + QString::number(s.listenPort);
            outgoing << s.bindAddress;
        }
        if (!s.bindAddress6.isEmpty()) {
            binds << '[' + s.bindAddress6 + "]:" + QString::number(s.listenPort);
            outgoing << s.bindAddress6;
        }
    }
    out.set_str(P::listen_interfaces, binds.join(',').toStdString());
    out.set_str(P::outgoing_interfaces, outgoing.join(',').toStdString());
    out.set_int(P::proxy_type, proxy ? (p.user.isEmpty() ? P::socks5 : P::socks5_pw) : P::none);
    out.set_str(P::proxy_hostname, proxy ? p.host.toStdString() : "");
    out.set_int(P::proxy_port, proxy ? p.port : 0);
    out.set_str(P::proxy_username, proxy ? p.user.toStdString() : "");
    out.set_str(P::proxy_password, proxy ? p.password.toStdString() : "");
    return out;
}

bool safeRelative(const QString &path) {
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains('\\') &&
        !path.contains(QChar(0)) && !path.contains(':') &&
        !path.split('/').contains("..") && !path.split('/').contains(".");
}
QString storagePath(const QString &absolute) {
    QString existing = QDir::cleanPath(absolute);
    QStringList suffix;
    while (!QFileInfo::exists(existing)) {
        suffix.prepend(QFileInfo(existing).fileName());
        const auto parent = QFileInfo(existing).absolutePath();
        if (parent == existing) return absolute;
        existing = parent;
    }
    // Resolve the chosen root once (including macOS /var and /tmp aliases).
    // Torrent-supplied paths below this root must still pass noSymlinks().
    return QDir::cleanPath(QFileInfo(existing).canonicalFilePath() + '/' + suffix.join('/'));
}
bool noSymlinks(const QString &absolute) {
    if (!QDir::isAbsolutePath(absolute)) return false;
    QString path = QDir::cleanPath(absolute);
    while (true) {
        if (QFileInfo(path).isSymLink()) return false;
        const auto parent = QFileInfo(path).absolutePath();
        if (parent == path) return true;
        path = parent;
    }
}
bool safeMetadata(const lt::torrent_info &ti, const QString &root) {
    const auto &fs = ti.layout();
    if (fs.num_files() > maxFiles || !noSymlinks(root)) return false;
    for (auto index : fs.file_range()) {
        const auto path = text(fs.file_path(index));
        if ((fs.file_flags(index) & lt::file_storage::flag_symlink) || !safeRelative(path) ||
            !noSymlinks(QDir(root).filePath(path))) return false;
    }
    return true;
}
QString storageKey(QString path) {
    path = QDir::cleanPath(path);
#if defined(Q_OS_DARWIN)
    path = path.normalized(QString::NormalizationForm_D).toCaseFolded();
#elif defined(Q_OS_WIN)
    path = path.toCaseFolded();
#endif
    return path;
}
QString storageCollisionError() { return TorrentEngineText::tr("Torrent payload storage overlaps another job; remove this blocked job without deleting data."); }
lt::load_torrent_limits parseLimits() {
    lt::load_torrent_limits limits;
    limits.max_buffer_size = maxMetadata;
    limits.max_pieces = 524288;
    limits.max_decode_tokens = 500000;
    return limits;
}
QString metadataIdentity(const lt::torrent_info &metadata) {
    const auto hashes = metadata.info_hashes();
    return QString::fromLatin1(QByteArray(hashes.v1.data(), hashes.v1.size()).toHex()) + '/' +
        QString::fromLatin1(QByteArray(hashes.v2.data(), hashes.v2.size()).toHex());
}
QString checkStorageIdentity(const QString &root) {
    if (!noSymlinks(root)) return {};
#ifndef Q_OS_WIN
    struct stat info {};
    if (::stat(QFile::encodeName(root).constData(), &info) != 0 || !S_ISDIR(info.st_mode)) return {};
    const auto identity = root.toUtf8() + '\n' + QByteArray::number(quint64(info.st_dev)) + ':' +
        QByteArray::number(quint64(info.st_ino));
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
#else
    // Do not substitute timestamps for an authoritative directory identity.
    return {};
#endif
}
QString checkPayloadIdentity(const QString &root, const lt::torrent_info &metadata, const RecheckPlan &plan) {
#ifndef Q_OS_WIN
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QSet<QString> seen;
    for (int index : plan.affectedFiles) {
        QString path = QDir(root).filePath(text(metadata.layout().file_path(lt::file_index_t(index))));
        bool payload = true;
        for (;;) {
            if (!seen.contains(path)) {
                seen.insert(path);
                struct stat info {};
                QByteArray value = path.toUtf8() + '\n';
                if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
                    if (errno != ENOENT) return {};
                    value += "missing";
                } else {
                    if (S_ISLNK(info.st_mode) || (payload ? !S_ISREG(info.st_mode) : !S_ISDIR(info.st_mode))) return {};
                    value += QByteArray::number(quint64(info.st_dev)) + ':' + QByteArray::number(quint64(info.st_ino));
                    if (payload) {
#ifdef Q_OS_DARWIN
                        const auto modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
                        const auto modified = info.st_mtim, changed = info.st_ctim;
#endif
                        value += ':' + QByteArray::number(info.st_size) + ':' + QByteArray::number(modified.tv_sec) + ':' +
                            QByteArray::number(modified.tv_nsec) + ':' + QByteArray::number(changed.tv_sec) + ':' + QByteArray::number(changed.tv_nsec);
                    }
                }
                hash.addData(value + '\n');
            }
            if (path == root) break;
            const auto parent = QFileInfo(path).absolutePath();
            if (parent == path) return {};
            path = parent;
            payload = false;
        }
    }
    return QString::fromLatin1(hash.result().toHex());
#else
    return {};
#endif
}
}

struct TorrentEngine::Impl {
    struct Record {
        QString id;
        quint64 revision = 0;
        lt::torrent_handle handle;
        lt::add_torrent_params resume;
        lt::torrent_status status;
        QList<FileEntry> files;
        std::optional<QList<int>> wanted;
        QMap<int, int> filePriorities;
        QString savePath, failure, moveAttemptedTarget;
        bool userPaused = false, seedStopped = false, notified = false;
        bool userStopped = false;
        bool dcShareExcluded = false;
        bool moving = false, selectionPending = false;
        bool ownsStorage = false, storageCollision = false;
        bool checking = false, savePending = false, saveAgain = false;
        quint64 resumeGeneration = 1, resumeSnapshotGeneration = 1;
        quint64 requestedSaveGeneration = 0, durableResumeGeneration = 0;
        FileRecheckSnapshot fileCheck;
        std::optional<RecheckPlan> checkPlan;
        std::optional<RecheckRecord> checkRecord;
        std::unique_ptr<lt::session> checkSession;
        QString checkIdentity, checkPayload;
        bool checkStarted = false, checkTerminal = false, checkSaveRequested = false;
        bool deferred = false, sourceReadOnly = false;
        qint64 observedUpload = 0, uploadBaseline = 0, lastPayloadUpload = 0;
        int downloadingPeers = 0, uploadingPeers = 0;
    };
    TorrentEngine *q;
    QString stateDir;
    RecheckJournal journal;
    const CountryLookup countryLookup;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> commands;
    Settings apiSettings;
    QString apiError = tr("Torrent routing has not been configured.");
    QList<Job> snapshots;
    QString observedJobId;
    QList<Peer> peerSnapshots;
    quint64 peerObservationEpoch = 0;
    QStringList listenerSnapshots;
    QHash<QString, QList<FileEntry>> fileSnapshots;
    QHash<QString, std::shared_ptr<const lt::torrent_info>> metadataSnapshots;
    QSet<QString> checkAdmissions, mutationBusy;
    quint64 checkSerial = 0;
    QHash<QString, quint64> revisions;
    struct PendingActivity { quint64 serial; bool paused, stopped; };
    QHash<QString, PendingActivity> pendingActivities;
    quint64 activitySerial = 0;
    quint64 configurationEpoch = 0;
    bool stopped = false;
    // Written by the worker, read by the owner only after join.
    bool shutdownSaved = false;
    std::thread worker;
    // Everything below is confined to worker, including session destruction.
    Settings config;
    const int randomizedPort = QRandomGenerator::global()->bounded(49152, 65536);
    quint64 listenerEpoch = 0;
    ProxyConfig proxy;
    QString blocked;
    bool loaded = false, closing = false, dirty = false, closingCheckpointOk = true;
    std::unique_ptr<lt::session> session;
    std::shared_ptr<PeerPolicy> peerPolicy;
    QStringList pendingBootstrap;
    Clock::time_point nextBootstrapRetry = Clock::time_point::max();
    std::chrono::seconds bootstrapRetryDelay{30};
    std::map<QString, Record> records;
    // Claims include unselected payload and both sides of an in-flight move.
    std::map<QString, QString> storageOwners;
    struct Removal {
        QString id;
        lt::torrent_handle handle;
        lt::info_hash_t hashes;
        bool deleting = false, deletionFinished = false;
    };
    std::vector<Removal> removals;
    Clock::time_point nextSnapshot = Clock::now(), nextSave = Clock::now(), nextPolicyStatus = Clock::now();
    Clock::time_point nextPeerSnapshot = Clock::now();
    Clock::time_point nextFileCheck = Clock::now();

    Impl(TorrentEngine *owner, QString directory, CountryLookup lookup) : q(owner), stateDir(std::move(directory)), journal(stateDir), countryLookup(std::move(lookup)) {
        worker = std::thread([this] { run(); });
    }
    ~Impl() { stop(); }
    void report(const QString &message) { emit q->error(message); }
    void reportSocks5(lt::operation_t operation, const lt::tcp::endpoint &endpoint, const lt::error_code &error) {
        const auto message = socks5Failure(operation, endpoint, error);
        if (operation == lt::operation_t::sock_read)
            emit q->diagnostic(message);
        else
            report(message);
    }
    void clearListeners() {
        {
            std::lock_guard lock(mutex);
            if (listenerSnapshots.isEmpty()) return;
            listenerSnapshots.clear();
        }
        emit q->listenersChanged();
    }
    void listenerSucceeded(const lt::address &address, int port, lt::socket_type_t type) {
        if (closing || proxy.type != ProxyType::Direct || port <= 0 ||
            (type != lt::socket_type_t::tcp && type != lt::socket_type_t::utp)) return;
        const auto host = text(address.to_string());
        const auto endpoint = (type == lt::socket_type_t::tcp ? QStringLiteral("TCP ") : QStringLiteral("UDP ")) +
            (address.is_v6() ? '[' + host + ']' : host) + ':' + QString::number(port);
        {
            std::lock_guard lock(mutex);
            // Public configure invalidates immediately, before the old session is joined.
            if (listenerEpoch != configurationEpoch || stopped) return;
            if (listenerSnapshots.contains(endpoint)) return;
            listenerSnapshots.append(endpoint);
            listenerSnapshots.sort();
        }
        emit q->listenersChanged();
    }
    void listenerFailed(lt::operation_t operation) {
        // These alerts are not close notifications: e.g. an accept failure can
        // leave its acceptor open. Preserve last-confirmed bind history.
        report(operation == lt::operation_t::sock_accept ? tr("Torrent could not accept an incoming connection.") :
            tr("Torrent could not bind a configured listening endpoint."));
    }
    bool enqueue(std::function<void()> command) {
        {
            std::lock_guard lock(mutex);
            if (stopped || commands.size() >= 1024) return false;
            commands.push_back(std::move(command));
        }
        wake.notify_one();
        return true;
    }
    quint64 invalidate(const QString &id, bool removing = false) {
        std::lock_guard lock(mutex);
        const auto revision = ++revisions[id];
        for (auto &job : snapshots) if (job.id == id) {
            job.revision = revision;
            job.complete = false;
        }
        if (removing) {
            snapshots.removeIf([&](const Job &job) { return job.id == id; });
            fileSnapshots.remove(id);
            metadataSnapshots.remove(id);
            mutationBusy.remove(id);
            if (observedJobId == id) {
                observedJobId.clear();
                peerSnapshots.clear();
                ++peerObservationEpoch;
            }
        }
        return revision;
    }
    bool stop() {
        {
            std::lock_guard lock(mutex);
            stopped = true;
            observedJobId.clear();
            peerSnapshots.clear();
            ++peerObservationEpoch;
        }
        wake.notify_one();
        if (worker.joinable()) worker.join();
        return shutdownSaved;
    }
    void run() {
        for (;;) {
            std::function<void()> command;
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, session ? 25ms : 250ms, [&] { return stopped || !commands.empty(); });
                if (!commands.empty()) {
                    command = std::move(commands.front());
                    commands.pop_front();
                } else if (stopped) break;
            }
            try {
                if (command) command();
                pollFileChecks();
                if (session) {
                    alerts();
                    reapRemovals();
                    const auto now = Clock::now();
                    if (now >= nextPeerSnapshot) {
                        // get_peer_info synchronizes with libtorrent: never call
                        // it on the GUI thread or while holding the cache mutex.
                        nextPeerSnapshot = now + 1s;
                        collectPeers();
                    }
                    if (now >= nextSnapshot) {
                        session->post_torrent_updates(lt::torrent_handle::query_name | lt::torrent_handle::query_save_path);
                        publish();
                        nextSnapshot = now + 250ms;
                    }
                    if (now >= nextSave) {
                        requestSaves();
                        nextSave = now + 30s;
                    }
                    if (now >= nextPolicyStatus) {
                        // Idle seeds don't necessarily produce changed-state alerts.
                        // Bounded asynchronous refreshes keep time/ratio policy live.
                        for (auto &[id, r] : records) if (r.handle.is_valid() && r.status.is_finished)
                            r.handle.post_status(lt::torrent_handle::query_name | lt::torrent_handle::query_save_path);
                        nextPolicyStatus = now + 1s;
                    }
                    if (now >= nextBootstrapRetry) retryBootstrap();
                }
                if (dirty) { persist(); dirty = false; }
            } catch (...) {
                report(tr("Torrent backend operation failed; details were withheld to protect credentials."));
            }
        }
        try {
            shutdownSaved = closeSession();
            if (loaded) shutdownSaved = persist() && shutdownSaved;
        } catch (...) {
            shutdownSaved = false;
            report(tr("Torrent shutdown persistence failed."));
        }
    }
    bool bootstrapCancelled() {
        std::lock_guard lock(mutex);
        return stopped || listenerEpoch != configurationEpoch;
    }
    void retryBootstrap() {
        if (!session || pendingBootstrap.isEmpty() || bootstrapCancelled()) return;
        const auto resolved = proxyBootstrapNodes(proxy, pendingBootstrap,
            [this] { return bootstrapCancelled(); }, 1000);
        if (bootstrapCancelled()) return;
        if (resolved.isEmpty()) {
            bootstrapRetryDelay = std::min(bootstrapRetryDelay * 2, 300s);
            nextBootstrapRetry = Clock::now() + bootstrapRetryDelay;
            return;
        }
        // Recover without restarting transfers; only validated numeric endpoints
        // enter libtorrent, and the configured hostnames remain unchanged on disk.
        lt::settings_pack update;
        update.set_str(lt::settings_pack::dht_bootstrap_nodes, resolved.join(',').toStdString());
        session->apply_settings(update);
        pendingBootstrap.clear();
        nextBootstrapRetry = Clock::time_point::max();
    }
    bool createSession() {
        if (session) return true;
        if (!blocked.isEmpty()) return false;
        // This is the only session construction site. Routing is complete first.
        Settings runtime = config;
        if (runtime.randomizePort) runtime.listenPort = randomizedPort;
        if (proxy.type == ProxyType::Socks5 && proxy.udp && runtime.dht) {
            const auto cancelled = [this] { return bootstrapCancelled(); };
            const auto nodes = runtime.bootstrapNodes.split(',', Qt::SkipEmptyParts);
            const auto resolved = proxyBootstrapNodes(proxy, nodes, cancelled);
            if (cancelled()) return false;
            runtime.bootstrapNodes = resolved.join(',');
            if (!nodes.isEmpty() && resolved.isEmpty()) {
                pendingBootstrap = nodes;
                bootstrapRetryDelay = 30s;
                nextBootstrapRetry = Clock::now() + bootstrapRetryDelay;
                report(tr("DHT bootstrap through SOCKS5 failed. Check UDP support and the bootstrap nodes; no direct fallback was used."));
            }
        }
        session = std::make_unique<lt::session>(lt::session_params(sessionSettings(runtime, proxy)));
        peerPolicy = std::make_shared<PeerPolicy>(config, countryLookup);
        session->add_extension([policy = peerPolicy](const lt::torrent_handle &, lt::client_data_t) {
            return std::make_shared<SecurityTorrentPlugin>(policy);
        });
        if (proxy.type == ProxyType::Socks5 && !proxy.udp)
            emit q->diagnostic(tr("Torrent proxy has no UDP support: DHT, uTP and UDP trackers are disabled."));
        return true;
    }
    void configure(Settings settings, ProxyConfig input, QString error, quint64 epoch,
                   const QHash<QString, quint64> &configureRevisions) {
        closeSession();
        {
            std::lock_guard lock(mutex);
            if (epoch != configurationEpoch) return;
        }
        config = std::move(settings);
        listenerEpoch = epoch;
        if (!config.completedPath.isEmpty()) config.completedPath = storagePath(config.completedPath);
        proxy = effectiveProxy(config, input);
        blocked = std::move(error);
        QString saveError;
        if (validateSettings(config).isEmpty() && !saveSettings(QDir(stateDir).filePath("settings.json"), config, &saveError))
            report(saveError);
        if (!loaded) restore();
        {
            std::lock_guard lock(mutex);
            for (auto &[id, record] : records) record.revision = configureRevisions.value(id);
        }
        if (blocked.isEmpty()) {
            for (auto &[id, record] : records) {
                if (!record.failure.isEmpty()) continue;
                start(record);
            }
        }
        publish();
        {
            std::lock_guard lock(mutex);
            if (epoch != configurationEpoch) return;
        }
        emit q->configurationApplied(epoch);
    }
    bool invalidateResume(Record &r) {
        if (r.resumeGeneration == std::numeric_limits<quint64>::max()) {
            fail(r, tr("Torrent verification generation is exhausted; remove and add the job again without deleting data."));
            return false;
        }
        ++r.resumeGeneration;
        return true;
    }
    void requestSave(Record &r) {
        if (!r.handle.is_valid()) return;
        if (r.fileCheck.active && !r.checkTerminal) return;
        if (r.savePending) { r.saveAgain = true; return; }
        r.savePending = true;
        r.requestedSaveGeneration = r.resumeGeneration;
        r.handle.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::flush_disk_cache);
    }
    void requestSaves() { for (auto &[id, r] : records) requestSave(r); }
    bool hasFileCheck(const QString &id) const {
        std::lock_guard lock(mutex);
        if (checkAdmissions.contains(id)) return true;
        return std::any_of(snapshots.cbegin(), snapshots.cend(), [&](const Job &job) {
            return job.id == id && job.fileCheck.active;
        });
    }
    void failFileCheck(Record &r, const QString &reason) {
        r.fileCheck.error = reason;
        r.userPaused = true;
        r.fileCheck.active = false;
        r.notified = false;
        for (auto &file : r.fileCheck.files) if (file.outcome == FileCheckOutcome::Pending)
            file.outcome = FileCheckOutcome::Unavailable;
#if EISKALT_SELECTED_RECHECK_AVAILABLE
        if (r.handle.is_valid()) {
            lt::cancel_selected_recheck(r.handle, r.fileCheck.token);
            lt::finish_selected_recheck(r.handle, r.fileCheck.token, false);
        }
#endif
        fail(r, reason);
        if (r.checkSession) { r.handle = {}; r.checkSession.reset(); }
        r.checkPlan.reset();
        dirty = true;
    }
    void cancelCheck(Record &r) {
        if (!r.fileCheck.active) return;
        r.fileCheck.canceling = true;
#if EISKALT_SELECTED_RECHECK_AVAILABLE
        // A cancel before admission still needs the durable scope and native
        // invalidation, otherwise unchecked old seed bits could survive it.
        if (r.checkStarted && !r.checkTerminal)
            lt::cancel_selected_recheck(r.handle, r.fileCheck.token);
#endif
    }
    void beginFileCheck(Record &r, RecheckPlan plan, quint64 token, quint64 revision) {
        r.revision = revision;
        r.fileCheck = {token, true, false, 0, plan.totalBytes, {}, {}};
        for (int index : plan.selectedFiles) r.fileCheck.files.append({index, FileCheckOutcome::Pending, {}});
        r.checkPlan = std::move(plan);
        r.checkStarted = r.checkTerminal = r.checkSaveRequested = false;
        r.checkPayload.clear();
        r.notified = false;
        const auto metadata = r.handle.is_valid() ? r.handle.torrent_file() : r.resume.ti;
        r.checkIdentity = checkStorageIdentity(r.savePath);
        if (!metadata || !safeMetadata(*metadata, r.savePath) || r.checkIdentity.isEmpty() ||
            r.storageCollision || r.moving || r.selectionPending || r.checking || r.checkRecord) {
            failFileCheck(r, tr("Selected-file verification conflicts with another operation or unsafe storage."));
            publish();
            return;
        }
        if (!invalidateResume(r)) { failFileCheck(r, r.failure); publish(); return; }
        r.failure.clear();
        if (!r.resume.ti) {
            r.resume.ti = std::make_shared<lt::torrent_info>(*metadata);
            if (!persist()) { failFileCheck(r, tr("Cannot save Torrent metadata before file verification.")); publish(); return; }
        }
#if EISKALT_SELECTED_RECHECK_AVAILABLE
        if (!r.handle.is_valid()) {
            auto settings = sessionSettings(config, {});
            using P = lt::settings_pack;
            settings.set_str(P::listen_interfaces, "");
            settings.set_str(P::outgoing_interfaces, "");
            settings.set_str(P::dht_bootstrap_nodes, "");
            for (const int option : {P::enable_dht, P::enable_lsd, P::enable_upnp, P::enable_natpmp,
                    P::enable_incoming_tcp, P::enable_outgoing_tcp, P::enable_incoming_utp, P::enable_outgoing_utp})
                settings.set_bool(option, false);
            r.checkSession = std::make_unique<lt::session>(lt::session_params(settings));
            auto params = r.resume;
            params.save_path = r.savePath.toStdString();
            params.file_priorities = priorities(r, *metadata);
            params.trackers.clear(); params.tracker_tiers.clear(); params.dht_nodes.clear(); params.url_seeds.clear();
#if TORRENT_ABI_VERSION < 4
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            params.http_seeds.clear();
            QT_WARNING_POP
#endif
            params.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::seed_mode);
            params.flags |= lt::torrent_flags::paused | lt::torrent_flags::no_verify_files |
                lt::torrent_flags::disable_dht | lt::torrent_flags::disable_lsd | lt::torrent_flags::disable_pex;
            if (r.sourceReadOnly) params.flags |= lt::torrent_flags::upload_mode;
            lt::error_code error;
            r.handle = r.checkSession->add_torrent(params, error);
            if (error) { failFileCheck(r, tr("Cannot prepare local-only Torrent verification.")); publish(); return; }
            // Startup is metadata-only; preparation is attempted once native
            // resume initialization has reached the ready paused state.
            r.status = r.handle.status();
        } else lt::prepare_selected_recheck(r.handle, token);
#endif
        publish();
    }
    void updateCheckResults(Record &r, const lt::torrent_info &metadata,
#if EISKALT_SELECTED_RECHECK_AVAILABLE
                            const lt::selected_recheck_status &native
#else
                            int
#endif
    ) {
#if EISKALT_SELECTED_RECHECK_AVAILABLE
        r.fileCheck.checkedBytes = native.checked_bytes;
        const auto &files = metadata.layout();
        QSet<int> passed, failedPieces, unavailablePieces;
        for (const auto piece : native.passed) passed.insert(int(piece));
        for (const auto piece : native.failed) failedPieces.insert(int(piece));
        for (const auto piece : native.unavailable) unavailablePieces.insert(int(piece));
        for (auto &file : r.fileCheck.files) {
            const auto index = lt::file_index_t(file.index);
            bool failed = false, unavailable = false, pending = false;
            if (files.file_size(index) != 0) {
                const int first = int(files.piece_index_at_file(index)), last = int(files.last_piece_index_at_file(index));
                for (int p = first; p <= last; ++p) {
                    const bool bad = failedPieces.contains(p), missing = unavailablePieces.contains(p);
                    failed |= bad;
                    unavailable |= missing;
                    pending |= !passed.contains(p) && !bad && !missing;
                }
            }
            file.outcome = failed ? FileCheckOutcome::FailedPiece : unavailable || (r.checkTerminal && pending)
                ? (native.phase == lt::selected_recheck_phase::canceled ? FileCheckOutcome::Canceled : FileCheckOutcome::Unavailable)
                : pending ? FileCheckOutcome::Pending : FileCheckOutcome::Verified;
            if (file.outcome == FileCheckOutcome::FailedPiece)
                file.detail = tr("An affected piece is missing or corrupt; shared boundaries may include neighboring files.");
            else if (file.outcome == FileCheckOutcome::Canceled)
                file.detail = tr("Unchecked affected pieces remain unavailable after cancellation.");
            else if (file.outcome == FileCheckOutcome::Unavailable)
                file.detail = tr("Required storage or authenticated hashes were unavailable.");
        }
#endif
    }
    void pollFileChecks(bool force = false) {
#if EISKALT_SELECTED_RECHECK_AVAILABLE
        const auto now = Clock::now();
        if (!force && now < nextFileCheck) return;
        nextFileCheck = now + 250ms;
        bool changed = false;
        for (auto &[id, r] : records) {
            if (!r.fileCheck.active || !r.checkPlan) continue;
            if (r.checkSession) alerts(r.checkSession.get());
            if (!r.fileCheck.active) { changed = true; continue; }
            const auto metadata = r.handle.torrent_file();
            if (!metadata) {
                failFileCheck(r, tr("Torrent metadata disappeared during verification; recovery remains required."));
                changed = true; continue;
            }
            if (checkStorageIdentity(r.savePath) != r.checkIdentity || !safeMetadata(*metadata, r.savePath) ||
                (r.checkStarted && checkPayloadIdentity(r.savePath, *metadata, *r.checkPlan) != r.checkPayload)) {
                failFileCheck(r, tr("Torrent storage changed during verification; recovery remains required."));
                changed = true; continue;
            }
            if (r.checkSession && !r.checkStarted) {
                const auto state = r.handle.status();
                if (state.state == lt::torrent_status::checking_resume_data) continue;
                const auto before = lt::query_selected_recheck(r.handle, r.fileCheck.token);
                if (before.error && before.phase == lt::selected_recheck_phase::failed)
                    lt::prepare_selected_recheck(r.handle, r.fileCheck.token);
            }
            const auto native = lt::query_selected_recheck(r.handle, r.fileCheck.token);
            if (native.token != r.fileCheck.token) continue;
            if (native.phase == lt::selected_recheck_phase::quiesced && !r.checkStarted) {
                if (!r.fileCheck.error.isEmpty()) { failFileCheck(r, r.fileCheck.error); changed = true; continue; }
                // Cached writes are drained now. Bind all actual boundary files
                // before reads, so a renamed/replaced inode cannot yield trust
                // for a different path through an older open descriptor.
                r.checkPayload = checkPayloadIdentity(r.savePath, *metadata, *r.checkPlan);
                if (r.checkPayload.isEmpty()) {
                    failFileCheck(r, tr("Torrent payload identity could not be validated before verification."));
                    changed = true; continue;
                }
                RecheckRecord record{id, metadataIdentity(*metadata), r.checkIdentity, r.fileCheck.token,
                    r.revision, r.resumeGeneration, r.checkPlan->selectedFiles, r.checkPlan->pieces, r.sourceReadOnly};
                QString error;
                if (!journal.begin(record, &error)) { failFileCheck(r, error); changed = true; continue; }
                r.checkRecord = std::move(record);
                lt::selected_recheck_request request;
                request.token = r.fileCheck.token;
                for (int piece : r.checkPlan->pieces) request.pieces.emplace_back(piece);
                r.checkStarted = true;
                lt::start_selected_recheck(r.handle, std::move(request));
                if (r.fileCheck.canceling) lt::cancel_selected_recheck(r.handle, r.fileCheck.token);
            } else if (native.phase == lt::selected_recheck_phase::complete ||
                       native.phase == lt::selected_recheck_phase::canceled || native.phase == lt::selected_recheck_phase::failed) {
                r.checkTerminal = true;
                if (native.error) r.fileCheck.error = tr("Selected-file verification encountered a storage or metadata error.");
                updateCheckResults(r, *metadata, native);
                if (!r.checkRecord) { failFileCheck(r, r.fileCheck.error); changed = true; continue; }
                if (!r.checkSaveRequested) {
                    // Capture newer owner-accepted activity before the final
                    // snapshot is committed, without undoing a pending Stop.
                    {
                        std::lock_guard lock(mutex);
                        const auto pending = pendingActivities.constFind(id);
                        if (pending != pendingActivities.cend()) { r.userPaused = pending->paused; r.userStopped = pending->stopped; }
                    }
                    if (!r.fileCheck.error.isEmpty()) r.userPaused = true;
                    r.checkSaveRequested = true;
                    requestSave(r);
                } else if (!r.savePending && r.resumeSnapshotGeneration == r.checkRecord->resumeGeneration) {
                    QString error;
                    if (!persist() || !journal.finish(id, r.fileCheck.token, r.durableResumeGeneration, &error)) {
                        failFileCheck(r, error.isEmpty() ? tr("Cannot commit selected-file verification; recovery remains required.") : error);
                        changed = true; continue;
                    }
                    r.checkRecord.reset();
                    lt::finish_selected_recheck(r.handle, r.fileCheck.token, false);
                    r.fileCheck.active = false;
                    r.checkPlan.reset();
                    r.status = r.handle.status(lt::torrent_handle::query_name | lt::torrent_handle::query_save_path);
                    if (!r.fileCheck.error.isEmpty()) fail(r, r.fileCheck.error);
                    bool hold;
                    { std::lock_guard lock(mutex); const auto pending = pendingActivities.constFind(id);
                      hold = pending != pendingActivities.cend() && (pending->paused || pending->stopped); }
                    const bool restoreActivity = !closing && !hold && !r.userPaused && !r.userStopped && !r.seedStopped &&
                        r.failure.isEmpty() && (!r.wanted || !r.wanted->isEmpty());
                    if (r.checkSession) {
                        r.handle = {}; r.checkSession.reset();
                        if (restoreActivity) { r.deferred = false; start(r); }
                    }
                    else if (restoreActivity) resume(r);
                    dirty = true;
                }
            } else updateCheckResults(r, *metadata, native);
            changed = true;
        }
        if (changed) publish();
#else
        (void)force;
#endif
    }
    bool drainFileChecks() {
        for (auto &[id, r] : records) cancelCheck(r);
        const auto deadline = Clock::now() + 3s;
        while (std::any_of(records.begin(), records.end(), [](const auto &entry) { return entry.second.fileCheck.active; })) {
            if (session) alerts();
            pollFileChecks(true);
            if (Clock::now() >= deadline) return false;
            std::this_thread::sleep_for(20ms);
        }
        return std::none_of(records.begin(), records.end(), [](const auto &entry) { return bool(entry.second.checkRecord); });
    }
    bool closeSession() {
        pendingBootstrap.clear();
        nextBootstrapRetry = Clock::time_point::max();
        closing = true;
        const bool checksOk = drainFileChecks();
        if (!checksOk) for (auto &[id, r] : records) if (r.fileCheck.active)
            failFileCheck(r, tr("Verification drain timed out; the Torrent remains paused and requires recovery."));
        if (!session) {
            for (auto &[id, r] : records) if (r.checkSession) {
                if (r.fileCheck.active)
                    failFileCheck(r, tr("Local verification drain timed out; the Torrent requires recovery."));
                r.handle = {};
                r.checkSession.reset();
                r.savePending = r.saveAgain = false;
                r.requestedSaveGeneration = 0;
                r.fileCheck.active = false;
                r.notified = false;
            }
            closing = false;
            const bool committed = persist();
            return checksOk && committed;
        }
        session->pause();
        const auto drainSaves = [&] {
            const auto deadline = Clock::now() + 3s;
            for (;;) {
                alerts();
                if (std::none_of(records.begin(), records.end(), [](const auto &entry) { return entry.second.savePending; })) return true;
                if (Clock::now() >= deadline) return false;
                session->wait_for_alert(lt::milliseconds(20));
            }
        };
        // An older in-flight reply cannot stand in for the final paused checkpoint.
        bool checkpointOk = drainSaves();
        if (checkpointOk) {
            closingCheckpointOk = true;
            requestSaves();
            checkpointOk = drainSaves() && closingCheckpointOk;
        }
        for (auto &[id, r] : records) {
            if (r.savePending) report(tr("Torrent resume checkpoint timed out; the last atomic checkpoint was retained."));
            r.savePending = false;
            r.saveAgain = false;
            r.requestedSaveGeneration = 0;
            r.handle = {};
            r.checkSession.reset();
            r.fileCheck.active = false;
            r.notified = false;
            r.downloadingPeers = r.uploadingPeers = 0;
        }
        // Joining libtorrent here closes every old route before another can start.
        session.reset();
        clearListeners();
        for (const auto &removal : removals) releaseStorage(removal.id);
        removals.clear();
        closing = false;
        const bool committed = persist();
        return checksOk && checkpointOk && committed;
    }
    bool persist() {
        if (!loaded) return true;
        QJsonArray entries;
        try {
            for (const auto &[id, r] : records) {
                auto params = r.resume;
                params.total_uploaded = std::max(params.total_uploaded, std::int64_t(r.observedUpload));
                params.save_path = r.savePath.toStdString();
                const auto bytes = lt::write_resume_data_buf(params);
                QJsonObject item{{"id", id}, {"resume", QString::fromLatin1(QByteArray(bytes.data(), qsizetype(bytes.size())).toBase64())},
                                 {"resumeGeneration", QString::number(r.resumeSnapshotGeneration)},
                                 {"paused", r.userPaused}, {"seedStopped", r.seedStopped}, {"deferred", r.deferred},
                                 {"stopped", r.userStopped},
                                 {"dcShareExcluded", r.dcShareExcluded},
                                 {"sourceReadOnly", r.sourceReadOnly},
                                 {"ownsStorage", r.ownsStorage}, {"storageCollision", r.storageCollision}};
                if (r.wanted) {
                    QJsonArray wanted;
                    for (int index : *r.wanted) wanted.append(index);
                    item.insert("wanted", wanted);
                }
                QJsonObject priorities;
                for (auto it = r.filePriorities.cbegin(); it != r.filePriorities.cend(); ++it)
                    priorities.insert(QString::number(it.key()), it.value());
                item.insert("filePriorities", priorities);
                if (r.fileCheck.recoveryRequired) {
                    QJsonArray files;
                    for (const auto &result : r.fileCheck.files) files.append(result.index);
                    item.insert("fileCheckRecovery", files);
                }
                entries.append(item);
            }
        } catch (...) { report(tr("Cannot encode Torrent resume state.")); return false; }
        if (!QDir().mkpath(stateDir)) { report(tr("Cannot create Torrent state directory.")); return false; }
        QFile::setPermissions(stateDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QSaveFile file(QDir(stateDir).filePath("jobs.json"));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) { report(tr("Cannot open Torrent resume state for atomic writing.")); return false; }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const auto bytes = QJsonDocument(QJsonObject{{"version", 2}, {"jobs", entries}}).toJson(QJsonDocument::Compact);
        if (file.write(bytes) != bytes.size() || !file.commit()) { report(tr("Cannot commit Torrent resume state.")); return false; }
        for (auto &[id, r] : records) r.durableResumeGeneration = r.resumeSnapshotGeneration;
        return true;
    }
    void restore() {
        loaded = true;
        QFile file(QDir(stateDir).filePath("jobs.json"));
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024) {
            report(tr("Cannot read bounded Torrent resume state.")); return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        const int version = doc["version"].toInt();
        if (error.error != QJsonParseError::NoError || !doc.isObject() || (version != 1 && version != 2) ||
            !doc["jobs"].isArray() || doc["jobs"].toArray().size() > maxJobs) {
            report(tr("Invalid Torrent resume state; no jobs were started.")); return;
        }
        for (const auto &value : doc["jobs"].toArray()) {
            const auto item = value.toObject();
            quint64 generation = 1;
            if (item.contains("resumeGeneration")) {
                bool valid = false;
                const auto encoded = item["resumeGeneration"].toString();
                generation = encoded.toULongLong(&valid);
                if (!item["resumeGeneration"].isString() || !valid || generation == 0 ||
                    QString::number(generation) != encoded) {
                    report(tr("Invalid verification generation in Torrent resume state; the job was skipped."));
                    continue;
                }
            }
            if ((version == 2 || item.contains("sourceReadOnly")) && !item["sourceReadOnly"].isBool()) {
                report(tr("Invalid source protection in Torrent resume state; the job was skipped.")); continue;
            }
            const QString id = item["id"].toString();
            const auto bytes = QByteArray::fromBase64(item["resume"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
            lt::error_code ec;
            auto params = lt::read_resume_data(lt::span<char const>(bytes.constData(), bytes.size()), ec, parseLimits());
            if (QUuid(id).isNull() || records.contains(id) || ec || !QDir::isAbsolutePath(text(params.save_path)) ||
                !params.renamed_files.empty() || (item["sourceReadOnly"].toBool() && !params.ti) ||
                (!params.ti && !params.info_hashes.has_v1() && !params.info_hashes.has_v2())) {
                report(tr("An invalid Torrent resume entry was skipped.")); continue;
            }
            Record r;
            r.id = id;
            r.resumeGeneration = r.resumeSnapshotGeneration = r.durableResumeGeneration = generation;
            r.resume = std::move(params);
            r.savePath = text(r.resume.save_path);
            r.userPaused = item["paused"].toBool();
            r.userStopped = item["stopped"].toBool();
            // Legacy absence follows global settings; malformed values never opt in.
            r.dcShareExcluded = item.contains("dcShareExcluded") &&
                (!item["dcShareExcluded"].isBool() || item["dcShareExcluded"].toBool());
            r.userPaused |= r.userStopped;
            const auto savedPriorities = item["filePriorities"].toObject();
            for (auto priority = savedPriorities.begin(); priority != savedPriorities.end(); ++priority) {
                bool validIndex = false;
                const int index = priority.key().toInt(&validIndex);
                const int value = priority.value().toInt(-1);
                if (validIndex && index >= 0 && index < maxFiles &&
                    (value == 1 || value == 4 || value == 7) && priority.value().toDouble() == value)
                    r.filePriorities.insert(index, value);
            }
            r.seedStopped = item["seedStopped"].toBool();
            r.deferred = item["deferred"].toBool();
            r.sourceReadOnly = item["sourceReadOnly"].toBool();
            r.ownsStorage = item["ownsStorage"].toBool();
            r.storageCollision = item["storageCollision"].toBool();
            if (r.storageCollision) r.failure = storageCollisionError();
            if (item.contains("wanted")) {
                r.wanted = QList<int>{};
                if (!item["wanted"].isArray()) { report(tr("Invalid Torrent file selection in resume state.")); continue; }
                for (const auto &index : item["wanted"].toArray()) {
                    if (!index.isDouble() || index.toInt(-1) < 0 || index.toDouble() != index.toInt()) {
                        r.failure = tr("Invalid Torrent file selection in resume state."); break;
                    }
                    r.wanted->append(index.toInt());
                }
            }
            if (item.contains("fileCheckRecovery")) {
                const auto files = item["fileCheckRecovery"].toArray();
                bool valid = item["fileCheckRecovery"].isArray() && !files.isEmpty() && files.size() <= maxFiles;
                int previous = -1;
                for (const auto &index : files) {
                    const int i = index.toInt(-1);
                    valid &= index.isDouble() && i > previous && i < maxFiles && index.toDouble() == i;
                    previous = i;
                    r.fileCheck.files.append({i, FileCheckOutcome::Unavailable, {}});
                }
                if (!valid) { report(tr("Invalid selected-file recovery state; the job was skipped.")); continue; }
                r.fileCheck.recoveryRequired = true;
            }
            records.emplace(id, std::move(r));
        }
        // Rebuild established ownership before starting any newly restored jobs.
        for (auto &[id, r] : records) if (r.ownsStorage && r.resume.ti) {
            if (safeMetadata(*r.resume.ti, r.savePath)) claimStorage(r, *r.resume.ti, r.savePath);
            else fail(r, tr("Unsafe Torrent storage path or symlink metadata was rejected."));
        }
        for (auto &[id, r] : records) reconcileFileCheck(r);
    }
    void reconcileFileCheck(Record &r) {
        QString error;
        const auto record = journal.pending(r.id, &error);
        if (!record && error.isEmpty() && !r.fileCheck.recoveryRequired) return;
        r.userPaused = r.deferred = true;
        r.notified = false;
        const auto block = [&](const QString &reason) {
            r.fileCheck.error = reason;
            r.fileCheck.recoveryRequired = false;
            fail(r, reason);
        };
        if (!error.isEmpty()) { block(error); return; }
        const auto metadata = r.resume.ti;
        QList<int> selected;
        if (record) selected = record->selectedFiles;
        else for (const auto &file : r.fileCheck.files) selected.append(file.index);
        const auto plan = metadata ? planSelectedRecheck(*metadata, selected, r.revision) : RecheckPlanResult{};
        if (!metadata || !plan.plan || !safeMetadata(*metadata, r.savePath) ||
            checkStorageIdentity(r.savePath).isEmpty() || (record &&
            (record->metadataDigest != metadataIdentity(*metadata) || record->storageIdentity != checkStorageIdentity(r.savePath) ||
             record->sourceReadOnly != r.sourceReadOnly || record->pieces != plan.plan->pieces ||
             record->resumeGeneration < r.resumeSnapshotGeneration))) {
            block(tr("Interrupted verification metadata, scope or storage identity changed; recovery is blocked."));
            return;
        }
        if (record) {
            r.checkRecord = record;
            r.resume.have_pieces.resize(metadata->num_pieces(), false);
            for (int p : record->pieces) {
                const auto piece = lt::piece_index_t(p);
                r.resume.have_pieces.clear_bit(piece);
                if (p < r.resume.verified_pieces.size()) r.resume.verified_pieces.clear_bit(piece);
                r.resume.unfinished_pieces.erase(piece);
            }
            r.resume.flags &= ~(lt::torrent_flags::seed_mode | lt::torrent_flags::auto_managed);
            r.resume.flags |= lt::torrent_flags::paused | lt::torrent_flags::no_verify_files;
            r.resumeGeneration = r.resumeSnapshotGeneration = record->resumeGeneration;
            r.fileCheck.token = record->token;
            { std::lock_guard lock(mutex); checkSerial = std::max(checkSerial, record->token); }
        }
        r.fileCheck.recoveryRequired = true;
        r.fileCheck.recheckOnly = r.sourceReadOnly;
        r.fileCheck.totalBytes = plan.plan->totalBytes;
        r.fileCheck.files.clear();
        for (int index : plan.plan->selectedFiles)
            r.fileCheck.files.append({index, FileCheckOutcome::Unavailable, tr("Interrupted verification left affected pieces unavailable.")});
        r.fileCheck.error = r.sourceReadOnly ? tr("Verification was interrupted. Recheck the selected original files; repair downloads are forbidden.") :
            tr("Verification was interrupted. Recheck the selected files or explicitly resume to redownload unavailable pieces.");
        if (metadata) priorities(r, *metadata);
        if (record && (!persist() || !journal.finish(r.id, record->token, r.durableResumeGeneration, &error))) {
            block(error.isEmpty() ? tr("Cannot checkpoint interrupted verification; recovery remains blocked.") : error);
            return;
        }
        r.checkRecord.reset();
        fail(r, r.fileCheck.error);
        dirty = true;
    }
    bool claimStorage(Record &r, const lt::torrent_info &ti, const QString &root) {
        QStringList paths;
        for (auto index : ti.layout().file_range()) {
            if (ti.layout().file_flags(index) & lt::file_storage::flag_pad_file) continue;
            paths.append(storageKey(QDir(root).filePath(text(ti.layout().file_path(index)))));
        }
        const auto otherOwner = [&](const QString &path) {
            const auto it = storageOwners.find(path);
            return it != storageOwners.end() && it->second != r.id;
        };
        const auto collision = [&] {
            r.storageCollision = true;
            dirty = true;
            fail(r, storageCollisionError());
            return false;
        };
        for (const auto &path : paths) {
            if (otherOwner(path)) return collision();
            for (auto slash = path.lastIndexOf('/'); slash > 0; slash = path.lastIndexOf('/', slash - 1))
                if (otherOwner(path.left(slash))) return collision();
            const auto prefix = path + '/';
            for (auto it = storageOwners.lower_bound(prefix); it != storageOwners.end() && it->first.startsWith(prefix); ++it)
                if (it->second != r.id) return collision();
        }
        // Validation and reservation are one worker transaction, before libtorrent I/O.
        for (const auto &path : paths) storageOwners.emplace(path, r.id);
        r.ownsStorage = true;
        dirty = true;
        return true;
    }
    void releaseStorage(const QString &id) {
        std::erase_if(storageOwners, [&](const auto &entry) { return entry.second == id; });
    }
    void reapRemovals() {
        std::erase_if(removals, [&](const Removal &removal) {
            if (removal.handle.is_valid() || (removal.deleting && !removal.deletionFinished)) return false;
            releaseStorage(removal.id);
            return true;
        });
    }
    std::vector<lt::download_priority_t> priorities(Record &r, const lt::torrent_info &ti) {
        std::vector<lt::download_priority_t> values(ti.num_files(), lt::dont_download);
        r.files.clear();
        for (auto index : ti.layout().file_range()) {
            if (ti.layout().file_flags(index) & lt::file_storage::flag_pad_file) continue;
            const int i = static_cast<int>(index);
            const bool wanted = !r.wanted || r.wanted->contains(i);
            const int priority = r.filePriorities.value(i, 4);
            values[i] = wanted ? lt::download_priority_t{static_cast<std::uint8_t>(priority)} : lt::dont_download;
            r.files.append({i, text(ti.layout().file_path(index)), ti.layout().file_size(index), wanted, priority});
        }
        return values;
    }
    bool validSelection(const Record &r, const lt::torrent_info &ti) {
        if (!r.wanted) return true;
        return std::all_of(r.wanted->begin(), r.wanted->end(), [&](int i) {
            return i >= 0 && i < ti.num_files() && !(ti.layout().file_flags(lt::file_index_t(i)) & lt::file_storage::flag_pad_file);
        });
    }
    void routing(lt::add_torrent_params &params) {
        if (!config.blockedCountries.isEmpty() || config.blockUnknownClients) {
            // Web seeds have no BitTorrent handshake. Strip both resume/magnet
            // URLs and metadata URLs; max_web_seed_connections=0 means unlimited.
            params.url_seeds.clear();
#if TORRENT_ABI_VERSION < 4
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
            params.http_seeds.clear();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif
            params.flags |= lt::torrent_flags::deprecated_override_web_seeds;
        }
        params.flags |= lt::torrent_flags::update_subscribe | lt::torrent_flags::apply_ip_filter;
        params.flags &= ~(lt::torrent_flags::seed_mode | lt::torrent_flags::auto_managed | lt::torrent_flags::paused);
        if (!config.pex) params.flags |= lt::torrent_flags::disable_pex;
        else params.flags &= ~lt::torrent_flags::disable_pex;
        if (!config.dht || (proxy.type == ProxyType::Socks5 && !proxy.udp)) params.flags |= lt::torrent_flags::disable_dht;
        else params.flags &= ~lt::torrent_flags::disable_dht;
        if (!config.localDiscovery || proxy.type != ProxyType::Direct) params.flags |= lt::torrent_flags::disable_lsd;
        else params.flags &= ~lt::torrent_flags::disable_lsd;
        const bool tcpOnly = proxy.type == ProxyType::Socks5 && !proxy.udp;
        std::vector<std::string> trackers;
        std::vector<int> tiers;
        for (std::size_t i = 0; i < params.trackers.size(); ++i) {
            const auto scheme = QUrl(text(params.trackers[i])).scheme().toLower();
            if (scheme != "http" && scheme != "https" && !(scheme == "udp" && !tcpOnly)) continue;
            trackers.push_back(params.trackers[i]);
            tiers.push_back(i < params.tracker_tiers.size() ? params.tracker_tiers[i] : 0);
        }
        params.trackers = std::move(trackers);
        params.tracker_tiers = std::move(tiers);
        if (tcpOnly) {
            params.dht_nodes.clear();
        } else if (proxy.type == ProxyType::Socks5) {
            // Metadata and resume files may carry their own bootstrap hostnames.
            std::erase_if(params.dht_nodes, [](auto &node) {
                const QHostAddress address(QString::fromStdString(node.first));
                if (address.isNull() || !address.scopeId().isEmpty() || node.second < 1 || node.second > 65535)
                    return true;
                node.first = address.toString().toStdString();
                return false;
            });
        }
        params.max_connections = config.perTorrentConnections;
    }
    void fail(Record &r, const QString &reason) {
        r.failure = r.storageCollision ? storageCollisionError() : reason;
        r.notified = false;
        if (r.handle.is_valid()) {
            r.handle.unset_flags(lt::torrent_flags::auto_managed);
            r.handle.pause();
        }
        report(r.failure);
    }
    bool sourceAvailable(const lt::torrent_info &ti, const QString &root) {
        for (const auto index : ti.layout().file_range()) {
            if (ti.layout().file_flags(index) & lt::file_storage::flag_pad_file) continue;
            const QFileInfo file(QDir(root).filePath(text(ti.layout().file_path(index))));
            if (!file.isFile() || file.size() != ti.layout().file_size(index)) return false;
        }
        return true;
    }
    void protectSource(Record &r) {
        if (!r.sourceReadOnly || !r.handle.is_valid()) return;
        r.handle.set_flags(lt::torrent_flags::upload_mode);
        r.handle.unset_flags(lt::torrent_flags::auto_managed | lt::torrent_flags::seed_mode |
                             lt::torrent_flags::no_verify_files | lt::torrent_flags::share_mode);
    }
    void resume(Record &r) {
        if (r.sourceReadOnly) protectSource(r);
        else r.handle.set_flags(lt::torrent_flags::auto_managed);
        r.handle.resume();
    }
    void start(Record &r) {
        if (r.storageCollision || r.fileCheck.recoveryRequired || r.checkRecord) return;
        auto params = r.resume;
        params.save_path = r.savePath.toStdString();
        routing(params);
        if (!noSymlinks(r.savePath) || (params.ti && !safeMetadata(*params.ti, r.savePath))) {
            fail(r, tr("Unsafe Torrent storage path or symlink metadata was rejected.")); return;
        }
        if (params.ti && !validSelection(r, *params.ti)) {
            fail(r, tr("Torrent file selection contains an invalid index.")); return;
        }
        if (r.sourceReadOnly && (!params.ti || !sourceAvailable(*params.ti, r.savePath))) {
            fail(r, tr("Original Torrent source files are missing or changed size; upload-only seeding was stopped.")); return;
        }
        if (params.ti && !claimStorage(r, *params.ti, r.savePath)) return;
        if (!r.sourceReadOnly && !QDir().mkpath(r.savePath)) { fail(r, tr("Cannot create Torrent download directory.")); return; }
        if (params.ti) params.file_priorities = priorities(r, *params.ti);
        else params.flags |= lt::torrent_flags::upload_mode | lt::torrent_flags::default_dont_download;
        if (r.userPaused || r.seedStopped || (r.wanted && r.wanted->isEmpty() && params.ti)) params.flags |= lt::torrent_flags::paused;
        else if (!r.sourceReadOnly) params.flags |= lt::torrent_flags::auto_managed | lt::torrent_flags::paused;
        if (r.sourceReadOnly) {
            params.flags |= lt::torrent_flags::upload_mode;
            params.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::seed_mode |
                              lt::torrent_flags::no_verify_files | lt::torrent_flags::share_mode);
            // Original files can change outside the app, even without size/mtime changes.
            params.have_pieces.clear();
            params.verified_pieces.clear();
            params.unfinished_pieces.clear();
        }
        params.flags |= lt::torrent_flags::duplicate_is_error;
        if (r.deferred) {
            r.resume = std::move(params);
            r.status.has_metadata = bool(r.resume.ti);
            r.status.total_wanted = 0;
            for (const auto &file : r.files) if (file.wanted) r.status.total_wanted += file.size;
            return;
        }
        if (!createSession()) return;
        lt::error_code ec;
        r.handle = session->add_torrent(params, ec);
        if (ec) { fail(r, tr("Cannot add Torrent: duplicate job or invalid metadata.")); return; }
        r.resume = std::move(params);
        r.observedUpload = std::max(r.observedUpload, qint64(r.resume.total_uploaded));
        r.uploadBaseline = r.observedUpload;
        r.lastPayloadUpload = 0;
        r.status = r.handle.status(lt::torrent_handle::query_name | lt::torrent_handle::query_save_path);
        requestSave(r);
    }
    void add(QString id, QString input, QString path, QList<int> selection, bool startPaused, bool sourceReadOnly) {
        if (!loaded) restore();
        Record r;
        r.id = id;
        r.userPaused = startPaused;
        r.deferred = startPaused;
        r.sourceReadOnly = sourceReadOnly;
        r.savePath = storagePath(path.isEmpty() ? (config.downloadPath.isEmpty() ? QDir(stateDir).filePath("downloads") : config.downloadPath) : path);
        if (!selection.isEmpty()) r.wanted = selection;
        lt::error_code ec;
        if (input.startsWith("magnet:", Qt::CaseInsensitive)) r.resume = lt::parse_magnet_uri(input.toStdString(), ec);
        else {
            QFile file(input);
            if (!file.open(QIODevice::ReadOnly) || file.size() > maxMetadata) ec = lt::errors::torrent_file_parse_failed;
            else {
                const auto bytes = file.readAll();
                r.resume = lt::load_torrent_buffer(lt::span<char const>(bytes.constData(), bytes.size()), ec, parseLimits());
            }
        }
        if (ec) {
            report(tr("Torrent metadata could not be parsed."));
            {
                std::lock_guard lock(mutex);
                snapshots.removeIf([&](const Job &j) { return j.id == id; });
            }
            emit q->changed();
            return;
        }
        auto [it, inserted] = records.emplace(id, std::move(r));
        if (inserted) start(it->second);
        dirty = true;
        publish();
    }
    Record *find(const lt::torrent_handle &handle) {
        for (auto &[id, r] : records) if (r.handle == handle) return &r;
        return nullptr;
    }
    void metadata(Record &r) {
        if (r.storageCollision) return;
        const auto ti = r.handle.torrent_file();
        if (!ti || !safeMetadata(*ti, r.savePath) || !validSelection(r, *ti)) {
            fail(r, tr("Received Torrent metadata has an unsafe path or invalid file selection.")); return;
        }
        if (!claimStorage(r, *ti, r.savePath)) return;
        if (!invalidateResume(r)) return;
        r.handle.prioritize_files(priorities(r, *ti));
        r.selectionPending = true;
        r.handle.unset_flags(lt::torrent_flags::default_dont_download);
        if (r.sourceReadOnly) protectSource(r);
        else r.handle.unset_flags(lt::torrent_flags::upload_mode);
        if (r.wanted && r.wanted->isEmpty()) {
            r.handle.unset_flags(lt::torrent_flags::auto_managed);
            r.handle.pause();
        }
        requestSave(r);
    }
    void alerts(lt::session *source = nullptr) {
        std::vector<lt::alert *> batch;
        (source ? source : session.get())->pop_alerts(&batch);
        for (auto *alert : batch) {
            if (auto *a = lt::alert_cast<lt::torrent_deleted_alert>(alert)) {
                for (auto &removal : removals) if (removal.deleting && removal.hashes == a->info_hashes)
                    removal.deletionFinished = true;
            } else if (auto *a = lt::alert_cast<lt::torrent_delete_failed_alert>(alert)) {
                for (auto &removal : removals) if (removal.deleting && removal.hashes == a->info_hashes)
                    removal.deletionFinished = true;
                report(tr("Torrent data deletion failed; some files were retained."));
            }
            if (auto *a = lt::alert_cast<lt::state_update_alert>(alert)) {
                for (const auto &status : a->status) if (auto *r = find(status.handle)) {
                    if (status.total_payload_upload < r->lastPayloadUpload) r->uploadBaseline = r->observedUpload;
                    r->observedUpload = std::max({r->observedUpload, qint64(status.all_time_upload),
                        r->uploadBaseline + qint64(status.total_payload_upload)});
                    r->lastPayloadUpload = status.total_payload_upload;
                    r->status = status;
                    if (!status.save_path.empty()) r->savePath = text(status.save_path);
                    if (status.errc) fail(*r, tr("Torrent stopped due to a storage or metadata error."));
                    if (!closing) finish(*r);
                }
                continue;
            }
            auto *ta = dynamic_cast<lt::torrent_alert *>(alert);
            Record *r = ta ? find(ta->handle) : nullptr;
            if (auto *a = lt::alert_cast<lt::save_resume_data_alert>(alert)) {
                if (r && r->savePending) {
                    // Only one save is admitted per handle. A mutation fences its
                    // queued reply before a newer request can be submitted.
                    if (r->requestedSaveGeneration == r->resumeGeneration) {
                        r->resume = a->params;
                        r->resumeSnapshotGeneration = r->requestedSaveGeneration;
                        dirty = true;
                    }
                    r->savePending = false;
                    r->requestedSaveGeneration = 0;
                    if (std::exchange(r->saveAgain, false)) requestSave(*r);
                }
            } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
                if (r) {
                    r->savePending = false;
                    r->requestedSaveGeneration = 0;
                    if (std::exchange(r->saveAgain, false)) requestSave(*r);
                }
                if (r && closing) closingCheckpointOk = false;
                report(tr("Torrent resume checkpoint failed; retaining the previous checkpoint."));
                if (r && r->fileCheck.active && r->checkTerminal)
                    failFileCheck(*r, tr("Cannot persist selected-file verification; recovery remains required."));
            } else if (lt::alert_cast<lt::metadata_received_alert>(alert)) {
                if (r && !closing) metadata(*r);
            } else if (lt::alert_cast<lt::file_prio_alert>(alert)) {
                if (r) { r->selectionPending = false; requestSave(*r); }
            } else if (auto *a = lt::alert_cast<lt::storage_moved_alert>(alert)) {
                if (r) {
                    r->moving = false;
                    r->savePath = QString::fromUtf8(a->storage_path());
                    if (const auto ti = r->handle.torrent_file()) {
                        releaseStorage(r->id);
                        claimStorage(*r, *ti, r->savePath);
                    }
                    requestSave(*r);
                }
            } else if (lt::alert_cast<lt::storage_moved_failed_alert>(alert)) {
                if (r) { r->moving = false; fail(*r, tr("Torrent completed-file relocation failed; original data was retained.")); }
            } else if (lt::alert_cast<lt::torrent_checked_alert>(alert)) {
                if (r) { r->checking = false; requestSave(*r); }
            } else if (lt::alert_cast<lt::torrent_error_alert>(alert) || lt::alert_cast<lt::file_error_alert>(alert)) {
                if (r) fail(*r, tr("Torrent stopped due to a storage or metadata error."));
            } else if (auto *a = lt::alert_cast<lt::listen_succeeded_alert>(alert)) {
                listenerSucceeded(a->address, a->port, a->socket_type);
            } else if (auto *a = lt::alert_cast<lt::listen_failed_alert>(alert)) {
                listenerFailed(a->op);
            } else if (auto *a = lt::alert_cast<lt::socks5_alert>(alert)) {
                reportSocks5(a->op, a->ip, a->error);
            }
        }
    }
    void finish(Record &r) {
        if (!r.failure.isEmpty() || r.fileCheck.active || r.checkRecord || r.checking || r.selectionPending || r.moving || !r.status.is_finished ||
            !r.status.has_metadata || r.status.state == lt::torrent_status::checking_files ||
            r.status.state == lt::torrent_status::checking_resume_data || r.files.isEmpty()) return;
        const bool anyWanted = std::any_of(r.files.begin(), r.files.end(), [](const auto &f) { return f.wanted; });
        if (!anyWanted) return;
        if (!r.sourceReadOnly && !config.completedPath.isEmpty() && QDir::cleanPath(r.savePath) != QDir::cleanPath(config.completedPath)) {
            if (r.moveAttemptedTarget != config.completedPath) {
                r.moveAttemptedTarget = config.completedPath;
                auto ti = r.handle.torrent_file();
                if (!ti || !safeMetadata(*ti, config.completedPath)) {
                    fail(r, tr("Unsafe or unavailable Torrent completed directory.")); return;
                }
                if (!claimStorage(r, *ti, config.completedPath)) return;
                if (!QDir().mkpath(config.completedPath)) {
                    fail(r, tr("Unsafe or unavailable Torrent completed directory.")); return;
                }
                r.moving = true;
                if (!invalidateResume(r)) { r.moving = false; return; }
                r.handle.move_storage(config.completedPath.toStdString(), lt::move_flags_t::fail_if_exist);
            }
            return;
        }
        if (!r.notified) {
            const auto progress = r.handle.file_progress(lt::torrent_handle::piece_granularity);
            QStringList completed;
            for (const auto &file : r.files) if (file.wanted) {
                const auto path = QDir(r.savePath).filePath(file.path);
                QFileInfo info(path);
                if (file.index >= int(progress.size()) || progress[file.index] != file.size ||
                    !info.isFile() || info.size() != file.size || !noSymlinks(path)) return;
                completed << info.absoluteFilePath();
            }
            r.notified = true;
            const auto ti = r.handle.torrent_file();
            bool current;
            { std::lock_guard lock(mutex); current = revisions.value(r.id) == r.revision; }
            if (current) emit q->completed(r.id, completed, ti && ti->priv());
            requestSave(r);
        }
        const auto denominator = std::max<std::int64_t>(r.status.all_time_download, r.status.total_wanted);
        if (!r.seedStopped && ((config.seedRatio > 0 && denominator > 0 &&
            double(r.observedUpload) / double(denominator) >= config.seedRatio) ||
            (config.seedMinutes > 0 && r.status.finished_duration.count() >= std::int64_t(config.seedMinutes) * 60))) {
            r.seedStopped = true;
            r.handle.unset_flags(lt::torrent_flags::auto_managed);
            r.handle.pause();
            requestSave(r);
        }
    }
    void collectPeers() {
        QString id;
        quint64 observation, configuration, revision;
        {
            std::lock_guard lock(mutex);
            if (stopped || listenerEpoch != configurationEpoch) return;
            id = observedJobId;
            observation = peerObservationEpoch;
            configuration = configurationEpoch;
            revision = revisions.value(id);
        }
        QList<Peer> next;
        for (auto &[jobId, record] : records) {
            record.downloadingPeers = record.uploadingPeers = 0;
            if (!session || closing || !record.handle.is_valid() || record.userPaused || record.seedStopped ||
                bool(record.status.flags & lt::torrent_flags::paused)) continue;
            std::vector<lt::peer_info> information;
            // A disappeared handle is an empty snapshot, not stale peer details.
            try { record.handle.get_peer_info(information); } catch (...) { information.clear(); }
            for (const auto &info : information) {
                if (!(info.flags & (lt::peer_info::connecting | lt::peer_info::handshake))) {
                    record.downloadingPeers += info.payload_down_speed > 0;
                    record.uploadingPeers += info.payload_up_speed > 0;
                }
                if (jobId != id || record.revision != revision) continue;
                Peer peer;
                const auto endpoint = info.remote_endpoint();
                peer.ip = text(endpoint.address().to_string());
                peer.port = endpoint.port();
                peer.id = (endpoint.address().is_v6() ? '[' + peer.ip + ']' : peer.ip) + ':' + QString::number(peer.port);
                peer.client = text(info.client);
                if (peerPolicy) peer.countryCode = peerPolicy->country(peer.ip);
                peer.progress = std::clamp(double(info.progress_ppm) / 1000000, 0.0, 1.0);
                peer.downloadRate = std::max(0, info.payload_down_speed);
                peer.uploadRate = std::max(0, info.payload_up_speed);
                peer.transport = bool(info.flags & lt::peer_info::i2p_socket) ? QStringLiteral("I2P") :
                    bool(info.flags & lt::peer_info::utp_socket) ? QStringLiteral("uTP") : QStringLiteral("TCP");
                peer.encryption = bool(info.flags & lt::peer_info::ssl_socket) ? QStringLiteral("TLS") :
                    bool(info.flags & lt::peer_info::rc4_encrypted) ? QStringLiteral("RC4") :
                    bool(info.flags & lt::peer_info::plaintext_encrypted) ? tr("Handshake only") : tr("None");
                if (info.flags & lt::peer_info::connecting) peer.state = tr("Connecting");
                else if (info.flags & lt::peer_info::handshake) peer.state = tr("Handshake");
                else {
                    QStringList state;
                    if (info.flags & lt::peer_info::seed) state << tr("Seed");
                    if (info.flags & lt::peer_info::remote_choked) state << tr("Download choked");
                    if (info.flags & lt::peer_info::choked) state << tr("Upload choked");
                    if (info.flags & lt::peer_info::snubbed) state << tr("Snubbed");
                    peer.state = state.isEmpty() ? tr("Connected") : state.join(QStringLiteral(", "));
                }
                next.append(std::move(peer));
            }
        }
        {
            std::lock_guard lock(mutex);
            // Selection can change A -> B -> A while the synchronous query is
            // in flight; matching the id alone cannot make that result current.
            if (stopped || observedJobId != id || observation != peerObservationEpoch ||
                configuration != configurationEpoch || revision != revisions.value(id)) return;
            peerSnapshots = std::move(next);
        }
        emit q->peersChanged();
    }
    void publish() {
        QList<Job> next;
        QHash<QString, std::shared_ptr<const lt::torrent_info>> metadata;
        QSet<QString> busy;
        for (auto &[id, r] : records) {
            metadata.insert(id, r.handle.is_valid() ? r.handle.torrent_file() : r.resume.ti);
            if (r.moving || r.selectionPending || r.checking || (r.handle.is_valid() &&
                (r.status.state == lt::torrent_status::checking_files || r.status.state == lt::torrent_status::checking_resume_data)))
                busy.insert(id);
            Job j;
            j.id = id;
            j.proxied = proxy.type != ProxyType::Direct;
            j.revision = r.revision;
            j.files = r.files;
            j.fileCheck = r.fileCheck;
            j.name = r.resume.ti ? text(r.resume.ti->name()) : text(r.status.name);
            if (j.name.isEmpty()) j.name = tr("Fetching Torrent metadata");
            j.savePath = r.savePath;
            j.privateTorrent = r.resume.ti && r.resume.ti->priv();
            j.dcShareExcluded = r.dcShareExcluded;
            if (!j.privateTorrent && r.handle.is_valid()) {
                const auto ti = r.handle.torrent_file();
                j.privateTorrent = ti && ti->priv();
            }
            j.progress = double(r.status.progress_ppm) / 1000000;
            const bool checkingFiles = r.status.state == lt::torrent_status::checking_files;
            j.checking = r.fileCheck.active || r.checking || (r.handle.is_valid() &&
                (checkingFiles || r.status.state == lt::torrent_status::checking_resume_data));
            if (j.checking) {
                j.checkProgress = checkingFiles ? std::clamp(j.progress, 0.0, 1.0) : -1;
                if (r.fileCheck.active) j.checkProgress = r.fileCheck.totalBytes > 0 ?
                    double(r.fileCheck.checkedBytes) / r.fileCheck.totalBytes : 0;
                j.progress = r.status.total_wanted > 0 ? std::clamp(
                    double(r.status.total_wanted_done) / r.status.total_wanted, 0.0, 1.0) : 0;
            }
            j.size = r.status.total_wanted;
            j.downloaded = r.status.total_wanted_done;
            j.uploaded = r.observedUpload;
            j.downloadRate = r.status.download_payload_rate;
            j.uploadRate = r.status.upload_payload_rate;
            j.peers = r.status.num_peers;
            j.downloadingPeers = r.downloadingPeers;
            j.uploadingPeers = r.uploadingPeers;
            j.seeds = r.status.num_seeds;
            j.paused = r.userPaused || r.seedStopped || bool(r.status.flags & lt::torrent_flags::paused);
            j.stopped = r.userStopped;
            const auto hashes = r.handle.is_valid() ? r.handle.info_hashes() :
                r.resume.ti ? r.resume.ti->info_hashes() : r.resume.info_hashes;
            j.v1 = hashes.has_v1();
            j.v2 = hashes.has_v2();
            if (j.v1 || j.v2) {
                lt::add_torrent_params share;
                share.info_hashes = hashes;
                share.name = j.name.toStdString();
                j.magnet = text(lt::make_magnet_uri(share));
            }
            j.complete = r.notified && !r.fileCheck.active && !r.checkRecord && !r.checking && !r.selectionPending && !r.moving && r.failure.isEmpty();
            j.error = r.failure.isEmpty() ? blocked : r.failure;
            if (j.paused || !j.error.isEmpty()) {
                j.downloadingPeers = j.uploadingPeers = 0;
                j.downloadRate = j.uploadRate = 0;
            }
            if (!j.error.isEmpty()) j.state = tr("Blocked");
            else if (r.fileCheck.active) j.state = r.fileCheck.canceling ? tr("Canceling selected-file verification") :
                tr("Checking selected files... %1%").arg(j.checkProgress * 100, 0, 'f', 1);
            else if (j.checking) {
                if (j.checkProgress < 0)
                    j.state = j.paused ? tr("Recheck queued (paused)") : tr("Recheck queued");
                else
                    j.state = (j.paused ? tr("Recheck paused (%1%)") : tr("Rechecking... %1%"))
                        .arg(j.checkProgress * 100, 0, 'f', 1);
            }
            else if (r.userStopped) j.state = tr("Stopped");
            else if (r.userPaused) j.state = tr("Paused");
            else if (r.moving) j.state = tr("Moving completed files");
            else if (r.seedStopped) j.state = tr("Seeding limit reached");
            else if (r.wanted && r.wanted->isEmpty()) j.state = tr("No files selected");
            else if (j.paused) j.state = r.userPaused ? tr("Paused") : tr("Queued");
            else if (j.complete) j.state = tr("Seeding");
            else if (!r.status.has_metadata) j.state = tr("Fetching metadata");
            else j.state = tr("Downloading");
            next.append(j);
        }
        {
            std::lock_guard lock(mutex);
            QList<Job> merged;
            for (auto &job : next) {
                // A preceding worker operation may publish after public pause
                // acceptance but before the queued pause is executed.
                const auto pending = pendingActivities.constFind(job.id);
                if (pending != pendingActivities.cend()) {
                    job.downloadingPeers = job.uploadingPeers = 0;
                    job.downloadRate = job.uploadRate = 0;
                    job.paused = pending->paused;
                    job.stopped = pending->stopped;
                    if (job.error.isEmpty())
                        job.state = job.stopped ? tr("Stopped") : job.paused ? tr("Paused") : tr("Resuming");
                }
                if (job.revision == revisions.value(job.id)) {
                    metadataSnapshots.insert(job.id, metadata.value(job.id));
                    if (busy.contains(job.id)) mutationBusy.insert(job.id); else mutationBusy.remove(job.id);
                    merged.append(std::move(job));
                }
                else {
                    const auto existing = std::find_if(snapshots.begin(), snapshots.end(), [&](const Job &old) { return old.id == job.id; });
                    if (existing != snapshots.end()) merged.append(*existing);
                }
            }
            snapshots = std::move(merged);
            fileSnapshots.clear();
            for (const auto &job : snapshots) fileSnapshots.insert(job.id, job.files);
        }
        emit q->changed();
    }
};

TorrentEngine::TorrentEngine(QString stateDirectory, QObject *parent, CountryLookup lookup)
    : QObject(parent), d(std::make_unique<Impl>(this, QDir(stateDirectory).absolutePath(), std::move(lookup))) {}
TorrentEngine::~TorrentEngine() { shutdown(); }
quint64 TorrentEngine::configure(const Settings &settings, const ProxyConfig &proxy) {
    Q_ASSERT(QThread::currentThread() == thread());
    const auto invalid = routeError(settings, proxy);
    quint64 epoch;
    bool listenersInvalidated;
    QHash<QString, quint64> configureRevisions;
    {
        std::unique_lock lock(d->mutex);
        if (d->stopped || d->commands.size() >= 1024) {
            lock.unlock();
            emit error(tr("Torrent backend is shut down or its command queue is full."));
            return 0;
        }
        // Storage cancellation must see the old revisions and run without the
        // cache lock. Recheck admission afterwards, then commit and enqueue together.
        lock.unlock();
        emit storageInvalidating({});
        lock.lock();
        if (d->stopped || d->commands.size() >= 1024) {
            lock.unlock();
            emit error(tr("Torrent backend is shut down or its command queue is full."));
            return 0;
        }
        d->apiSettings = settings;
        d->apiError = invalid;
        epoch = ++d->configurationEpoch;
        listenersInvalidated = !d->listenerSnapshots.isEmpty();
        d->listenerSnapshots.clear();
        d->peerSnapshots.clear();
        ++d->peerObservationEpoch;
        for (auto &job : d->snapshots) {
            job.complete = false;
            job.revision = ++d->revisions[job.id];
        }
        configureRevisions = d->revisions;
        d->commands.push_back([p = d.get(), settings, proxy, invalid, epoch, configureRevisions] {
            p->configure(settings, proxy, invalid, epoch, configureRevisions);
        });
    }
    d->wake.notify_one();
    if (listenersInvalidated) emit listenersChanged();
    emit peersChanged();
    if (!invalid.isEmpty()) emit error(invalid);
    emit changed();
    return epoch;
}
Settings TorrentEngine::settings() const { std::lock_guard lock(d->mutex); return d->apiSettings; }
QStringList TorrentEngine::effectiveListeners() const { std::lock_guard lock(d->mutex); return d->listenerSnapshots; }
quint64 TorrentEngine::configurationGeneration() const { std::lock_guard lock(d->mutex); return d->configurationEpoch; }
QString TorrentEngine::add(const QString &input, const QString &savePath, const QList<int> &wanted, bool startPaused) {
    return enqueueAdd(input, savePath, wanted, startPaused, false);
}
QString TorrentEngine::seedCreated(const QString &metadata, const QString &sourceParent) {
    return enqueueAdd(metadata, sourceParent, {}, false, true);
}
QString TorrentEngine::enqueueAdd(const QString &input, const QString &savePath, const QList<int> &wanted,
                                 bool startPaused, bool sourceReadOnly) {
    Q_ASSERT(QThread::currentThread() == thread());
    QString reason;
    {
        std::lock_guard lock(d->mutex);
        reason = d->apiError;
        if (d->stopped) reason = tr("Torrent backend is shut down.");
        if (d->snapshots.size() >= maxJobs) reason = tr("Torrent job limit reached.");
    }
    if (reason.isEmpty() && (!savePath.isEmpty() && (!QDir::isAbsolutePath(savePath) || savePath.contains(QChar(0)))))
        reason = tr("Torrent download destination must be an absolute path.");
    if (reason.isEmpty() && sourceReadOnly && (input.startsWith("magnet:", Qt::CaseInsensitive) ||
        savePath.isEmpty() || !QFileInfo(savePath).isDir()))
        reason = tr("Seeding original files requires local Torrent metadata and an existing source directory.");
    if (reason.isEmpty() && (wanted.size() > maxFiles || std::any_of(wanted.begin(), wanted.end(), [](int i) { return i < 0 || i >= maxFiles; })))
        reason = tr("Invalid Torrent file selection.");
    if (reason.isEmpty()) {
        if (input.size() > 65536 || input.contains(QChar(0))) reason = tr("Torrent input is too large or invalid.");
        else if (input.startsWith("magnet:", Qt::CaseInsensitive)) {
            lt::error_code ec;
            const auto params = lt::parse_magnet_uri(input.toStdString(), ec);
            if (ec || (!params.info_hashes.has_v1() && !params.info_hashes.has_v2())) reason = tr("Invalid BitTorrent magnet link.");
        } else if (!QFileInfo(input).isFile()) reason = tr("Torrent metadata file does not exist.");
    }
    if (!reason.isEmpty()) { emit error(reason); return {}; }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        std::lock_guard lock(d->mutex);
        Job pending;
        pending.id = id;
        pending.paused = startPaused;
        pending.state = tr("Loading metadata");
        d->snapshots.append(pending);
    }
    if (!d->enqueue([p = d.get(), id, input, savePath, wanted, startPaused, sourceReadOnly] {
        p->add(id, input, savePath, wanted, startPaused, sourceReadOnly);
    })) {
        { std::lock_guard lock(d->mutex); d->snapshots.removeIf([&](const Job &j) { return j.id == id; }); }
        emit error(tr("Torrent backend is shut down or its command queue is full."));
        return {};
    }
    return id;
}
QList<Job> TorrentEngine::jobs() const { std::lock_guard lock(d->mutex); return d->snapshots; }
void TorrentEngine::selectObservedJob(const QString &id) {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        std::lock_guard lock(d->mutex);
        const bool exists = !d->stopped && std::any_of(d->snapshots.cbegin(), d->snapshots.cend(),
            [&](const Job &job) { return job.id == id; });
        const QString selected = exists ? id : QString{};
        if (selected == d->observedJobId) return;
        d->observedJobId = selected;
        d->peerSnapshots.clear();
        ++d->peerObservationEpoch;
    }
    emit peersChanged();
}
QString TorrentEngine::observedJob() const { std::lock_guard lock(d->mutex); return d->observedJobId; }
QList<Peer> TorrentEngine::peers(const QString &id) const {
    std::lock_guard lock(d->mutex);
    return !d->stopped && !id.isEmpty() && id == d->observedJobId ? d->peerSnapshots : QList<Peer>{};
}
QList<FileEntry> TorrentEngine::files(const QString &id) const { std::lock_guard lock(d->mutex); return d->fileSnapshots.value(id); }
QList<FileEntry> TorrentEngine::files(const QString &id, quint64 revision) const {
    std::lock_guard lock(d->mutex);
    if (d->revisions.value(id) != revision) return {};
    return d->fileSnapshots.value(id);
}
bool TorrentEngine::publicationValid(const QString &id, quint64 revision, quint64 epoch) const {
    std::lock_guard lock(d->mutex);
    if (d->stopped || epoch == 0 || epoch != d->configurationEpoch ||
        revision != d->revisions.value(id) || !d->apiError.isEmpty()) return false;
    const auto job = std::find_if(d->snapshots.cbegin(), d->snapshots.cend(),
        [&](const Job &snapshot) { return snapshot.id == id; });
    return job != d->snapshots.cend() && job->revision == revision && job->complete &&
        !job->dcShareExcluded && job->error.isEmpty();
}
void TorrentEngine::setDcShareExcluded(const QString &id, bool excluded) {
    Q_ASSERT(QThread::currentThread() == thread());
    std::unique_lock lock(d->mutex);
    const auto findJob = [&] {
        return std::find_if(d->snapshots.begin(), d->snapshots.end(),
            [&](const Job &job) { return job.id == id; });
    };
    auto job = findJob();
    if (job == d->snapshots.end() || job->dcShareExcluded == excluded) return;
    if (d->stopped || d->commands.size() >= 1024) {
        lock.unlock();
        emit error(tr("Torrent backend is shut down or its command queue is full."));
        return;
    }
    // Cancellation observes the old revision without holding the snapshot mutex.
    lock.unlock();
    emit storageInvalidating(id);
    lock.lock();
    job = findJob();
    if (job == d->snapshots.end()) return;
    if (d->stopped || d->commands.size() >= 1024) {
        lock.unlock();
        emit error(tr("Torrent backend is shut down or its command queue is full."));
        return;
    }
    const auto revision = ++d->revisions[id];
    job->revision = revision;
    job->dcShareExcluded = excluded;
    // Revision merging prevents an earlier worker publish from undoing this choice.
    d->commands.push_back([p = d.get(), id, excluded, revision] {
        const auto it = p->records.find(id);
        if (it == p->records.end()) return;
        it->second.dcShareExcluded = excluded;
        it->second.revision = revision;
        p->dirty = true;
        p->publish();
    });
    lock.unlock();
    d->wake.notify_one();
    emit dcShareExclusionChanged(id);
    emit changed();
}
void TorrentEngine::setWantedFiles(const QString &id, const QList<int> &wanted) {
    setWantedFiles(id, wanted, {});
}
void TorrentEngine::setWantedFiles(const QString &id, const QList<int> &wanted, const QMap<int, int> &filePriorities) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->hasFileCheck(id)) { emit error(tr("File selection and priorities cannot change during selected-file verification.")); return; }
    emit storageInvalidating(id);
    const auto revision = d->invalidate(id);
    d->enqueue([p = d.get(), id, wanted, filePriorities, revision] {
        auto it = p->records.find(id);
        if (it == p->records.end()) return;
        auto &r = it->second;
        r.revision = revision;
        if (r.sourceReadOnly) {
            // Even upload_mode does not stop prioritize_files() from truncating
            // a reselected file. Originals keep their initial full selection.
            r.notified = false;
            p->report(tr("File selection cannot be changed for a Torrent seeding original files. Pause or remove the seed instead."));
            p->publish();
            return;
        }
        if (r.storageCollision) { p->publish(); return; }
        const auto old = r.wanted;
        r.wanted = wanted;
        const auto ti = r.handle.is_valid() ? r.handle.torrent_file() : r.resume.ti;
        for (auto priority = filePriorities.cbegin(); priority != filePriorities.cend(); ++priority) {
            if (priority.key() < 0 || priority.key() >= maxFiles || (ti && priority.key() >= ti->num_files()) ||
                (priority.value() != 1 && priority.value() != 4 && priority.value() != 7)) {
                r.wanted = old;
                p->report(tr("Invalid Torrent file priority."));
                p->publish();
                return;
            }
        }
        if (wanted.size() > maxFiles || (ti && !p->validSelection(r, *ti)) ||
            std::any_of(wanted.begin(), wanted.end(), [](int i) { return i < 0 || i >= maxFiles; })) {
            r.wanted = old;
            p->report(tr("Invalid Torrent file selection.")); return;
        }
        if (!p->invalidateResume(r)) { r.wanted = old; p->publish(); return; }
        r.notified = false;
        for (auto priority = filePriorities.cbegin(); priority != filePriorities.cend(); ++priority)
            r.filePriorities.insert(priority.key(), priority.value());
        r.seedStopped = false;
        if (ti) {
            auto priorities = p->priorities(r, *ti);
            r.resume.file_priorities = priorities;
            if (r.handle.is_valid()) {
                r.selectionPending = true;
                r.handle.prioritize_files(priorities);
                if (wanted.isEmpty()) { r.handle.unset_flags(lt::torrent_flags::auto_managed); r.handle.pause(); }
                else if (!r.userPaused) p->resume(r);
            }
        }
        p->requestSave(r);
        p->dirty = true;
        p->publish();
    });
    emit changed();
}
void TorrentEngine::pause(const QString &id, bool paused) {
    setPaused(id, paused, false);
}
void TorrentEngine::stop(const QString &id) {
    setPaused(id, true, true);
}
void TorrentEngine::setPaused(const QString &id, bool paused, bool stopped) {
    Q_ASSERT(QThread::currentThread() == thread());
    std::unique_lock lock(d->mutex);
    const auto current = std::find_if(d->snapshots.cbegin(), d->snapshots.cend(), [&](const Job &job) { return job.id == id; });
    if (!paused && current != d->snapshots.cend() && current->fileCheck.recoveryRequired && current->fileCheck.recheckOnly) {
        lock.unlock();
        emit error(tr("Recheck the original files before resuming; recovery downloads cannot modify read-only sources."));
        return;
    }
    if (d->stopped || d->commands.size() >= 1024) {
        lock.unlock();
        emit error(tr("Torrent backend is shut down or its command queue is full."));
        return;
    }
    const auto serial = ++d->activitySerial;
    d->pendingActivities.insert(id, {serial, paused, stopped});
    for (auto &job : d->snapshots)
        if (job.id == id) {
            job.downloadingPeers = job.uploadingPeers = 0;
            job.downloadRate = job.uploadRate = 0;
            job.paused = paused;
            job.stopped = stopped;
            if (job.error.isEmpty())
                job.state = stopped ? tr("Stopped") : paused ? tr("Paused") : tr("Resuming");
        }
    d->commands.push_back([p = d.get(), id, paused, stopped, serial] {
        const auto finishRequest = [p, id, serial] {
            std::lock_guard lock(p->mutex);
            const auto pending = p->pendingActivities.constFind(id);
            if (pending != p->pendingActivities.cend() && pending->serial == serial)
                p->pendingActivities.remove(id);
        };
        auto it = p->records.find(id);
        if (it == p->records.end()) { finishRequest(); return; }
        auto &r = it->second;
        if (!paused && r.fileCheck.recoveryRequired) {
            if (r.sourceReadOnly || r.checkRecord) { finishRequest(); p->publish(); return; }
            r.fileCheck.recoveryRequired = false;
            r.fileCheck.error.clear();
            r.failure.clear();
            r.userPaused = r.userStopped = false;
            // Persist the explicit choice before enabling any route.
            if (!p->persist()) {
                r.fileCheck.recoveryRequired = true;
                r.userPaused = true;
                p->fail(r, tr("Cannot save the verification recovery choice; the Torrent remains paused."));
                finishRequest(); p->publish(); return;
            }
        }
        r.userPaused = paused;
        r.userStopped = stopped;
        if (paused) r.downloadingPeers = r.uploadingPeers = 0;
        if (!paused) r.seedStopped = false;
        if (stopped) p->cancelCheck(r);
        if (!paused && !r.fileCheck.active && r.deferred && p->blocked.isEmpty() && r.failure.isEmpty()) {
            r.deferred = false;
            p->start(r);
        }
        if (r.handle.is_valid() && !r.fileCheck.active) {
            if (paused) {
                r.handle.unset_flags(lt::torrent_flags::auto_managed);
                r.handle.pause(stopped ? lt::pause_flags_t{} : lt::torrent_handle::graceful_pause);
            }
            else if (r.failure.isEmpty() && (!r.wanted || !r.wanted->isEmpty())) {
                p->resume(r);
            }
        }
        p->requestSave(r);
        p->dirty = true;
        finishRequest();
        p->publish();
    });
    lock.unlock();
    d->wake.notify_one();
    emit changed();
}
void TorrentEngine::remove(const QString &id, bool deleteFiles) {
    Q_ASSERT(QThread::currentThread() == thread());
    emit storageInvalidating(id);
    const auto revision = d->invalidate(id, true);
    emit peersChanged();
    d->enqueue([p = d.get(), id, deleteFiles, revision]() mutable {
        auto it = p->records.find(id);
        if (it == p->records.end()) return;
        auto &r = it->second;
        if (r.fileCheck.active || r.checkRecord) {
            p->cancelCheck(r);
            const auto deadline = Clock::now() + 3s;
            while (r.fileCheck.active && Clock::now() < deadline) {
                if (p->session) p->alerts();
                p->pollFileChecks(true);
                std::this_thread::sleep_for(20ms);
            }
            if (r.fileCheck.active || r.checkRecord) {
                r.revision = revision;
                p->failFileCheck(r, tr("Torrent removal waits for verification recovery; no data was deleted."));
                p->publish(); return;
            }
        }
        r.revision = revision;
        if (deleteFiles && r.sourceReadOnly) {
            deleteFiles = false;
            p->report(tr("Torrent job removed; original source files were retained and cannot be deleted by this job."));
        }
        if (deleteFiles) {
            if (r.storageCollision) { p->report(storageCollisionError()); p->publish(); return; }
            const auto ti = r.handle.is_valid() ? r.handle.torrent_file() : r.resume.ti;
            if (!ti || !safeMetadata(*ti, r.savePath)) {
                p->report(tr("Torrent data deletion refused because its paths could not be validated.")); p->publish(); return;
            }
            if (!r.handle.is_valid()) { p->report(tr("Resume Torrent routing before requesting data deletion.")); p->publish(); return; }
            if (!p->claimStorage(r, *ti, r.savePath)) { p->publish(); return; }
        }
        if (r.handle.is_valid()) {
            p->removals.push_back({id, r.handle, r.handle.info_hashes(), deleteFiles});
            p->session->remove_torrent(r.handle, deleteFiles ? lt::session::delete_files : lt::remove_flags_t{});
        } else p->releaseStorage(id);
        p->records.erase(it);
        if (p->records.empty()) p->closeSession();
        p->dirty = true;
        p->publish();
    });
    emit changed();
}
void TorrentEngine::recheck(const QString &id) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->hasFileCheck(id)) { emit error(tr("Another recheck cannot start during selected-file verification.")); return; }
    emit storageInvalidating(id);
    emit hashRecheckRequested(id);
    const auto revision = d->invalidate(id);
    d->enqueue([p = d.get(), id, revision] {
        auto it = p->records.find(id);
        if (it == p->records.end()) return;
        auto &r = it->second;
        r.revision = revision;
        if (r.storageCollision) { p->publish(); return; }
        if (!r.handle.is_valid()) {
            p->report(tr("Recheck unavailable. Resume the Torrent first."));
            p->publish();
            return;
        }
        auto ti = r.handle.torrent_file();
        if (!ti || !safeMetadata(*ti, r.savePath)) { p->fail(r, tr("Unsafe Torrent storage path; recheck refused.")); return; }
        p->protectSource(r);
        if (r.sourceReadOnly && !p->sourceAvailable(*ti, r.savePath)) {
            p->fail(r, tr("Original Torrent source files are missing or changed size; recheck refused.")); return;
        }
        if (!p->invalidateResume(r)) { p->publish(); return; }
        r.notified = false;
        r.checking = true;
        r.failure.clear();
        r.seedStopped = false;
        p->publish();
        r.handle.force_recheck();
        p->requestSave(r);
    });
    emit changed();
}
void TorrentEngine::shutdown() {
    (void)shutdownChecked();
}
bool TorrentEngine::supportsFileRecheck() const {
#if EISKALT_SELECTED_RECHECK_AVAILABLE && !defined(Q_OS_WIN)
    return lt::eiskalt_selected_recheck_abi() == 1;
#else
    return false;
#endif
}
void TorrentEngine::recheckFiles(const QString &id, const QList<int> &indices, quint64 expectedRevision) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!supportsFileRecheck()) { emit error(tr("Selected-file checking is not available in this build.")); return; }
    std::unique_lock lock(d->mutex);
    const auto find = [&] { return std::find_if(d->snapshots.begin(), d->snapshots.end(), [&](const Job &job) { return job.id == id; }); };
    auto job = find();
    QString reason;
    if (d->stopped || d->commands.size() >= 1024) reason = tr("Torrent backend is shut down or its command queue is full.");
    else if (job == d->snapshots.end() || job->revision != expectedRevision) reason = tr("The Torrent selection changed; select the files again.");
    else if (job->fileCheck.active || d->checkAdmissions.contains(id) || d->mutationBusy.contains(id)) reason = tr("Selected-file verification conflicts with another operation.");
    else if (!job->error.isEmpty() && !job->fileCheck.recoveryRequired) reason = tr("Resolve Torrent recovery or storage errors before file verification.");
    else if (d->checkSerial == std::numeric_limits<quint64>::max() || expectedRevision == std::numeric_limits<quint64>::max()) reason = tr("Torrent verification generation is exhausted.");
    const auto metadata = d->metadataSnapshots.value(id);
    if (reason.isEmpty() && (!metadata || indices.size() > maxFiles)) reason = tr("Torrent metadata or file selection is unavailable.");
    RecheckPlanResult scope;
    if (reason.isEmpty()) { scope = planSelectedRecheck(*metadata, indices, expectedRevision); reason = scope.error; }
    if (reason.isEmpty() && job->fileCheck.recoveryRequired && std::any_of(job->fileCheck.files.cbegin(), job->fileCheck.files.cend(),
            [&](const FileCheckResult &file) { return !scope.plan->selectedFiles.contains(file.index); }))
        reason = tr("Recheck all files from the interrupted operation before checking a different selection.");
    if (!reason.isEmpty()) { lock.unlock(); emit error(reason); return; }
    d->checkAdmissions.insert(id);
    lock.unlock();
    emit storageInvalidating(id);
    lock.lock();
    d->checkAdmissions.remove(id);
    job = find();
    if (d->stopped || d->commands.size() >= 1024 || job == d->snapshots.end() || job->revision != expectedRevision) {
        lock.unlock(); emit error(tr("The Torrent selection changed before verification could start.")); return;
    }
    const auto revision = ++d->revisions[id];
    const auto token = ++d->checkSerial;
    scope.plan->revision = revision;
    job->revision = revision;
    job->complete = false;
    job->checking = true;
    job->downloadRate = job->uploadRate = 0;
    job->downloadingPeers = job->uploadingPeers = 0;
    job->fileCheck = {token, true, false, 0, scope.plan->totalBytes, {}, {}};
    for (int index : scope.plan->selectedFiles) job->fileCheck.files.append({index, FileCheckOutcome::Pending, {}});
    d->commands.push_back([p = d.get(), id, plan = std::move(*scope.plan), token, revision]() mutable {
        const auto it = p->records.find(id);
        if (it != p->records.end()) p->beginFileCheck(it->second, std::move(plan), token, revision);
    });
    if (d->observedJobId == id) { d->peerSnapshots.clear(); ++d->peerObservationEpoch; }
    lock.unlock();
    d->wake.notify_one();
    emit peersChanged(); emit changed();
}
void TorrentEngine::cancelFileRecheck(const QString &id, quint64 token) {
    Q_ASSERT(QThread::currentThread() == thread());
    std::unique_lock lock(d->mutex);
    const auto job = std::find_if(d->snapshots.begin(), d->snapshots.end(), [&](const Job &job) { return job.id == id; });
    if (!token || job == d->snapshots.end() || !job->fileCheck.active || job->fileCheck.token != token) return;
    if (d->stopped || d->commands.size() >= 1024) { lock.unlock(); emit error(tr("Torrent backend cannot accept cancellation.")); return; }
    job->fileCheck.canceling = true;
    d->commands.push_back([p = d.get(), id, token] {
        const auto it = p->records.find(id);
        if (it != p->records.end() && it->second.fileCheck.token == token) p->cancelCheck(it->second);
    });
    lock.unlock(); d->wake.notify_one(); emit changed();
}
bool TorrentEngine::shutdownChecked() {
    Q_ASSERT(QThread::currentThread() == thread());
    return !d || d->stop();
}

} // namespace eiskalt::torrent
