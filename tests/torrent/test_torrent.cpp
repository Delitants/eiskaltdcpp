#include <catch2/catch_test_macros.hpp>
#include "torrent/TorrentSettings.h"
#include "torrent/ProxyTrust.h"
#include "torrent/SocksUdpBootstrap.h"
#include "torrent/TorrentEngine.cpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <functional>
#include <future>
#include <atomic>
#include <libtorrent/magnet_uri.hpp>
#include "LocalSocksProxy.h"

namespace eiskalt::torrent {
struct TorrentEngineTestAccess {
    template<class F> static auto call(TorrentEngine &engine, F action) {
        using Result = decltype(action(*engine.d));
        auto result = std::make_shared<std::promise<Result>>();
        auto future = result->get_future();
        if (!engine.d->enqueue([p = engine.d.get(), action, result] {
            try { result->set_value(action(*p)); }
            catch (...) { result->set_exception(std::current_exception()); }
        })) throw std::runtime_error("test probe queue rejected");
        if (future.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
            throw std::runtime_error("test probe timeout");
        return future.get();
    }
    static bool active(TorrentEngine &engine) {
        return call(engine, [](auto &d) { return bool(d.session); });
    }
    static lt::settings_pack settings(TorrentEngine &engine) {
        return call(engine, [](auto &d) { return d.session->get_settings(); });
    }
    static bool connect(TorrentEngine &engine, const QString &id, int port) {
        return call(engine, [=](auto &d) {
            const auto it = d.records.find(id);
            if (it == d.records.end() || !it->second.handle.is_valid()) return false;
            it->second.handle.connect_peer({lt::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
            return true;
        });
    }
    static QStringList trackers(TorrentEngine &engine, const QString &id) {
        return call(engine, [=](auto &d) {
            QStringList urls;
            for (const auto &tracker : d.records.at(id).handle.trackers()) urls << QString::fromStdString(tracker.url);
            return urls;
        });
    }
    static int port(TorrentEngine &engine) {
        return call(engine, [](auto &d) { return d.session->listen_port(); });
    }
    static lt::torrent_status status(TorrentEngine &engine, const QString &id) {
        return call(engine, [=](auto &d) { return d.records.at(id).handle.status(); });
    }
    static std::size_t pendingCommands(TorrentEngine &engine) {
        std::lock_guard lock(engine.d->mutex);
        return engine.d->commands.size();
    }
};
}

using namespace eiskalt::torrent;

template<class Snapshot> QString exportedMagnet(const Snapshot &job) {
    if constexpr (requires { job.magnet; }) return job.magnet;
    else return {};
}

namespace {
void application() {
    static int argc = 1;
    static char name[] = "torrent-tests";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
}
bool eventually(const std::function<bool()> &condition, int timeout = 10000) {
    application();
    QElapsedTimer timer;
    timer.start();
    QEventLoop events;
    QTimer tick;
    tick.setSingleShot(true);
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, &events, &QEventLoop::quit);
    do {
        QCoreApplication::processEvents();
        if (condition()) return true;
        const auto remaining = qint64(timeout) - timer.elapsed();
        if (remaining <= 0) break;
        // The loopback proxy lives on this thread: sleeping stalls every
        // handshake and relay stage while the worker's deadline keeps running.
        tick.start(int(std::min<qint64>(20, remaining)));
        events.exec();
    } while (timer.elapsed() < timeout);
    return condition();
}
Settings isolated(const QString &path) {
    Settings s;
    s.downloadPath = path;
    s.proxyMode = ProxyMode::Direct;
    s.bindAddress = "127.0.0.1";
    s.bindAddress6.clear();
    s.listenPort = 49171;
    s.dht = s.pex = s.localDiscovery = s.portMapping = s.utp = false;
    s.bootstrapNodes.clear();
    s.seedRatio = 0;
    return s;
}
QString fixture(const QString &root, lt::create_flags_t flags = lt::create_torrent::v1_only,
                bool multi = false, const QStringList &trackers = {}, bool privateTorrent = true,
                const QStringList &names = {}, const QStringList &webSeeds = {}) {
    QDir().mkpath(root);
    std::vector<lt::create_file_entry> storage;
    const auto paths = names.isEmpty() ? (multi ? QStringList{"bundle/a.bin", "bundle/b.bin"} : QStringList{"payload.bin"}) : names;
    for (const auto &name : paths) {
        const auto path = QDir(root).filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error("fixture file");
        QByteArray data(131072, 'x');
        for (int i = 0; i < data.size(); ++i) data[i] = char((i * 17 + name.size()) % 251);
        file.write(data);
        storage.emplace_back(name.toStdString(), data.size());
    }
    lt::create_torrent torrent(storage, 16384, flags);
    for (const auto &seed : webSeeds) torrent.add_url_seed(seed.toStdString());
    torrent.set_priv(privateTorrent);
    for (const auto &tracker : trackers) torrent.add_tracker(tracker.toStdString());
    lt::error_code ec;
    lt::set_piece_hashes(torrent, root.toStdString(), ec);
    if (ec) throw std::runtime_error("fixture hashing");
    std::vector<char> bytes;
    lt::bencode(std::back_inserter(bytes), torrent.generate());
    const auto path = QDir(root).filePath("fixture.torrent");
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) throw std::runtime_error("fixture metadata");
    out.write(bytes.data(), bytes.size());
    return path;
}
QByteArray hashFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
Job jobSnapshot(const TorrentEngine &engine, const QString &id) {
    for (const auto &job : engine.jobs()) if (job.id == id) return job;
    return {};
}

class LocalUdpService {
public:
    enum Kind { Tracker, Dht };
    QUdpSocket socket;
    int packets = 0, connects = 0, announces = 0, queries = 0, invalid = 0;
    QSet<quint16> senderPorts;
    QList<QByteArray> announcedHashes;
    QByteArray lastDhtPacket;
    bool rejectDhtPing = false;

    explicit LocalUdpService(Kind kind) {
        socket.setProxy(QNetworkProxy::NoProxy);
        QObject::connect(&socket, &QUdpSocket::readyRead, &socket, [this, kind] {
            while (socket.hasPendingDatagrams()) {
                const auto packet = socket.receiveDatagram(65536);
                const auto &b = packet.data();
                ++packets;
                senderPorts.insert(packet.senderPort());
                if (packet.senderAddress() != QHostAddress::LocalHost) { ++invalid; continue; }
                QByteArray reply;
                if (kind == Tracker) {
                    // BEP 15 connect and announce, with a fixed fixture connection ID.
                    if (b.size() == 16 && b.left(12) == QByteArray::fromHex("000004172710198000000000")) {
                        ++connects;
                        reply = QByteArray::fromHex("00000000") + b.mid(12, 4) + QByteArray::fromHex("1122334455667788");
                    } else if (b.size() >= 98 && b.left(12) == QByteArray::fromHex("112233445566778800000001")) {
                        ++announces;
                        announcedHashes << b.mid(16, 20);
                        reply = QByteArray::fromHex("00000001") + b.mid(12, 4) + QByteArray::fromHex("0000003c0000000000000001");
                    } else { ++invalid; continue; }
                } else {
                    lastDhtPacket = b;
                    lt::error_code ec;
                    const auto request = lt::bdecode(lt::span<char const>(b.constData(), b.size()), ec);
                    if (ec || request.type() != lt::bdecode_node::dict_t) { ++invalid; continue; }
                    const auto args = request.dict_find_dict("a");
                    const auto query = request.dict_find_string_value("q");
                    const auto transaction = request.dict_find_string_value("t");
                    if (request.dict_find_string_value("y") != "q" || !args ||
                        args.dict_find_string_value("id").size() != 20 || transaction.empty() ||
                        (query != "ping" && query != "find_node" && query != "get_peers" && query != "announce_peer")) {
                        ++invalid;
                        continue;
                    }
                    if (query == "announce_peer") {
                        const auto infoHash = args.dict_find_string_value("info_hash");
                        if (args.dict_find_string_value("token") != "loopback-token" || infoHash.size() != 20 ||
                            args.dict_find_int_value("port") <= 0 || args.dict_find_int_value("port") > 65535) {
                            ++invalid;
                            continue;
                        }
                        ++announces;
                        announcedHashes << QByteArray(infoHash.data(), infoHash.size());
                    }
                    ++queries;
                    lt::entry response;
                    response["t"] = std::string(transaction);
                    if (rejectDhtPing && query == "ping") {
                        response["y"] = "e";
                        response["e"].list().emplace_back(203);
                        response["e"].list().emplace_back("Invalid node ID");
                    } else {
                        response["y"] = "r";
                        response["r"]["id"] = std::string(20, 'L');
                        if (query == "find_node" || query == "get_peers") response["r"]["nodes"] = "";
                        if (query == "get_peers") response["r"]["token"] = "loopback-token";
                    }
                    std::vector<char> encoded;
                    lt::bencode(std::back_inserter(encoded), response);
                    reply = QByteArray(encoded.data(), encoded.size());
                }
                if (socket.writeDatagram(reply, packet.senderAddress(), packet.senderPort()) != reply.size()) ++invalid;
            }
        });
    }
    bool bind() { return socket.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)); }
    bool onlyFrom(const LocalSocksProxy &proxy) const {
        for (const auto port : senderPorts) if (!proxy.udpEgressPorts.contains(port)) return false;
        return !senderPorts.isEmpty();
    }
};

ProxyConfig udpProxy(const LocalSocksProxy &bridge) {
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = bridge.server.serverPort();
    proxy.user = "fixture-user";
    proxy.password = "fixture-password";
    proxy.udp = true;
    return proxy;
}
}

TEST_CASE("Torrent fixture wait keeps queued network stages responsive", "[torrent][fixture-event-loop]") {
    application();
    QObject receiver;
    int stages = 0;
    std::function<void()> advance = [&] {
        if (++stages < 32) QMetaObject::invokeMethod(&receiver, advance, Qt::QueuedConnection);
    };
    QMetaObject::invokeMethod(&receiver, advance, Qt::QueuedConnection);
    const bool completed = eventually([&] { return stages == 32; }, 250);
    INFO("queued stages completed=" << stages);
    REQUIRE(completed);
}

TEST_CASE("Torrent fixture wait preserves predicate and deadline behavior", "[torrent][fixture-event-loop]") {
    application();
    SECTION("already satisfied") {
        REQUIRE(eventually([] { return true; }, 0));
    }
    SECTION("expired unsatisfied condition") {
        REQUIRE_FALSE(eventually([] { return false; }, 0));
    }
    SECTION("unsatisfied condition remains false") {
        REQUIRE_FALSE(eventually([] { return false; }, 30));
    }
    SECTION("predicate exceptions remain catchable") {
        REQUIRE_THROWS_AS(eventually([]() -> bool { throw std::runtime_error("fixture"); }), std::runtime_error);
    }
}

TEST_CASE("Torrent settings reject invalid values before saving", "[torrent][settings]") {
    Settings s;
    REQUIRE(validateSettings(s).isEmpty());
    for (int port : {0, -1, 65536}) {
        s.listenPort = port;
        REQUIRE_FALSE(validateSettings(s).isEmpty());
    }
    s = Settings{};
    s.downloadLimitKiB = -1;
    REQUIRE_FALSE(validateSettings(s).isEmpty());
    s = Settings{};
    s.uploadLimitKiB = 2147483647;
    REQUIRE_FALSE(validateSettings(s).isEmpty());
    s = Settings{};
    s.bindAddress = "not-an-ip";
    REQUIRE_FALSE(validateSettings(s).isEmpty());
    s = Settings{};
    s.tcp = s.utp = false;
    REQUIRE_FALSE(validateSettings(s).isEmpty());
}

TEST_CASE("Torrent security policy survives settings round trip", "[torrent][settings][security]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    REQUIRE(saveSettings(path, Settings{}));
    QFile input(path);
    REQUIRE(input.open(QIODevice::ReadOnly));
    auto document = QJsonDocument::fromJson(input.readAll()).object();
    input.close();
    document.insert("blockedCountries", QJsonArray{"UA", "US"});
    document.insert("blockUnknownClients", true);
    REQUIRE(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
    input.write(QJsonDocument(document).toJson());
    input.close();
    REQUIRE(saveSettings(path, loadSettings(path)));
    REQUIRE(input.open(QIODevice::ReadOnly));
    const auto persisted = QJsonDocument::fromJson(input.readAll()).object();
    REQUIRE(persisted.value("blockedCountries").toArray() == QJsonArray{"UA", "US"});
    REQUIRE(persisted.value("blockUnknownClients").toBool());
}

TEST_CASE("Security changes preserve proxy profiles and reject malformed country lists", "[torrent][settings][security]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.socks5Proxy.host = "proxy.invalid";
    settings.socks5Proxy.user = "fixture-user";
    settings.socks5Proxy.password = "fixture-password";
    settings.uploadLimitKiB = 17;
    settings.blockedCountries = {"UA", "US"};
    settings.blockUnknownClients = true;
    settings.encryptionMode = EncryptionMode::Required;
    REQUIRE(saveSettings(path, settings));
    auto restored = loadSettings(path);
    REQUIRE(restored.socks5Proxy.host == settings.socks5Proxy.host);
    REQUIRE(restored.socks5Proxy.password == settings.socks5Proxy.password);
    REQUIRE(restored.uploadLimitKiB == 17);
    REQUIRE(restored.encryptionMode == EncryptionMode::Required);
    for (const QStringList &bad : {QStringList{"UA", "UA"}, QStringList{"ua"}, QStringList{"U1"}, QStringList{"invalid"}}) {
        restored.blockedCountries = bad;
        REQUIRE_FALSE(saveSettings(path, restored));
        REQUIRE(loadSettings(path).blockedCountries == settings.blockedCountries);
    }
}

TEST_CASE("Peer policy caches local IPv4 and IPv6 countries and allows unmapped IPs", "[torrent][security]") {
    Settings settings;
    settings.blockedCountries = {"UA"};
    int lookups = 0;
    PeerPolicy policy(settings, [&](const QString &ip) {
        ++lookups;
        return ip == "::1" || ip == "127.0.0.1" ? QStringLiteral("UA") : QString{};
    });
    REQUIRE(policy.countryBlocked("::1"));
    REQUIRE(policy.countryBlocked("::1"));
    REQUIRE(lookups == 1);
    REQUIRE(policy.countryBlocked("127.0.0.1"));
    REQUIRE_FALSE(policy.countryBlocked("192.0.2.1"));
    REQUIRE_FALSE(policy.clientBlocked("Unknown"));
}

TEST_CASE("SOCKS5 status retains operation endpoint and error instead of generic failure", "[torrent][proxy-diagnostics]") {
    const lt::error_code error = boost::asio::error::connection_refused;
    const auto message = socks5Failure(lt::operation_t::connect, {lt::make_address("127.0.0.1"), 5541}, error);
    REQUIRE(message.contains("127.0.0.1:5541"));
    REQUIRE(message.contains(QString::fromStdString(error.message())));
    REQUIRE(message.contains(QString::fromLatin1(lt::operation_name(lt::operation_t::connect))));
    REQUIRE(message.contains("Direct fallback remains disabled"));
}

TEST_CASE("Exported Torrent magnets contain hashes and name but no tracker credentials", "[torrent][torrent-sharing]") {
    application();
    QTemporaryDir dir;
    lt::create_flags_t flags = lt::create_torrent::v1_only;
    bool v1 = true, v2 = false;
    SECTION("v1") {}
    SECTION("v2") { flags = lt::create_torrent::v2_only; v1 = false; v2 = true; }
    SECTION("hybrid") { flags = {}; v2 = true; }
    const auto path = fixture(dir.path() + "/source", flags, true,
                              {"https://tracker.invalid/private/passkey/announce"}, false);
    TorrentEngine engine(dir.path() + "/state");
    engine.configure(isolated(dir.path() + "/data"), {});
    const auto id = engine.add(path, {}, {}, true);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(eventually([&] { return !exportedMagnet(jobSnapshot(engine, id)).isEmpty(); }));
    const auto magnet = exportedMagnet(engine.jobs().front());
    INFO(magnet.toStdString());
    REQUIRE(magnet.startsWith("magnet:?"));
    REQUIRE_FALSE(magnet.contains("tracker.invalid"));
    REQUIRE_FALSE(magnet.contains("passkey"));
    const QUrlQuery query{QUrl(magnet)};
    REQUIRE(query.queryItems().size() == (v1 && v2 ? 3 : 2));
    const auto parsed = lt::parse_magnet_uri(magnet.toStdString());
    const auto source = lt::load_torrent_file(path.toStdString());
    REQUIRE(parsed.info_hashes == source.ti->info_hashes());
    REQUIRE(parsed.info_hashes.has_v1() == v1);
    REQUIRE(parsed.info_hashes.has_v2() == v2);
    REQUIRE_FALSE(query.queryItemValue("dn").isEmpty());
    engine.stop(id);
    REQUIRE(exportedMagnet(engine.jobs().front()) == magnet);
}

TEST_CASE("SOCKS5 relay control EOF is distinguished from a peer failure", "[torrent][proxy-diagnostics][torrent-controls]") {
    const lt::error_code error = boost::asio::error::eof;
    const auto message = socks5Failure(lt::operation_t::sock_read, {lt::make_address("127.0.0.1"), 5541}, error);
    REQUIRE(message.contains("UDP relay"));
    REQUIRE(message.contains("retry"));
    REQUIRE(message.contains("127.0.0.1:5541"));
    REQUIRE(message.contains("Direct fallback remains disabled"));
}

TEST_CASE("Retryable SOCKS5 relay warnings are not fatal engine errors", "[torrent][proxy-diagnostics]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.path());
    QStringList errors, diagnostics;
    QObject::connect(&engine, &TorrentEngine::error, &engine,
                     [&](const QString &message) { errors.append(message); });
    QObject::connect(&engine, &TorrentEngine::diagnostic, &engine,
                     [&](const QString &message) { diagnostics.append(message); });
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        d.reportSocks5(lt::operation_t::sock_read, {lt::make_address("127.0.0.1"), 5541},
                      boost::asio::error::eof);
        return true;
    }));
    REQUIRE(eventually([&] { return !diagnostics.isEmpty(); }));
    REQUIRE(errors.isEmpty());
    REQUIRE(diagnostics.front().contains("Direct fallback remains disabled"));
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        d.reportSocks5(lt::operation_t::connect, {lt::make_address("127.0.0.1"), 5541},
                      boost::asio::error::connection_refused);
        return true;
    }));
    REQUIRE(eventually([&] { return !errors.isEmpty(); }));
    REQUIRE(diagnostics.size() == 1);
}

TEST_CASE("Peer filters disable web seeds without changing unrestricted defaults", "[torrent][security][backend-review-fixes]") {
    application();
    QTemporaryDir dir;
    Settings settings;
    bool restricted = true;
    SECTION("Country blocklist") { settings.blockedCountries = {"UA"}; }
    SECTION("Unknown client block") { settings.blockUnknownClients = true; }
    SECTION("Both filters") { settings.blockedCountries = {"UA"}; settings.blockUnknownClients = true; }
    SECTION("No peer filters") { restricted = false; }
    for (const bool proxied : {false, true}) {
        ProxyConfig proxy;
        if (proxied) { proxy.type = ProxyType::Socks5; proxy.host = "127.0.0.1"; proxy.port = 9; }
        TorrentEngine engine(dir.filePath(proxied ? "proxy" : "direct"));
        const auto seeds = TorrentEngineTestAccess::call(engine, [=](auto &d) {
            d.config = settings;
            d.proxy = proxy;
            lt::add_torrent_params params;
            params.url_seeds = {"http://127.0.0.1:9/payload.bin"};
            d.routing(params);
            return std::pair{params.url_seeds, bool(params.flags & lt::torrent_flags::deprecated_override_web_seeds)};
        });
        if (restricted) {
            REQUIRE(seeds.first.empty());
            REQUIRE(seeds.second);
        } else {
            REQUIRE(seeds.first == std::vector<std::string>{"http://127.0.0.1:9/payload.bin"});
            REQUIRE_FALSE(seeds.second);
        }
    }
}

TEST_CASE("Libtorrent receives no metadata web seeds when peer filters are enabled", "[torrent][security][backend-review-fixes]") {
    application();
    QTemporaryDir dir;
    const QString url = "http://127.0.0.1:9/payload.bin";
    const auto metadata = fixture(dir.filePath("source"), lt::create_torrent::v1_only,
                                  false, {}, true, {}, {url});
    auto settings = isolated(dir.filePath("downloads"));
    bool restricted = true;
    SECTION("Country rule") { settings.blockedCountries = {"US"}; }
    SECTION("Client rule") { settings.blockUnknownClients = true; }
    SECTION("No rule preserves web seeds") { restricted = false; }
    TorrentEngine engine(dir.filePath("state"));
    const auto seeds = TorrentEngineTestAccess::call(engine, [=](auto &d) {
        d.config = settings;
        if (!d.createSession()) throw std::runtime_error("fixture session");
        auto params = lt::load_torrent_file(metadata.toStdString());
        params.save_path = settings.downloadPath.toStdString();
        d.routing(params);
        params.flags |= lt::torrent_flags::paused;
        return d.session->add_torrent(params).url_seeds();
    });
    if (restricted) REQUIRE(seeds.empty());
    else REQUIRE(seeds == std::set<std::string>{url.toStdString()});
}

TEST_CASE("Rejected configuration preserves the admitted policy and public generation", "[torrent][security][backend-review-fixes]") {
    application();
    QTemporaryDir dir;
    const auto settings = isolated(dir.filePath("source"));
    TorrentEngine engine(dir.filePath("state"));
    const auto epoch = engine.configure(settings, {});
    const auto id = engine.add(fixture(settings.downloadPath));
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    const auto revision = jobSnapshot(engine, id).revision;
    const auto session = TorrentEngineTestAccess::call(engine, [](auto &d) { return d.session.get(); });
    struct Gate { std::atomic_bool entered{false}, release{false}; };
    auto gate = std::make_shared<Gate>();
    struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release = true; } } release{gate};
    SECTION("Queue capacity") {
        REQUIRE(TorrentEngineTestAccess::call(engine, [gate](auto &d) {
            return d.enqueue([&d, gate] {
                {
                    std::lock_guard lock(d.mutex);
                    while (d.commands.size() < 1024) d.commands.push_back([] {});
                }
                gate->entered = true;
                while (!gate->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            });
        }));
        REQUIRE(eventually([&] { return gate->entered.load(); }));
    }
    SECTION("Stopped engine") { engine.shutdown(); }
    QCoreApplication::processEvents();
    int invalidations = 0, changes = 0;
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::storageInvalidating, &engine, [&](QString) { ++invalidations; });
    QObject::connect(&engine, &TorrentEngine::changed, &engine, [&] { ++changes; });
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString error) { errors << error; });
    auto requested = settings;
    requested.blockedCountries = {"UA"};
    requested.blockUnknownClients = true;
    REQUIRE(engine.configure(requested, {}) == 0);
    REQUIRE(engine.settings().blockedCountries.isEmpty());
    REQUIRE_FALSE(engine.settings().blockUnknownClients);
    REQUIRE(engine.configurationGeneration() == epoch);
    REQUIRE(jobSnapshot(engine, id).revision == revision);
    REQUIRE(invalidations == 0);
    REQUIRE(changes == 0);
    REQUIRE(errors.size() == 1);
    if (gate->entered) {
        gate->release = true;
        // Wait until admission is possible, without using a probe on the full queue.
        REQUIRE(eventually([&] { return TorrentEngineTestAccess::pendingCommands(engine) < 1024; }));
        REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) { return d.session.get(); }) == session);
    }
}

TEST_CASE("Stopped publications zero sampled peer counts", "[torrent][security][backend-review-fixes]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    int stop = 0;
    SECTION("User paused") { stop = 1; }
    SECTION("Seeding limit") { stop = 2; }
    SECTION("Libtorrent paused") { stop = 3; }
    SECTION("Job error") { stop = 4; }
    SECTION("Configuration error") { stop = 5; }
    REQUIRE(TorrentEngineTestAccess::call(engine, [stop](auto &d) {
        auto &record = d.records[QStringLiteral("counts")];
        record.id = "counts";
        record.status.flags = {};
        record.downloadingPeers = 4;
        record.uploadingPeers = 5;
        record.userPaused = stop == 1;
        record.seedStopped = stop == 2;
        if (stop == 3) record.status.flags |= lt::torrent_flags::paused;
        if (stop == 4) record.failure = "Disk unavailable";
        if (stop == 5) d.blocked = "Routing blocked";
        d.publish();
        return true;
    }));
    const auto job = jobSnapshot(engine, "counts");
    REQUIRE(job.id == "counts");
    REQUIRE(job.downloadingPeers == 0);
    REQUIRE(job.uploadingPeers == 0);
}

TEST_CASE("Public pause clears counts before the worker handles its command", "[torrent][security][backend-review-fixes]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    struct Gate { std::atomic_bool entered{false}, release{false}, published{false}, finish{false}; };
    auto gate = std::make_shared<Gate>();
    struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release = true; gate->finish = true; } } release{gate};
    REQUIRE(TorrentEngineTestAccess::call(engine, [gate](auto &d) {
        auto &record = d.records[QStringLiteral("counts")];
        record.id = "counts";
        record.status.flags = {};
        record.downloadingPeers = 4;
        record.uploadingPeers = 5;
        d.publish();
        return d.enqueue([&d, gate] {
            gate->entered = true;
            while (!gate->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            d.publish();
            gate->published = true;
            while (!gate->finish) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }));
    REQUIRE(eventually([&] { return gate->entered.load(); }));
    REQUIRE(jobSnapshot(engine, "counts").downloadingPeers == 4);
    REQUIRE(jobSnapshot(engine, "counts").uploadingPeers == 5);
    engine.pause("counts", true);
    REQUIRE(jobSnapshot(engine, "counts").downloadingPeers == 0);
    REQUIRE(jobSnapshot(engine, "counts").uploadingPeers == 0);
    bool expectPaused = true;
    bool expectStopped = false;
    SECTION("Pause remains pending") {}
    SECTION("Resume supersedes pending pause immediately") {
        engine.stop("counts");
        REQUIRE(jobSnapshot(engine, "counts").stopped);
        engine.pause("counts", false);
        expectPaused = false;
        REQUIRE_FALSE(jobSnapshot(engine, "counts").paused);
        REQUIRE_FALSE(jobSnapshot(engine, "counts").stopped);
    }
    SECTION("Stop supersedes pending Resume immediately") {
        engine.pause("counts", false);
        engine.stop("counts");
        expectStopped = true;
        REQUIRE(jobSnapshot(engine, "counts").paused);
        REQUIRE(jobSnapshot(engine, "counts").stopped);
    }
    gate->release = true;
    REQUIRE(eventually([&] { return gate->published.load(); }));
    REQUIRE(jobSnapshot(engine, "counts").downloadingPeers == 0);
    REQUIRE(jobSnapshot(engine, "counts").uploadingPeers == 0);
    REQUIRE(jobSnapshot(engine, "counts").paused == expectPaused);
    REQUIRE(jobSnapshot(engine, "counts").stopped == expectStopped);
    gate->finish = true;
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        return d.records.at(QStringLiteral("counts")).downloadingPeers == 0 &&
            d.records.at(QStringLiteral("counts")).uploadingPeers == 0;
    }));
}

TEST_CASE("Torrent settings persist atomically without losing the last valid document", "[torrent][settings]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("nested/settings.json");
    Settings s;
    s.downloadPath = dir.filePath("downloads");
    s.completedPath = dir.filePath("completed");
    s.seedRatio = 2.5;
    s.proxyMode = ProxyMode::RequireProxy;
    s.pex = false;
    s.shareCompleted = true;
    s.bootstrapNodes = "127.0.0.1:49001";
    QString error;
    REQUIRE(saveSettings(path, s, &error));
    REQUIRE(error.isEmpty());
    const auto loaded = loadSettings(path);
    REQUIRE(loaded.downloadPath == s.downloadPath);
    REQUIRE(loaded.completedPath == s.completedPath);
    REQUIRE(loaded.seedRatio == 2.5);
    REQUIRE(loaded.proxyMode == ProxyMode::RequireProxy);
    REQUIRE_FALSE(loaded.pex);
    REQUIRE(loaded.shareCompleted);
    REQUIRE(loaded.bootstrapNodes == s.bootstrapNodes);
    s.listenPort = 0;
    REQUIRE_FALSE(saveSettings(path, s, &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(loadSettings(path).seedRatio == 2.5);
}

TEST_CASE("Corrupt saved settings cannot silently enable networking", "[torrent][settings]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    REQUIRE(loadSettings(path).enabled);
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("{broken");
    f.close();
    REQUIRE_FALSE(loadSettings(path).enabled);
}

TEST_CASE("Torrent defaults enable public sharing and local discovery", "[torrent][settings][defaults]") {
    REQUIRE(Settings{}.shareCompleted);
    REQUIRE(Settings{}.localDiscovery);
    REQUIRE(Settings{}.bootstrapNodes == "dhtb.hublist.eu:6252");
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    REQUIRE(loadSettings(path).shareCompleted);
    REQUIRE(loadSettings(path).localDiscovery);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("{\"version\":1}");
    file.close();
    REQUIRE(loadSettings(path).shareCompleted);
    REQUIRE(loadSettings(path).localDiscovery);
}

TEST_CASE("Explicit saved OFF choices survive the new Torrent defaults", "[torrent][settings][defaults]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    Settings settings;
    settings.shareCompleted = false;
    settings.localDiscovery = false;
    REQUIRE(saveSettings(path, settings));
    REQUIRE_FALSE(loadSettings(path).shareCompleted);
    REQUIRE_FALSE(loadSettings(path).localDiscovery);
}

TEST_CASE("Only the exact former Torrent bootstrap default is migrated", "[torrent][settings][defaults]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    for (const QString &nodes : {QString("31.3.251.150:6252"), QString("31.3.251.150:6253"),
             QString("31.3.251.150:6252,example.test:6252"), QString(" 31.3.251.150:6252 "), QString()}) {
        Settings settings;
        settings.bootstrapNodes = nodes;
        REQUIRE(saveSettings(path, settings));
        REQUIRE(loadSettings(path).bootstrapNodes ==
            (nodes == "31.3.251.150:6252" ? QString("dhtb.hublist.eu:6252") : nodes));
    }
}

TEST_CASE("Random port preference defaults ON and preserves saved OFF", "[torrent][settings][defaults]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    REQUIRE(saveSettings(path, Settings{}));
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    REQUIRE(QJsonDocument::fromJson(file.readAll()).object()["randomizePort"].toBool());
    file.close();
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{\"version\":1,\"randomizePort\":false}");
    file.close();
    REQUIRE(saveSettings(path, loadSettings(path)));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(file.readAll()).object().value("randomizePort");
    REQUIRE(saved.isBool());
    REQUIRE_FALSE(saved.toBool());
}

TEST_CASE("Encryption choices persist and map both peer policies", "[torrent][settings][encryption]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    using P = lt::settings_pack;
    for (const auto [mode, policy] : {std::pair{0, P::pe_disabled}, {1, P::pe_enabled}, {2, P::pe_forced}}) {
        DYNAMIC_SECTION("mode " << mode) {
            QFile file(path);
            REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(QJsonDocument(QJsonObject{{"version", 1}, {"encryptionMode", mode}}).toJson());
            file.close();
            const auto settings = loadSettings(path);
            REQUIRE(settings.enabled);
            for (const bool proxied : {false, true}) {
                ProxyConfig proxy;
                if (proxied) { proxy.type = ProxyType::Socks5; proxy.host = "127.0.0.1"; proxy.port = 9; }
                const auto pack = sessionSettings(settings, proxy);
                REQUIRE(pack.get_int(P::in_enc_policy) == policy);
                REQUIRE(pack.get_int(P::out_enc_policy) == policy);
                REQUIRE(pack.get_int(P::allowed_enc_level) == (mode == 2 ? P::pe_rc4 : P::pe_both));
                REQUIRE(pack.get_bool(P::enable_lsd) == !proxied);
                if (proxied) {
                    REQUIRE(pack.get_str(P::listen_interfaces).empty());
                    REQUIRE_FALSE(pack.get_bool(P::enable_dht));
                    REQUIRE_FALSE(pack.get_bool(P::enable_incoming_tcp));
                    REQUIRE_FALSE(pack.get_bool(P::enable_incoming_utp));
                    REQUIRE_FALSE(pack.get_bool(P::enable_upnp));
                    REQUIRE_FALSE(pack.get_bool(P::enable_natpmp));
                }
            }
            REQUIRE(saveSettings(path, settings));
            REQUIRE(file.open(QIODevice::ReadOnly));
            REQUIRE(QJsonDocument::fromJson(file.readAll()).object()["encryptionMode"].toInt(-1) == mode);
        }
    }
    const auto defaults = sessionSettings(Settings{}, {});
    REQUIRE(defaults.get_int(P::in_enc_policy) == P::pe_enabled);
    REQUIRE(defaults.get_int(P::out_enc_policy) == P::pe_enabled);
    REQUIRE(defaults.get_int(P::allowed_enc_level) == P::pe_both);
}

TEST_CASE("Invalid encryption and randomization fields fail closed", "[torrent][settings][encryption]") {
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.json");
    for (const auto &value : {QJsonValue(-1), QJsonValue(3), QJsonValue(0.5), QJsonValue("1"), QJsonValue(true)}) {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"encryptionMode", value}}).toJson());
        file.close();
        REQUIRE_FALSE(loadSettings(path).enabled);
    }
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{\"version\":1,\"randomizePort\":1}");
    file.close();
    REQUIRE_FALSE(loadSettings(path).enabled);
}

TEST_CASE("Random listener port is ephemeral and stable across configure", "[torrent][listeners]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    auto settings = isolated(dir.filePath("downloads"));
    settings.listenPort = 6881;
    engine.configure(settings, {});
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    const auto id = engine.add(fixture(dir.filePath("source")));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::port(engine) != 0; }));
    const int port = TorrentEngineTestAccess::port(engine);
    REQUIRE(port >= 49152);
    REQUIRE(port <= 65535);
    settings.downloadLimitKiB = 16;
    engine.configure(settings, {});
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::port(engine) == port; }));
    REQUIRE(engine.settings().listenPort == 6881);
    REQUIRE(loadSettings(dir.filePath("state/settings.json")).listenPort == 6881);
    QTcpServer reservation;
    REQUIRE(reservation.listen(QHostAddress::LocalHost, 0));
    settings.randomizePort = false;
    settings.listenPort = reservation.serverPort();
    reservation.close();
    engine.configure(settings, {});
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::port(engine) == settings.listenPort; }));
    settings.randomizePort = true;
    engine.configure(settings, {});
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::port(engine) == port; }));
}

TEST_CASE("Listener snapshot follows actual dual-stack binds and clears on route changes", "[torrent][listeners]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    int changes = 0;
    QObject::connect(&engine, &TorrentEngine::listenersChanged, &engine, [&] { ++changes; });
    REQUIRE(engine.effectiveListeners().isEmpty());
    auto settings = isolated(dir.filePath("downloads"));
    settings.bindAddress6 = "::1";
    settings.utp = true;
    engine.configure(settings, {});
    REQUIRE(engine.effectiveListeners().isEmpty());
    const auto id = engine.add(fixture(dir.filePath("source")));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    REQUIRE(eventually([&] { return engine.effectiveListeners().size() == 4; }));
    const auto port = TorrentEngineTestAccess::port(engine);
    QStringList expected;
    for (const auto &protocol : {QString("TCP"), QString("UDP")})
        for (const auto &address : {QString("127.0.0.1"), QString("[::1]")})
            expected << protocol + ' ' + address + ':' + QString::number(port);
    expected.sort();
    REQUIRE(engine.effectiveListeners() == expected);
    REQUIRE(changes > 0);
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, port);
    REQUIRE(probe.waitForConnected(2000));
    probe.abort();
    auto snapshot = std::async(std::launch::async, [&] { return engine.effectiveListeners(); });
    REQUIRE(snapshot.get() == expected);
    settings.proxyMode = ProxyMode::RequireProxy;
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = 9;
    engine.configure(settings, proxy);
    REQUIRE(engine.effectiveListeners().isEmpty());
    REQUIRE(TorrentEngineTestAccess::settings(engine).get_str(lt::settings_pack::listen_interfaces).empty());
    REQUIRE(engine.effectiveListeners().isEmpty());
    engine.shutdown();
    REQUIRE(engine.effectiveListeners().isEmpty());
}

TEST_CASE("Occupied fixed listener is never reported as successful or moved silently", "[torrent][listeners]") {
    application();
    QTcpServer tcp;
    REQUIRE(tcp.listen(QHostAddress::LocalHost, 0));
    QUdpSocket udp;
    REQUIRE(udp.bind(QHostAddress::LocalHost, tcp.serverPort(), QUdpSocket::DontShareAddress));
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    auto settings = isolated(dir.filePath("downloads"));
    settings.randomizePort = false;
    settings.listenPort = tcp.serverPort();
    settings.utp = true;
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString error) { errors << error; });
    engine.configure(settings, {});
    const auto id = engine.add(fixture(dir.filePath("source")));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty() && errors.join(' ').contains("bind"); }));
    REQUIRE(engine.effectiveListeners().isEmpty());
    REQUIRE(TorrentEngineTestAccess::port(engine) == 0);
    const auto policy = TorrentEngineTestAccess::settings(engine);
    REQUIRE(policy.get_int(lt::settings_pack::max_retry_port_bind) == 0);
    REQUIRE_FALSE(policy.get_bool(lt::settings_pack::listen_system_port_fallback));
    tcp.close();
    udp.close();
    engine.configure(settings, {});
    REQUIRE(eventually([&] { return engine.effectiveListeners().size() == 2; }));
    REQUIRE(TorrentEngineTestAccess::port(engine) == settings.listenPort);
    engine.shutdown();
    REQUIRE(engine.effectiveListeners().isEmpty());
}

TEST_CASE("Engine rejects missing or unsupported proxy before accepting work", "[torrent][proxy]") {
    application();
    QTemporaryDir dir;
    const auto metadata = fixture(dir.filePath("source"));
    TorrentEngine engine(dir.filePath("state"));
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString e) { errors << e; });
    auto s = isolated(dir.filePath("download"));
    s.proxyMode = ProxyMode::RequireProxy;
    engine.configure(s, {});
    REQUIRE(engine.add(metadata).isEmpty());
    REQUIRE(engine.jobs().isEmpty());
    REQUIRE_FALSE(errors.isEmpty());
    for (auto type : {ProxyType::Shadowsocks, ProxyType::Socks5Tls, ProxyType::Socks5}) {
        ProxyConfig proxy;
        proxy.type = type;
        proxy.password = "do-not-leak-this-password";
        errors.clear();
        engine.configure(s, proxy);
        REQUIRE(engine.add(metadata).isEmpty());
        REQUIRE_FALSE(errors.isEmpty());
        REQUIRE_FALSE(errors.join(' ').contains(proxy.password));
    }
    engine.shutdown();
    engine.shutdown();
}

TEST_CASE("Invalid input and shutdown reject jobs visibly", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString e) { errors << e; });
    engine.configure(isolated(dir.filePath("downloads")), {});
    REQUIRE(engine.jobs().isEmpty());
    for (const auto &input : {"magnet:?xt=urn:tree:tiger:ABC", "magnet:?xt=urn:btih:bad", "/does/not/exist.torrent"}) {
        errors.clear();
        REQUIRE(engine.add(input).isEmpty());
        REQUIRE_FALSE(errors.isEmpty());
    }
    engine.shutdown();
    errors.clear();
    REQUIRE(engine.add("magnet:?xt=urn:btih:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").isEmpty());
    REQUIRE_FALSE(errors.isEmpty());
}

TEST_CASE("Private metadata and explicit empty selection survive restart", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    const auto metadata = fixture(dir.filePath("source"), lt::create_torrent::v2_only, true);
    auto s = isolated(dir.filePath("downloads"));
    QString id;
    {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(s, {});
        id = engine.add(metadata, {}, {0});
        REQUIRE_FALSE(id.isEmpty());
        REQUIRE(eventually([&] { return engine.files(id).size() == 2; }));
        REQUIRE(engine.jobs().first().privateTorrent);
        REQUIRE(engine.files(id)[0].wanted);
        REQUIRE_FALSE(engine.files(id)[1].wanted);
        engine.setWantedFiles(id, {});
        REQUIRE(eventually([&] { return !engine.files(id)[0].wanted; }));
        engine.pause(id, true);
        REQUIRE(eventually([&] { return engine.jobs().first().paused; }));
        engine.shutdown();
    }
    TorrentEngine restored(dir.filePath("state"));
    REQUIRE(restored.jobs().isEmpty());
    restored.configure(s, {});
    REQUIRE(eventually([&] { return restored.files(id).size() == 2; }));
    REQUIRE(restored.jobs().first().paused);
    REQUIRE_FALSE(restored.files(id)[0].wanted);
    REQUIRE_FALSE(restored.files(id)[1].wanted);
    restored.remove(id);
    REQUIRE(eventually([&] { return restored.jobs().isEmpty(); }));
    restored.shutdown();
    TorrentEngine empty(dir.filePath("state"));
    empty.configure(s, {});
    QThread::msleep(100);
    REQUIRE(empty.jobs().isEmpty());
}

TEST_CASE("Constructor and empty configuration are network lazy", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    engine.configure(Settings{}, {});
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    engine.configure(isolated(dir.filePath("downloads")), {});
    const auto id = engine.add(fixture(dir.filePath("source")));
    REQUIRE(eventually([&] { return engine.files(id).size() == 1; }));
    REQUIRE(TorrentEngineTestAccess::active(engine));
    engine.remove(id);
    REQUIRE(eventually([&] { return !TorrentEngineTestAccess::active(engine); }));
}

TEST_CASE("WebTorrent paths stay disabled outside the supported proxy contract", "[torrent][proxy][routing]") {
    application();
    for (bool proxied : {false, true}) {
        DYNAMIC_SECTION("proxied " << proxied) {
            QTemporaryDir dir;
            auto s = isolated(dir.filePath("downloads"));
            ProxyConfig proxy;
            if (proxied) {
                s.proxyMode = ProxyMode::RequireProxy;
                proxy.type = ProxyType::Socks5;
                proxy.host = "127.0.0.1";
                proxy.port = 9;
            }
            const auto policy = sessionSettings(s, proxy);
            REQUIRE(policy.get_int(lt::settings_pack::max_webtorrent_offers) == 0);
            REQUIRE(policy.get_str(lt::settings_pack::webtorrent_stun_server).empty());
            TorrentEngine engine(dir.filePath("state"));
            engine.configure(s, proxy);
            const QStringList trackers{"ws://fixture.invalid/announce", "wss://fixture.invalid/announce",
                                       "http://fixture.invalid/announce", "https://fixture.invalid/announce",
                                       "udp://fixture.invalid:80/announce"};
            const auto id = engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only,
                                               false, trackers), {}, {}, true);
            REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
            const auto routed = TorrentEngineTestAccess::call(engine, [id](auto &d) { return d.records.at(id).resume; });
            QStringList urls;
            for (const auto &url : routed.trackers) urls << QString::fromStdString(url);
            QStringList expected{trackers[2], trackers[3]};
            if (!proxied) expected << trackers[4];
            REQUIRE(urls == expected);
            REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
        }
    }
}

TEST_CASE("Local v1 v2 hybrid swarms verify selected bytes and restart", "[torrent][swarm]") {
    application();
    for (int format = 0; format < 3; ++format) {
        DYNAMIC_SECTION("format " << format) {
            QTemporaryDir dir;
            const auto flags = format == 0 ? lt::create_torrent::v1_only :
                format == 1 ? lt::create_torrent::v2_only : lt::create_flags_t{};
            const bool multi = format != 0;
            const auto source = dir.filePath("source");
            const auto metadata = fixture(source, flags, multi);
            auto seedSettings = sessionSettings(isolated(source), {});
            seedSettings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
            lt::session seed{lt::session_params(seedSettings)};
            auto params = lt::load_torrent_file(metadata.toStdString());
            params.save_path = source.toStdString();
            params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
            const auto seedHandle = seed.add_torrent(params);
            REQUIRE(eventually([&] { return seedHandle.status().is_seeding && seed.listen_port() != 0; }));
            auto s = isolated(dir.filePath("download"));
            s.completedPath = dir.filePath("finished");
            QString id;
            QString completedPath;
            {
                TorrentEngine engine(dir.filePath("state"));
                QStringList completed;
                bool privateFlag = false;
                QObject::connect(&engine, &TorrentEngine::completed, &engine,
                    [&](QString, QStringList files, bool priv) { completed = files; privateFlag = priv; });
                engine.configure(s, {});
                id = engine.add(metadata, {}, multi ? QList<int>{0} : QList<int>{});
                REQUIRE_FALSE(id.isEmpty());
                REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
                engine.pause(id, true);
                REQUIRE(eventually([&] { return engine.jobs().first().state == "Paused"; }));
                engine.pause(id, false);
                REQUIRE(eventually([&] { return !engine.jobs().first().paused; }));
                REQUIRE(TorrentEngineTestAccess::connect(engine, id, seed.listen_port()));
                REQUIRE(eventually([&] { return !completed.isEmpty(); }, 30000));
                REQUIRE(privateFlag);
                REQUIRE(completed.size() == 1);
                completedPath = completed.first();
                const QString relative = multi ? "bundle/a.bin" : "payload.bin";
                REQUIRE(QFileInfo(completedPath).canonicalFilePath() == QFileInfo(QDir(s.completedPath).filePath(relative)).canonicalFilePath());
                REQUIRE(hashFile(completedPath) == hashFile(QDir(source).filePath(relative)));
                REQUIRE(hashFile(completedPath).size() == 32);
                if (multi) REQUIRE_FALSE(QFileInfo::exists(QDir(s.completedPath).filePath("bundle/b.bin")));
                REQUIRE(eventually([&] { return engine.jobs().first().complete; }));
                engine.pause(id, true);
                REQUIRE(eventually([&] { return engine.jobs().first().paused; }));
                engine.shutdown();
            }
            TorrentEngine restored(dir.filePath("state"));
            restored.configure(s, {});
            REQUIRE(eventually([&] { return !restored.files(id).isEmpty(); }));
            REQUIRE(restored.jobs().first().paused);
            REQUIRE(eventually([&] { return restored.jobs().first().complete; }));
            restored.remove(id, false);
            REQUIRE(eventually([&] { return restored.jobs().isEmpty(); }));
            REQUIRE(QFileInfo::exists(completedPath));
        }
    }
}

TEST_CASE("TCP-only SOCKS5 disables UDP and never falls back when proxy fails", "[torrent][proxy]") {
    application();
    QTemporaryDir dir;
    QTcpServer proxy, tracker, peer;
    QUdpSocket udpTracker;
    REQUIRE(proxy.listen(QHostAddress::LocalHost));
    REQUIRE(tracker.listen(QHostAddress::LocalHost));
    REQUIRE(peer.listen(QHostAddress::LocalHost));
    REQUIRE(udpTracker.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    int attempts = 0;
    QObject::connect(&proxy, &QTcpServer::newConnection, [&] {
        while (proxy.hasPendingConnections()) {
            auto *socket = proxy.nextPendingConnection();
            ++attempts;
            socket->abort();
            socket->deleteLater();
        }
    });
    const auto http = QString("http://127.0.0.1:%1/announce").arg(tracker.serverPort());
    const auto udp = QString("udp://127.0.0.1:%1/announce").arg(udpTracker.localPort());
    auto s = isolated(dir.filePath("download"));
    s.proxyMode = ProxyMode::RequireProxy;
    s.dht = s.utp = s.localDiscovery = s.portMapping = true;
    ProxyConfig cfg;
    cfg.type = ProxyType::Socks5;
    cfg.host = "127.0.0.1";
    cfg.port = proxy.serverPort();
    cfg.user = "session-user";
    cfg.password = "session-secret";
    TorrentEngine engine(dir.filePath("state"));
    QStringList diagnostics, errors;
    QObject::connect(&engine, &TorrentEngine::diagnostic, &engine, [&](QString message) { diagnostics << message; });
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString message) { errors << message; });
    engine.configure(s, cfg);
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    const auto id = engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only, false,
        {http, udp, "ws://127.0.0.1:9/announce", "wss://127.0.0.1:9/announce"}));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    REQUIRE(eventually([&] {
        return diagnostics.contains("Torrent proxy has no UDP support: DHT, uTP and UDP trackers are disabled.");
    }));
    REQUIRE_FALSE(errors.contains("Torrent proxy has no UDP support: DHT, uTP and UDP trackers are disabled."));
    const auto actual = TorrentEngineTestAccess::settings(engine);
    for (int setting : {lt::settings_pack::enable_dht, lt::settings_pack::enable_incoming_utp,
         lt::settings_pack::enable_outgoing_utp, lt::settings_pack::enable_incoming_tcp,
         lt::settings_pack::enable_lsd, lt::settings_pack::enable_upnp, lt::settings_pack::enable_natpmp})
        REQUIRE_FALSE(actual.get_bool(setting));
    REQUIRE(actual.get_bool(lt::settings_pack::proxy_peer_connections));
    REQUIRE(actual.get_bool(lt::settings_pack::proxy_tracker_connections));
    REQUIRE(actual.get_bool(lt::settings_pack::proxy_hostnames));
    REQUIRE(actual.get_int(lt::settings_pack::proxy_type) == lt::settings_pack::socks5_pw);
    REQUIRE(TorrentEngineTestAccess::trackers(engine, id) == QStringList{http});
    REQUIRE(eventually([&] { return !engine.jobs().first().paused; }));
    REQUIRE(TorrentEngineTestAccess::connect(engine, id, peer.serverPort()));
    REQUIRE(eventually([&] { return attempts > 0; }));
    REQUIRE_FALSE(eventually([&] { return tracker.hasPendingConnections() || peer.hasPendingConnections() || udpTracker.hasPendingDatagrams(); }, 1500));
    REQUIRE(engine.jobs().first().downloaded == 0);
    cfg.type = ProxyType::Shadowsocks;
    engine.configure(s, cfg);
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    REQUIRE_FALSE(engine.jobs().first().error.isEmpty());
}

TEST_CASE("Authenticated SOCKS5 carries magnet metadata payload and remote tracker names", "[torrent][swarm][proxy]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    QTcpServer tracker;
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    REQUIRE(tracker.listen(QHostAddress::LocalHost));
    QObject::connect(&tracker, &QTcpServer::newConnection, [&] {
        while (tracker.hasPendingConnections()) {
            auto *socket = tracker.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 25\r\nConnection: close\r\n\r\nd8:intervali60e5:peers0:e");
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const auto source = dir.filePath("source");
    const auto file = fixture(source, lt::create_torrent::v1_only, false, {}, false);
    auto settings = sessionSettings(isolated(source), {});
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    lt::session seed{lt::session_params(settings)};
    auto params = lt::load_torrent_file(file.toStdString());
    params.save_path = source.toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    auto handle = seed.add_torrent(params);
    REQUIRE(eventually([&] { return handle.status().is_seeding; }));
    params.trackers.push_back(QString("http://fixture.invalid:%1/announce").arg(tracker.serverPort()).toStdString());
    const auto magnet = QString::fromStdString(lt::make_magnet_uri(params));
    auto s = isolated(dir.filePath("download"));
    s.proxyMode = ProxyMode::RequireProxy;
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = bridge.server.serverPort();
    proxy.user = "fixture-user";
    proxy.password = "fixture-password";
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(s, proxy);
    auto id = engine.add(magnet);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().state == "Fetching metadata" && !engine.jobs().first().paused; }));
    REQUIRE(TorrentEngineTestAccess::connect(engine, id, seed.listen_port()));
    const bool finished = eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete; }, 30000);
    INFO("state=" << engine.jobs().first().state.toStdString() << " peers=" << engine.jobs().first().peers
         << " bytes=" << engine.jobs().first().downloaded << " authenticated=" << bridge.authenticated
         << " forwarded=" << bridge.forwarded << " files=" << engine.files(id).size());
    REQUIRE(finished);
    REQUIRE_FALSE(engine.jobs().first().privateTorrent);
    REQUIRE(hashFile(dir.filePath("download/payload.bin")) == hashFile(dir.filePath("source/payload.bin")));
    REQUIRE(bridge.authenticated > 0);
    REQUIRE(bridge.forwarded > 0);
    REQUIRE(eventually([&] { return bridge.domainRequests > 0; }));
}

TEST_CASE("SOCKS5 UDP fixture restricts destinations and expires with TCP", "[torrent][proxy][socks-udp-fixture]") {
    application();
    LocalSocksProxy bridge;
    bridge.udpEnabled = true;
    QUdpSocket destination, deniedDestination, sender;
    REQUIRE(destination.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    REQUIRE(deniedDestination.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    REQUIRE(sender.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    bridge.udpAllowedPorts.insert(destination.localPort());
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    QTcpSocket control;
    control.connectToHost(QHostAddress::LocalHost, bridge.server.serverPort());
    REQUIRE(eventually([&] { return control.state() == QAbstractSocket::ConnectedState; }));
    control.write(QByteArray::fromHex("050102"));
    REQUIRE(eventually([&] { return control.bytesAvailable() == 2; }));
    REQUIRE(control.readAll() == QByteArray::fromHex("0502"));
    control.write(QByteArray::fromHex("010c") + "fixture-user" + QByteArray::fromHex("10") + "fixture-password");
    REQUIRE(eventually([&] { return control.bytesAvailable() == 2; }));
    REQUIRE(control.readAll() == QByteArray::fromHex("0100"));
    control.write(QByteArray::fromHex("05030001000000000000"));
    REQUIRE(eventually([&] { return control.bytesAvailable() == 10; }));
    const auto reply = control.readAll();
    REQUIRE(quint8(reply[1]) == 0);
    REQUIRE(reply.left(8) == QByteArray::fromHex("050000017f000001"));
    const quint16 relayPort = quint16((quint8(reply[8]) << 8) | quint8(reply[9]));
    auto envelope = [](quint16 port) {
        auto bytes = QByteArray::fromHex("000000017f000001");
        bytes.append(char(port >> 8));
        bytes.append(char(port & 0xff));
        return bytes + "fixture-datagram";
    };
    const auto valid = envelope(destination.localPort());
    REQUIRE(sender.writeDatagram(valid, QHostAddress::LocalHost, relayPort) == valid.size());
    REQUIRE(eventually([&] { return destination.hasPendingDatagrams(); }));
    const auto forwarded = destination.receiveDatagram();
    REQUIRE(forwarded.data() == "fixture-datagram");
    REQUIRE(bridge.udpEgressPorts.contains(forwarded.senderPort()));
    REQUIRE(destination.writeDatagram("response", forwarded.senderAddress(), forwarded.senderPort()) == 8);
    REQUIRE(eventually([&] { return sender.hasPendingDatagrams(); }));
    REQUIRE(sender.receiveDatagram().data() == valid.left(10) + "response");
    // proxy_hostnames may encode even a numeric tracker address as ATYP=DOMAIN.
    for (const auto &name : {QByteArray("127.0.0.1"), QByteArray("fixture.invalid")}) {
        const auto packet = QByteArray::fromHex("00000003") + char(name.size()) + name + valid.mid(8);
        REQUIRE(sender.writeDatagram(packet, QHostAddress::LocalHost, relayPort) == packet.size());
        REQUIRE(eventually([&] { return destination.hasPendingDatagrams(); }));
        REQUIRE(destination.receiveDatagram().data() == "fixture-datagram");
    }
    QList<QByteArray> denied{envelope(deniedDestination.localPort()), valid.left(7)};
    auto nonLoopback = valid;
    nonLoopback.replace(4, 4, QByteArray::fromHex("c0000201"));
    denied << nonLoopback;
    auto fragmented = valid;
    fragmented[2] = 1;
    denied << fragmented;
    denied << QByteArray::fromHex("00000004") + QByteArray(15, '\0') + char(1) + valid.mid(8);
    denied << QByteArray::fromHex("000000030d") + "other.invalid" + valid.mid(8);
    for (const auto &packet : denied)
        REQUIRE(sender.writeDatagram(packet, QHostAddress::LocalHost, relayPort) == packet.size());
    REQUIRE(eventually([&] { return bridge.udpDropped == denied.size(); }));
    REQUIRE_FALSE(destination.hasPendingDatagrams());
    REQUIRE_FALSE(deniedDestination.hasPendingDatagrams());
    QUdpSocket unrelated;
    REQUIRE(unrelated.bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
    REQUIRE(unrelated.writeDatagram(valid, QHostAddress::LocalHost, relayPort) == valid.size());
    REQUIRE(eventually([&] { return bridge.udpDropped == denied.size() + 1; }));
    control.disconnectFromHost();
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
    sender.writeDatagram(valid, QHostAddress::LocalHost, relayPort);
    REQUIRE_FALSE(eventually([&] { return destination.hasPendingDatagrams(); }, 250));
}

TEST_CASE("Authenticated SOCKS5 UDP carries tracker announces and DHT replies", "[torrent][proxy][socks-udp]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    LocalUdpService tracker(LocalUdpService::Tracker), dht(LocalUdpService::Dht);
    REQUIRE(tracker.bind());
    REQUIRE(dht.bind());
    bridge.udpEnabled = true;
    bridge.udpAllowedPorts = {tracker.socket.localPort(), dht.socket.localPort()};
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = QString("127.0.0.1:%1").arg(dht.socket.localPort());
    bool namedBootstrap = false;
    SECTION("numeric bootstrap") {}
    SECTION("hostname bootstrap is resolved through the production helper") {
        namedBootstrap = true;
        settings.bootstrapNodes = QString("fixture.invalid:%1").arg(dht.socket.localPort());
    }
    const auto url = QString("udp://127.0.0.1:%1/announce").arg(tracker.socket.localPort());
    const auto metadata = fixture(dir.filePath("source"), lt::create_torrent::v1_only, false, {url}, false);
    const auto hash = lt::load_torrent_file(metadata.toStdString()).ti->info_hashes().v1;
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(settings, udpProxy(bridge));
    const auto id = engine.add(metadata);
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    const bool associated = eventually([&] { return bridge.udpAssociated > 0; }, 5000);
    INFO("authenticated=" << bridge.authenticated << " associated=" << bridge.udpAssociated
         << " tracker packets=" << tracker.packets << " DHT packets=" << dht.packets);
    REQUIRE(associated);
    if (namedBootstrap) {
        REQUIRE(bridge.udpDomainNames.contains("fixture.invalid"));
        REQUIRE(engine.settings().bootstrapNodes == settings.bootstrapNodes);
        REQUIRE(TorrentEngineTestAccess::settings(engine).get_str(lt::settings_pack::dht_bootstrap_nodes) ==
            QString("127.0.0.1:%1").arg(dht.socket.localPort()).toStdString());
    }
    REQUIRE(TorrentEngineTestAccess::call(engine, [&](auto &d) {
        d.session->add_dht_node({"127.0.0.1", dht.socket.localPort()});
        d.records.at(id).handle.force_reannounce();
        return true;
    }));
    const bool exchanged = eventually([&] {
        return tracker.announces > 0 && dht.announces > 0 &&
            bridge.udpReturned.value(tracker.socket.localPort()) > 0 && bridge.udpReturned.value(dht.socket.localPort()) > 0;
    }, 15000);
    INFO("tracker connects=" << tracker.connects << " announces=" << tracker.announces
         << " DHT queries=" << dht.queries << " DHT announces=" << dht.announces
         << " tracker replies=" << bridge.udpReturned.value(tracker.socket.localPort())
         << " DHT replies=" << bridge.udpReturned.value(dht.socket.localPort()) << " dropped=" << bridge.udpDropped);
    REQUIRE(exchanged);
    REQUIRE(tracker.invalid == 0);
    INFO("last DHT packet=" << dht.lastDhtPacket.toHex().toStdString());
    REQUIRE(dht.invalid == 0);
    REQUIRE(tracker.onlyFrom(bridge));
    REQUIRE(dht.onlyFrom(bridge));
    REQUIRE(tracker.announcedHashes.contains(QByteArray(hash.data(), 20)));
    REQUIRE(dht.announcedHashes.contains(QByteArray(hash.data(), 20)));
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::call(engine, [&](auto &d) {
        const auto trackers = d.records.at(id).handle.trackers();
        return !trackers.empty() && trackers.front().verified;
    }); }));
    REQUIRE(engine.effectiveListeners().isEmpty());
    engine.shutdown();
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("Rejected SOCKS5 UDP never falls back to direct tracker or DHT datagrams", "[torrent][proxy][socks-udp]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    LocalUdpService tracker(LocalUdpService::Tracker), dht(LocalUdpService::Dht);
    REQUIRE(tracker.bind());
    REQUIRE(dht.bind());
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = QString("127.0.0.1:%1").arg(dht.socket.localPort());
    const auto url = QString("udp://127.0.0.1:%1/announce").arg(tracker.socket.localPort());
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(settings, udpProxy(bridge));
    const auto id = engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only, false, {url}, false));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    REQUIRE(eventually([&] { return bridge.udpRejected > 0; }, 5000));
    REQUIRE(bridge.authenticated > 0);
    REQUIRE(TorrentEngineTestAccess::call(engine, [&](auto &d) {
        d.session->add_dht_node({"127.0.0.1", dht.socket.localPort()});
        d.records.at(id).handle.force_reannounce();
        return true;
    }));
    REQUIRE_FALSE(eventually([&] { return tracker.packets > 0 || dht.packets > 0; }, 3000));
    INFO("authenticated=" << bridge.authenticated << " rejected=" << bridge.udpRejected
         << " direct tracker packets=" << tracker.packets << " direct DHT packets=" << dht.packets);
    REQUIRE(tracker.packets == 0);
    REQUIRE(dht.packets == 0);
    REQUIRE(bridge.udpAssociated == 0);
    REQUIRE(engine.jobs().first().downloaded == 0);
}

TEST_CASE("SOCKS5 never passes DHT bootstrap hostnames to the libtorrent resolver", "[torrent][proxy][bootstrap-privacy]") {
    application();
    QTemporaryDir dir;
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = "fixture.invalid:6252,127.0.0.1:6252,[::1]:6252";
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = 9;
    proxy.udp = true;
    SECTION("session bootstrap list") {
        REQUIRE(sessionSettings(settings, proxy).get_str(lt::settings_pack::dht_bootstrap_nodes) ==
            "127.0.0.1:6252,[::1]:6252");
        settings.bootstrapNodes = " 127.0.0.1 :6252,[0:0:0:0:0:0:0:1]:6253,fixture.invalid:6252";
        REQUIRE(sessionSettings(settings, proxy).get_str(lt::settings_pack::dht_bootstrap_nodes) ==
            "127.0.0.1:6252,[::1]:6253");
    }
    SECTION("metadata and resume bootstrap list") {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(settings, proxy);
        const auto routed = TorrentEngineTestAccess::call(engine, [](auto &d) {
            lt::add_torrent_params params;
            params.dht_nodes = {{"fixture.invalid", 6252}, {" 127.0.0.1 ", 6252},
                                {"0:0:0:0:0:0:0:1", 6252}, {"127.0.0.1", 0},
                                {"::1%en0", 6252}, {"127.0.0.1", 65536}};
            d.routing(params);
            return params.dht_nodes;
        });
        const std::vector<std::pair<std::string, int>> expected{{"127.0.0.1", 6252}, {"::1", 6252}};
        REQUIRE(routed == expected);
        REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    }
}

TEST_CASE("SOCKS5 DHT bootstrap recovers after a temporary proxy failure", "[torrent][proxy][bootstrap-retry]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    // A transient transport failure must not claim that UDP is unsupported.
    bridge.udpTransientFailure = true;
    LocalUdpService dht(LocalUdpService::Dht);
    REQUIRE(dht.bind());
    bridge.udpAllowedPorts.insert(dht.socket.localPort());
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = QString("fixture.invalid:%1").arg(dht.socket.localPort());
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(settings, udpProxy(bridge));
    const auto id = engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only, false, {}, false));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    // Observe both the bootstrap helper and libtorrent fail before recovery.
    REQUIRE(eventually([&] { return bridge.udpRejected >= 2; }));
    REQUIRE(dht.packets == 0);
    const auto initialSession = TorrentEngineTestAccess::call(engine, [](auto &d) {
        return reinterpret_cast<quintptr>(d.session.get());
    });
    const auto generation = engine.configurationGeneration();
    bridge.udpEnabled = true;
    const auto expected = QString("127.0.0.1:%1").arg(dht.socket.localPort()).toStdString();
    // Do not synchronously query the worker while it awaits this thread's
    // loopback relay: keep processing the actual SOCKS5 handshake and datagrams.
    REQUIRE(eventually([&] {
        return bridge.udpDomainNames.contains("fixture.invalid") &&
            bridge.udpReturned.value(dht.socket.localPort()) > 0;
    }, 35000));
    REQUIRE(TorrentEngineTestAccess::settings(engine).get_str(lt::settings_pack::dht_bootstrap_nodes) == expected);
    REQUIRE(bridge.udpDomainNames.contains("fixture.invalid"));
    REQUIRE(dht.packets > 0);
    REQUIRE(TorrentEngineTestAccess::call(engine, [&](auto &d) {
        d.records.at(id).handle.force_dht_announce();
        return true;
    }));
    const bool announced = eventually([&] { return dht.announces > 0; }, 15000);
    INFO("DHT queries=" << dht.queries << " invalid=" << dht.invalid
         << " returned=" << bridge.udpReturned.value(dht.socket.localPort())
         << " forwarded=" << bridge.udpForwarded.value(dht.socket.localPort())
         << " active=" << bridge.udpActive << " rejected=" << bridge.udpRejected);
    INFO("last local DHT packet=" << dht.lastDhtPacket.toHex().toStdString());
    REQUIRE(announced);
    REQUIRE(dht.onlyFrom(bridge));
    REQUIRE(engine.configurationGeneration() == generation);
    REQUIRE(engine.settings().bootstrapNodes == settings.bootstrapNodes);
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        return reinterpret_cast<quintptr>(d.session.get());
    }) == initialSession);
    engine.shutdown();
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("Unsupported SOCKS5 UDP stays closed and a configuration reload can restore it", "[torrent][proxy][bootstrap-unsupported]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    LocalUdpService dht(LocalUdpService::Dht);
    REQUIRE(dht.bind());
    bridge.udpAllowedPorts.insert(dht.socket.localPort());
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = QString("fixture.invalid:%1").arg(dht.socket.localPort());
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(settings, udpProxy(bridge));
    const auto id = engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only, false, {}, false));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty() && bridge.udpRejected >= 2; }));
    REQUIRE(dht.packets == 0);
    REQUIRE(bridge.udpActive == 0);

    // This is explicit reconfiguration, not a claim of automatic recovery from
    // a server changing its advertised UDP capability after a terminal reply.
    bridge.udpEnabled = true;
    quint64 applied = 0;
    QObject::connect(&engine, &TorrentEngine::configurationApplied, &engine,
                     [&](quint64 epoch) { applied = epoch; });
    const auto epoch = engine.configure(settings, udpProxy(bridge));
    REQUIRE(eventually([&] { return applied == epoch && bridge.udpActive > 0; }));
    REQUIRE(eventually([&] { return dht.announces > 0; }, 15000));
    REQUIRE(dht.onlyFrom(bridge));
    REQUIRE(engine.jobs().size() == 1);
    REQUIRE(engine.jobs().front().id == id);
    engine.shutdown();
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("SOCKS5 bootstrap retries back off and cancel when routing changes", "[torrent][proxy][bootstrap-retry-lifecycle]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = "fixture.invalid:6252";
    TorrentEngine engine(dir.filePath("state"));
    quint64 applied = 0;
    QObject::connect(&engine, &TorrentEngine::configurationApplied, &engine, [&](quint64 epoch) { applied = epoch; });
    engine.configure(settings, udpProxy(bridge));
    const auto id = engine.add(fixture(dir.filePath("source")));
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) { return d.bootstrapRetryDelay.count(); }) == 30);
    for (const auto expected : {60, 120, 240, 300, 300}) {
        const int rejected = bridge.udpRejected;
        REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
            d.nextBootstrapRetry = Clock::now();
            return !d.pendingBootstrap.isEmpty();
        }));
        REQUIRE(eventually([&] { return bridge.udpRejected > rejected; }));
        REQUIRE(TorrentEngineTestAccess::call(engine, [expected](auto &d) {
            return d.bootstrapRetryDelay.count() == expected &&
                d.nextBootstrapRetry > Clock::now() + std::chrono::seconds(expected - 2);
        }));
    }
    // A silent relay keeps the retry in flight until the new generation cancels it.
    bridge.udpEnabled = true;
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        d.nextBootstrapRetry = Clock::now();
        return true;
    }));
    REQUIRE(eventually([&] { return bridge.udpDropped > 0; }));
    settings.proxyMode = ProxyMode::Direct;
    settings.dht = false;
    settings.bootstrapNodes.clear();
    QElapsedTimer cancellation;
    cancellation.start();
    engine.configure(settings, {});
    const auto generation = engine.configurationGeneration();
    const bool acknowledged = eventually([&] { return applied == generation; }, 750);
    INFO("retry cancellation ms=" << cancellation.elapsed());
    INFO("applied=" << applied << " requested=" << generation << " UDP active=" << bridge.udpActive
         << " dropped=" << bridge.udpDropped << " rejected=" << bridge.udpRejected);
    REQUIRE(acknowledged);
    REQUIRE(cancellation.elapsed() < 750);
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        return d.pendingBootstrap.isEmpty() && d.nextBootstrapRetry == Clock::time_point::max();
    }));
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("SOCKS5 DHT bootstrap preserves numeric nodes without a connection", "[torrent][proxy][bootstrap-helper]") {
    application();
    LocalSocksProxy bridge;
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    REQUIRE(proxyBootstrapNodes(udpProxy(bridge), {"127.0.0.1:6252", "[::1]:6253"}) ==
        QStringList{"127.0.0.1:6252", "[::1]:6253"});
    REQUIRE_FALSE(bridge.server.hasPendingConnections());
    REQUIRE(bridge.authenticated == 0);
    auto proxy = udpProxy(bridge);
    proxy.udp = false;
    REQUIRE(proxyBootstrapNodes(proxy, {"fixture.invalid:6252", "127.0.0.1:6252"}) == QStringList{"127.0.0.1:6252"});
    REQUIRE(proxyBootstrapNodes(proxy, {"bad host:42", "[not-ip]:42", "127.0.0.1:0", "[::1]:65536"}).isEmpty());
    REQUIRE_FALSE(bridge.server.hasPendingConnections());
}

TEST_CASE("SOCKS5 DHT bootstrap learns a numeric endpoint from a remote hostname ping", "[torrent][proxy][bootstrap-helper]") {
    application();
    LocalSocksProxy bridge;
    LocalUdpService dht(LocalUdpService::Dht);
    REQUIRE(dht.bind());
    bridge.udpEnabled = true;
    bridge.udpAllowedPorts.insert(dht.socket.localPort());
    QString expectedHost = "127.0.0.1";
    bool differentGlobalProxy = false, namedProxy = false;
    SECTION("authenticated IPv4 relay and response") {}
    SECTION("no-auth SOCKS5 relay") { bridge.requireAuthentication = false; }
    SECTION("IPv6 relay with IPv4 destination") { bridge.udpRelayV6 = true; }
    SECTION("numeric IPv6 source in the reply envelope") {
        expectedHost = "[::1]";
        bridge.udpReplyTransform = [](QByteArray packet) {
            return QByteArray::fromHex("00000004") + QByteArray(15, '\0') + char(1) + packet.mid(8);
        };
    }
    SECTION("KRPC error also identifies a numeric bootstrap endpoint") { dht.rejectDhtPing = true; }
    SECTION("chosen proxy overrides a rejecting Qt application proxy") { differentGlobalProxy = true; }
    SECTION("explicit proxy hostname resolves asynchronously") { namedProxy = true; }
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto proxy = udpProxy(bridge);
    if (!bridge.requireAuthentication) { proxy.user.clear(); proxy.password.clear(); }
    if (namedProxy) proxy.host = "localhost";
    QTcpServer unrelated;
    struct RestoreProxy {
        QNetworkProxy original = QNetworkProxy::applicationProxy();
        ~RestoreProxy() { QNetworkProxy::setApplicationProxy(original); }
    } restoreProxy;
    if (differentGlobalProxy) {
        unrelated.setProxy(QNetworkProxy::NoProxy);
        REQUIRE(unrelated.listen(QHostAddress::LocalHost));
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1", unrelated.serverPort()));
    }
    const auto node = QString("fixture.invalid:%1").arg(dht.socket.localPort());
    auto result = std::async(std::launch::async, [=] { return proxyBootstrapNodes(proxy, {node}); });
    REQUIRE(eventually([&] { return result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }, 6000));
    REQUIRE(result.get() == QStringList{QString("%1:%2").arg(expectedHost).arg(dht.socket.localPort())});
    REQUIRE(dht.queries == 1);
    REQUIRE(dht.onlyFrom(bridge));
    REQUIRE(bridge.authenticated == (bridge.requireAuthentication ? 1 : 0));
    REQUIRE(bridge.udpDomainForwarded == 1);
    REQUIRE_FALSE(unrelated.hasPendingConnections());
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("SOCKS5 bootstrap rejects untrusted replies and denied UDP without fallback", "[torrent][proxy][bootstrap-helper]") {
    application();
    LocalSocksProxy bridge;
    LocalUdpService dht(LocalUdpService::Dht);
    REQUIRE(dht.bind());
    bridge.udpEnabled = true;
    bridge.udpAllowedPorts.insert(dht.socket.localPort());
    bool noDatagram = false, wrongPassword = false;
    SECTION("denied UDP association") { bridge.udpEnabled = false; noDatagram = true; }
    SECTION("wrong password") { wrongPassword = true; noDatagram = true; }
    SECTION("malformed association reply") {
        noDatagram = true;
        bridge.associateReplyTransform = [](QByteArray p) { p[2] = 1; return p; };
    }
    SECTION("relay hostname reply must not cause DNS resolution") {
        noDatagram = true;
        bridge.associateReplyTransform = [](QByteArray p) {
            return QByteArray::fromHex("050000030f") + "fixture.invalid" + p.right(2);
        };
    }
    SECTION("wrong relay source port") { bridge.udpWrongReplyPort = true; }
    SECTION("wrong remote source port") { bridge.udpReplyTransform = [](QByteArray p) { p[9] = char(p[9] ^ 1); return p; }; }
    SECTION("fragmented response") { bridge.udpReplyTransform = [](QByteArray p) { p[2] = 1; return p; }; }
    SECTION("unknown source address family") { bridge.udpReplyTransform = [](QByteArray p) { p[3] = 9; return p; }; }
    SECTION("wrong transaction") {
        bridge.udpReplyTransform = [](QByteArray p) {
            const auto token = p.indexOf("1:t16:");
            if (token >= 0) p[token + 6] = char(p[token + 6] ^ 1);
            return p;
        };
    }
    SECTION("not a KRPC response") { bridge.udpReplyTransform = [](QByteArray p) { p[p.size() - 2] = 'q'; return p; }; }
    SECTION("truncated KRPC response") { bridge.udpReplyTransform = [](QByteArray p) { p.chop(1); return p; }; }
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    auto proxy = udpProxy(bridge);
    if (wrongPassword) proxy.password = "wrong-password";
    const auto node = QString("fixture.invalid:%1").arg(dht.socket.localPort());
    QElapsedTimer elapsed;
    elapsed.start();
    auto result = std::async(std::launch::async, [=] { return proxyBootstrapNodes(proxy, {node}, {}, 250); });
    const bool finished = eventually([&] { return result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }, 1500);
    INFO("bootstrap elapsed ms=" << elapsed.elapsed() << " authenticated=" << bridge.authenticated
         << " associated=" << bridge.udpAssociated << " forwarded=" << bridge.udpDomainForwarded
         << " returned=" << bridge.udpReturned.value(dht.socket.localPort()) << " packets=" << dht.packets);
    REQUIRE(finished);
    REQUIRE(result.get().isEmpty());
    if (noDatagram) REQUIRE(dht.packets == 0);
    else { REQUIRE(dht.queries == 1); REQUIRE(dht.onlyFrom(bridge)); }
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("SOCKS5 bootstrap uses one deadline across nodes and promptly cancels", "[torrent][proxy][bootstrap-helper]") {
    application();
    SECTION("silent nodes share one deadline") {
        LocalSocksProxy bridge;
        bridge.udpEnabled = true;
        QUdpSocket first, second, third;
        QStringList nodes;
        for (auto *socket : {&first, &second, &third}) {
            REQUIRE(socket->bind(QHostAddress(QHostAddress::LocalHost), quint16(0)));
            bridge.udpAllowedPorts.insert(socket->localPort());
            nodes << QString("fixture.invalid:%1").arg(socket->localPort());
        }
        REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
        const auto proxy = udpProxy(bridge);
        QElapsedTimer elapsed;
        elapsed.start();
        auto result = std::async(std::launch::async, [=] { return proxyBootstrapNodes(proxy, nodes, {}, 250); });
        REQUIRE(eventually([&] { return result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }, 1500));
        REQUIRE(result.get().isEmpty());
        INFO("three silent nodes elapsed ms=" << elapsed.elapsed());
        REQUIRE(elapsed.elapsed() < 700);
        REQUIRE(bridge.udpDomainForwarded == 3);
        REQUIRE(first.hasPendingDatagrams());
        REQUIRE(second.hasPendingDatagrams());
        REQUIRE(third.hasPendingDatagrams());
    }
    SECTION("cancel a stalled SOCKS5 handshake") {
        QTcpServer stalled;
        REQUIRE(stalled.listen(QHostAddress::LocalHost));
        ProxyConfig proxy;
        proxy.type = ProxyType::Socks5;
        proxy.host = "127.0.0.1";
        proxy.port = stalled.serverPort();
        proxy.udp = true;
        std::atomic_bool cancel{false};
        auto result = std::async(std::launch::async, [&] {
            return proxyBootstrapNodes(proxy, {"fixture.invalid:6252"}, [&] { return cancel.load(); });
        });
        REQUIRE(eventually([&] { return stalled.hasPendingConnections(); }));
        QElapsedTimer elapsed;
        elapsed.start();
        cancel = true;
        REQUIRE(eventually([&] { return result.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }, 1000));
        REQUIRE(result.get().isEmpty());
        INFO("cancellation elapsed ms=" << elapsed.elapsed());
        REQUIRE(elapsed.elapsed() < 250);
    }
}

TEST_CASE("New engine configuration promptly cancels an older SOCKS5 bootstrap", "[torrent][proxy][bootstrap-generation]") {
    application();
    QTemporaryDir dir;
    QTcpServer stalled;
    REQUIRE(stalled.listen(QHostAddress::LocalHost));
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.dht = true;
    settings.bootstrapNodes = "fixture.invalid:6252";
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = stalled.serverPort();
    proxy.udp = true;
    TorrentEngine engine(dir.filePath("state"));
    quint64 applied = 0;
    QObject::connect(&engine, &TorrentEngine::configurationApplied, &engine, [&](quint64 epoch) { applied = epoch; });
    engine.configure(settings, proxy);
    engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only, false, {}, false));
    REQUIRE(eventually([&] { return stalled.hasPendingConnections(); }, 2000));
    settings.enabled = false;
    QElapsedTimer elapsed;
    elapsed.start();
    const auto epoch = engine.configure(settings, proxy);
    const auto cancelled = eventually([&] { return applied == epoch; }, 750);
    INFO("new configuration acknowledgement ms=" << elapsed.elapsed());
    REQUIRE(cancelled);
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
}

TEST_CASE("Authenticated SOCKS5 UDP transfers a complete uTP-only peer payload", "[torrent][proxy][swarm][socks-utp]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    bridge.udpEnabled = true;
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    auto seedConfig = isolated(source);
    seedConfig.tcp = false;
    seedConfig.utp = true;
    auto seedSettings = sessionSettings(seedConfig, {});
    seedSettings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    lt::session seed{lt::session_params(seedSettings)};
    auto params = lt::load_torrent_file(metadata.toStdString());
    params.save_path = source.toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    const auto seeder = seed.add_torrent(params);
    REQUIRE(eventually([&] { return seeder.status().is_seeding; }));
    const auto peerPort = seed.listen_port();
    REQUIRE(peerPort > 0);
    bridge.udpAllowedPorts.insert(peerPort);
    auto settings = isolated(dir.filePath("download"));
    settings.proxyMode = ProxyMode::RequireProxy;
    settings.tcp = false;
    settings.utp = true;
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(settings, udpProxy(bridge));
    const auto id = engine.add(metadata);
    REQUIRE(eventually([&] { return !engine.files(id).isEmpty() && !jobSnapshot(engine, id).paused; }));
    REQUIRE(eventually([&] { return bridge.udpAssociated > 0; }));
    REQUIRE(TorrentEngineTestAccess::call(engine, [&](auto &d) {
        d.records.at(id).handle.connect_peer({lt::make_address("127.0.0.1"), peerPort}, {}, lt::pex_utp);
        return true;
    }));
    const auto finished = eventually([&] { return jobSnapshot(engine, id).complete; }, 20000);
    const auto job = jobSnapshot(engine, id);
    INFO("uTP state=" << job.state.toStdString() << " bytes=" << job.downloaded
         << " peer UDP port=" << peerPort << " sent=" << bridge.udpForwarded.value(peerPort)
         << " returned=" << bridge.udpReturned.value(peerPort) << " TCP forwarded=" << bridge.forwarded);
    REQUIRE(finished);
    REQUIRE(job.downloaded >= 131072);
    REQUIRE(hashFile(dir.filePath("download/payload.bin")) == hashFile(dir.filePath("source/payload.bin")));
    REQUIRE(bridge.authenticated > 0);
    REQUIRE(bridge.udpForwarded.value(peerPort) > 0);
    REQUIRE(bridge.udpReturned.value(peerPort) > 0);
    REQUIRE(bridge.forwarded == 0);
    engine.shutdown();
    REQUIRE(eventually([&] { return bridge.udpActive == 0; }));
}

TEST_CASE("Recheck retracts completion on corruption and deletion preserves unrelated files", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto file = fixture(source);
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(source), {});
    const auto id = engine.add(file);
    REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete; }));
    engine.pause(id, true);
    REQUIRE(eventually([&] { return engine.jobs().first().paused; }));
    QFile payload(dir.filePath("source/payload.bin"));
    REQUIRE(payload.open(QIODevice::WriteOnly));
    REQUIRE(payload.write(QByteArray(131072, 'z')) == 131072);
    payload.close();
    engine.recheck(id);
    REQUIRE(eventually([&] { return !engine.jobs().first().complete; }));
    engine.pause(id, false);
    REQUIRE(eventually([&] { return engine.jobs().first().state == "Downloading" && engine.jobs().first().downloaded == 0; }));
    QFile unrelated(dir.filePath("source/unrelated.txt"));
    REQUIRE(unrelated.open(QIODevice::WriteOnly));
    unrelated.write("keep");
    unrelated.close();
    engine.remove(id, true);
    REQUIRE(eventually([&] { return engine.jobs().isEmpty() && !QFileInfo::exists(payload.fileName()); }));
    REQUIRE(QFileInfo::exists(unrelated.fileName()));
    REQUIRE(QFileInfo::exists(file));
}

TEST_CASE("SOCKS5 uploads verified payload over an outgoing proxied peer connection", "[torrent][proxy][proxy-upload]") {
    application();
    QTemporaryDir dir;
    LocalSocksProxy bridge;
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    auto settings = isolated(source);
    settings.proxyMode = ProxyMode::RequireProxy;
    // Keep payload flowing across several one-second peer snapshot intervals.
    settings.uploadLimitKiB = 8;
    ProxyConfig proxy;
    proxy.type = ProxyType::Socks5;
    proxy.host = "127.0.0.1";
    proxy.port = bridge.server.serverPort();
    proxy.user = "fixture-user";
    proxy.password = "fixture-password";
    TorrentEngine seed(dir.filePath("seed-state"));
    seed.configure(settings, proxy);
    const auto id = seed.add(metadata);
    REQUIRE(eventually([&] { return jobSnapshot(seed, id).complete; }));
    seed.selectObservedJob(id);
    REQUIRE(seed.effectiveListeners().isEmpty());
    const auto actual = TorrentEngineTestAccess::settings(seed);
    REQUIRE(actual.get_bool(lt::settings_pack::proxy_peer_connections));
    REQUIRE_FALSE(actual.get_bool(lt::settings_pack::enable_incoming_tcp));
    // Local peers bypass the global limit; pace this fixture across snapshots.
    REQUIRE(TorrentEngineTestAccess::call(seed, [](auto &d) {
        auto local = d.session->get_peer_class(lt::session::local_peer_class_id);
        local.upload_limit = 8 * 1024;
        d.session->set_peer_class(lt::session::local_peer_class_id, local);
        return true;
    }));
    auto direct = sessionSettings(isolated(dir.filePath("leech")), {});
    direct.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    lt::session leech{lt::session_params(direct)};
    auto params = lt::load_torrent_file(metadata.toStdString());
    params.save_path = dir.filePath("leech").toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    const auto handle = leech.add_torrent(params);
    REQUIRE(eventually([&] { return handle.status().state == lt::torrent_status::downloading; }));
    REQUIRE(TorrentEngineTestAccess::connect(seed, id, leech.listen_port()));
    bool observedUpload = false, observedUploadingCount = false;
    QStringList peerEvidence;
    const bool transferred = eventually([&] {
        const auto snapshot = jobSnapshot(seed, id);
        if (snapshot.uploadingPeers == 1 && snapshot.downloadingPeers == 0)
            observedUploadingCount = true;
        for (const auto &peer : seed.peers(id)) {
            const auto evidence = QString("%1:%2 %3 up=%4").arg(peer.ip).arg(peer.port).arg(peer.transport).arg(peer.uploadRate);
            if (!peerEvidence.contains(evidence)) peerEvidence << evidence;
            if (peer.ip == "127.0.0.1" && peer.port == leech.listen_port() &&
                peer.transport == "TCP" && peer.uploadRate > 0)
                observedUpload = true;
        }
        return handle.status().is_seeding && snapshot.uploaded >= 131072
            && observedUpload && observedUploadingCount;
    }, 35000);
    INFO("peers=" << peerEvidence.join(';').toStdString() << " selected=" << seed.observedJob().toStdString()
         << " uploaded=" << jobSnapshot(seed, id).uploaded << " complete=" << handle.status().is_seeding
         << " leech-port=" << leech.listen_port());
    REQUIRE(transferred);
    REQUIRE(observedUpload);
    REQUIRE(observedUploadingCount);
    REQUIRE(bridge.authenticated > 0);
    REQUIRE(bridge.forwarded > 0);
    REQUIRE(bridge.tcpClientBytes >= 131072);
    REQUIRE(bridge.tcpTargetBytes > 0);
    REQUIRE(hashFile(dir.filePath("leech/payload.bin")) == hashFile(dir.filePath("source/payload.bin")));
    REQUIRE(seed.effectiveListeners().isEmpty());
    seed.selectObservedJob({});
    REQUIRE(seed.peers(id).isEmpty());
}

TEST_CASE("Country and unidentified client policies reject payload before exchange", "[torrent][security][security-peer]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    auto settings = isolated(source);
    bool unidentified = false, outgoing = false, blocked = true, proxied = false;
    LocalSocksProxy bridge;
    REQUIRE(bridge.server.listen(QHostAddress::LocalHost));
    SECTION("Country") { settings.blockedCountries = {"UA"}; }
    SECTION("Unidentified client") { settings.blockUnknownClients = true; unidentified = true; }
    SECTION("Country outgoing") { settings.blockedCountries = {"UA"}; outgoing = true; }
    SECTION("Unidentified outgoing") { settings.blockUnknownClients = true; unidentified = outgoing = true; }
    SECTION("Country over SOCKS") { settings.blockedCountries = {"UA"}; outgoing = proxied = true; }
    SECTION("Recognized client remains allowed") { settings.blockUnknownClients = true; blocked = false; }
    SECTION("Other countries remain allowed") { settings.blockedCountries = {"US"}; blocked = false; }
    TorrentEngine seed(dir.filePath("seed-state"), nullptr, [](const QString &) { return QStringLiteral("UA"); });
    auto proxy = proxied ? udpProxy(bridge) : ProxyConfig{};
    proxy.udp = false;
    if (proxied) settings.proxyMode = ProxyMode::RequireProxy;
    seed.configure(settings, proxy);
    const auto id = seed.add(metadata);
    REQUIRE(eventually([&] { return jobSnapshot(seed, id).complete; }));
    auto leechSettings = sessionSettings(isolated(dir.filePath("leech")), {});
    leechSettings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    if (unidentified) {
        leechSettings.set_str(lt::settings_pack::peer_fingerprint, "xxxxxxxxxxxxxxxxxxxx");
        leechSettings.set_bool(lt::settings_pack::anonymous_mode, true);
    }
    lt::session leech{lt::session_params(leechSettings)};
    auto params = lt::load_torrent_file(metadata.toStdString());
    params.save_path = dir.filePath("leech").toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    const auto handle = leech.add_torrent(params);
    REQUIRE(eventually([&] { return handle.status().state == lt::torrent_status::downloading; }));
    if (outgoing) REQUIRE(TorrentEngineTestAccess::connect(seed, id, leech.listen_port()));
    else handle.connect_peer({lt::make_address("127.0.0.1"), static_cast<unsigned short>(TorrentEngineTestAccess::port(seed))});
    if (blocked) {
        REQUIRE_FALSE(eventually([&] { return handle.status().total_payload_download > 0; }, 3000));
        REQUIRE(jobSnapshot(seed, id).uploaded == 0);
        REQUIRE(TorrentEngineTestAccess::call(seed, [=](auto &d) {
            return (unidentified ? d.peerPolicy->clientRejects.load() : d.peerPolicy->countryRejects.load()) > 0;
        }));
    } else {
        REQUIRE(eventually([&] { return handle.status().is_seeding; }));
        // Libtorrent can report completion before its disk worker flushes the file.
        REQUIRE(eventually([&] {
            return hashFile(dir.filePath("leech/payload.bin")) == hashFile(dir.filePath("source/payload.bin"));
        }));
    }
}

TEST_CASE("Seeding ratio stops uploads without removing data", "[torrent][swarm]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto file = fixture(source);
    auto s = isolated(source);
    s.seedRatio = 0.01;
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(s, {});
    const auto id = engine.add(file);
    REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete && !engine.jobs().first().paused; }));
    auto settings = sessionSettings(isolated(dir.filePath("leech")), {});
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    lt::session leech{lt::session_params(settings)};
    auto params = lt::load_torrent_file(file.toStdString());
    params.save_path = dir.filePath("leech").toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    auto handle = leech.add_torrent(params);
    REQUIRE(eventually([&] { return handle.status().state == lt::torrent_status::downloading; }));
    handle.connect_peer({lt::make_address("127.0.0.1"), static_cast<unsigned short>(TorrentEngineTestAccess::port(engine))});
    const bool stopped = eventually([&] { return engine.jobs().first().state == "Seeding limit reached"; }, 5000);
    const auto live = TorrentEngineTestAccess::status(engine, id);
    INFO("state=" << engine.jobs().first().state.toStdString() << " peers=" << engine.jobs().first().peers
         << " uploaded=" << engine.jobs().first().uploaded << " leech-bytes=" << handle.status().total_wanted_done
         << " live-upload=" << live.all_time_upload << " payload-upload=" << live.total_payload_upload
         << " live-state=" << live.state << " live-path=" << live.save_path
         << " subscribed=" << bool(live.flags & lt::torrent_flags::update_subscribe));
    REQUIRE(stopped);
    REQUIRE(engine.jobs().first().uploaded > 0);
    REQUIRE(engine.jobs().first().paused);
    REQUIRE(QFileInfo::exists(dir.filePath("source/payload.bin")));
}

TEST_CASE("Persisted seeding time limit stops a resumed completed job", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    auto s = isolated(source);
    QString id;
    {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(s, {});
        id = engine.add(fixture(source));
        REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete; }));
        engine.shutdown();
    }
    QFile file(dir.filePath("state/jobs.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    auto root = doc.object();
    auto jobs = root["jobs"].toArray();
    auto entry = jobs.first().toObject();
    auto bytes = QByteArray::fromBase64(entry["resume"].toString().toLatin1());
    auto params = lt::read_resume_data(lt::span<char const>(bytes.constData(), bytes.size()));
    params.finished_time = 59;
    params.seeding_time = 59;
    const auto encoded = lt::write_resume_data_buf(params);
    entry["resume"] = QString::fromLatin1(QByteArray(encoded.data(), encoded.size()).toBase64());
    jobs[0] = entry;
    root["jobs"] = jobs;
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(root).toJson());
    file.close();
    s.seedMinutes = 1;
    TorrentEngine restored(dir.filePath("state"));
    restored.configure(s, {});
    REQUIRE(eventually([&] { return !restored.jobs().isEmpty() && restored.jobs().first().state == "Seeding limit reached"; }));
    REQUIRE(restored.jobs().first().paused);
    REQUIRE(QFileInfo::exists(dir.filePath("source/payload.bin")));
}

TEST_CASE("Malformed metadata emits a change after rejecting asynchronous work", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    const auto path = dir.filePath("invalid.torrent");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("not bencoded metadata");
    file.close();
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(dir.filePath("downloads")), {});
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    QStringList errors;
    int changes = 0;
    QCoreApplication::processEvents();
    QObject::connect(&engine, &TorrentEngine::changed, &engine, [&] { ++changes; });
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString e) { errors << e; });
    engine.add(path);
    REQUIRE(eventually([&] { return !errors.isEmpty() && engine.jobs().isEmpty(); }));
    REQUIRE(changes > 0);
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
}

TEST_CASE("Torrent symlink destinations fail closed and preserve the target", "[torrent][engine]") {
    application();
    QTemporaryDir dir;
    const auto file = fixture(dir.filePath("source"));
    const auto path = dir.filePath("downloads");
    REQUIRE(QDir().mkpath(path));
    const auto sentinel = dir.filePath("sentinel");
    QFile target(sentinel);
    REQUIRE(target.open(QIODevice::WriteOnly));
    target.write("sentinel");
    target.close();
    REQUIRE(QFile::link(sentinel, QDir(path).filePath("payload.bin")));
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(path), {});
    auto id = engine.add(file);
    REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && !engine.jobs().first().error.isEmpty(); }));
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    engine.remove(id, true);
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &) { return true; }));
    REQUIRE(target.open(QIODevice::ReadOnly));
    REQUIRE(target.readAll() == "sentinel");
    REQUIRE_FALSE(engine.jobs().isEmpty());
}

TEST_CASE("Pause-on-add exposes metadata without creating a network session", "[torrent][barrier]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(dir.filePath("download")), {});
    const auto id = engine.add(fixture(dir.filePath("source")), {}, {}, true);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(eventually([&] { return engine.files(id).size() == 1; }));
    REQUIRE(engine.jobs().first().paused);
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    engine.pause(id, false);
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::active(engine); }));
}

TEST_CASE("Public invalidation revisions defeat a stale worker publication", "[torrent][barrier]") {
    application();
    QTemporaryDir dir;
    auto s = isolated(dir.filePath("source"));
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(s, {});
    const auto id = engine.add(fixture(s.downloadPath));
    REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete; }));
    const auto before = engine.jobs().first().revision;
    struct Barrier {
        std::atomic_bool entered{false}, release{false}, published{false}, finish{false};
    };
    auto barrier = std::make_shared<Barrier>();
    struct Release { std::shared_ptr<Barrier> b; ~Release() { b->release = true; b->finish = true; } } release{barrier};
    TorrentEngineTestAccess::call(engine, [&](auto &d) {
        return d.enqueue([&d, barrier] {
            barrier->entered = true;
            while (!barrier->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            d.publish();
            barrier->published = true;
            while (!barrier->finish) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    });
    REQUIRE(eventually([&] { return barrier->entered.load(); }));
    engine.recheck(id);
    REQUIRE_FALSE(engine.jobs().first().complete);
    REQUIRE(engine.jobs().first().revision == before + 1);
    REQUIRE(engine.files(id, before).isEmpty());
    barrier->release = true;
    REQUIRE(eventually([&] { return barrier->published.load(); }));
    REQUIRE_FALSE(engine.jobs().first().complete);
    REQUIRE(engine.jobs().first().revision == before + 1);
    barrier->finish = true;
    REQUIRE(eventually([&] { return engine.jobs().first().complete; }));
}

TEST_CASE("Configuration acknowledgement follows snapshot publication and completion repeats", "[torrent][barrier]") {
    application();
    QTemporaryDir dir;
    auto s = isolated(dir.filePath("source"));
    TorrentEngine engine(dir.filePath("state"));
    QList<quint64> applied;
    int completions = 0;
    QObject::connect(&engine, &TorrentEngine::configurationApplied, &engine, [&](quint64 generation) { applied << generation; });
    QObject::connect(&engine, &TorrentEngine::completed, &engine, [&](QString, QStringList, bool) { ++completions; });
    engine.configure(s, {});
    REQUIRE(engine.configurationGeneration() > 0);
    REQUIRE(eventually([&] { return applied.contains(engine.configurationGeneration()); }));
    engine.add(fixture(s.downloadPath));
    REQUIRE(eventually([&] { return completions == 1; }));
    const auto first = engine.configurationGeneration();
    engine.configure(s, {});
    REQUIRE(engine.configurationGeneration() == first + 1);
    REQUIRE(eventually([&] { return applied.contains(first + 1) && completions == 2; }));
    REQUIRE(eventually([&] { return engine.jobs().first().complete; }));
}

TEST_CASE("Storage invalidation is synchronous and precedes every public storage mutation", "[torrent][publication-guard]") {
    application();
    for (int operation = 0; operation < 4; ++operation) {
        DYNAMIC_SECTION("operation " << operation) {
            QTemporaryDir dir;
            const auto settings = isolated(dir.filePath("source"));
            TorrentEngine engine(dir.filePath("state"));
            const auto epoch = engine.configure(settings, {});
            const auto id = engine.add(fixture(settings.downloadPath));
            REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete; }));
            const auto revision = engine.jobs().first().revision;
            struct Gate { std::atomic_bool entered{false}, release{false}; };
            auto gate = std::make_shared<Gate>();
            struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release = true; } } release{gate};
            TorrentEngineTestAccess::call(engine, [&](auto &d) {
                return d.enqueue([gate] {
                    gate->entered = true;
                    while (!gate->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                });
            });
            REQUIRE(eventually([&] { return gate->entered.load(); }));
            QStringList invalidations;
            QStringList hashRechecks;
            QObject::connect(&engine, &TorrentEngine::hashRecheckRequested, &engine, [&](QString rechecked) {
                REQUIRE_FALSE(invalidations.isEmpty());
                hashRechecks << rechecked;
            }, Qt::DirectConnection);
            bool ownerThread = false, sawOriginal = false, guardWasValid = false;
            QObject::connect(&engine, &TorrentEngine::storageInvalidating, &engine, [&](QString invalidated) {
                invalidations << invalidated;
                ownerThread = QThread::currentThread() == engine.thread();
                const auto jobs = engine.jobs();
                sawOriginal = jobs.size() == 1 && jobs.first().complete && jobs.first().revision == revision &&
                    engine.configurationGeneration() == epoch;
                guardWasValid = engine.publicationValid(id, revision, epoch);
            }, Qt::DirectConnection);
            if (operation == 0) engine.configure(settings, {});
            if (operation == 1) engine.setWantedFiles(id, {});
            if (operation == 2) engine.recheck(id);
            if (operation == 3) engine.remove(id);
            REQUIRE(hashRechecks == (operation == 2 ? QStringList{id} : QStringList{}));
            // No event processing or worker progress between mutation and assertions.
            REQUIRE(invalidations == QStringList{operation == 0 ? QString{} : id});
            REQUIRE(ownerThread);
            REQUIRE(sawOriginal);
            REQUIRE(guardWasValid);
            REQUIRE_FALSE(engine.publicationValid(id, revision, epoch));
            const auto jobs = engine.jobs();
            if (operation == 3) REQUIRE(jobs.isEmpty());
            else {
                REQUIRE_FALSE(jobs.first().complete);
                REQUIRE_FALSE(engine.publicationValid(id, jobs.first().revision, engine.configurationGeneration()));
            }
            auto concurrent = std::async(std::launch::async, [&] { return engine.publicationValid(id, revision, epoch); });
            REQUIRE_FALSE(concurrent.get());
            gate->release = true;
            QObject::disconnect(&engine, &TorrentEngine::storageInvalidating, &engine, nullptr);
        }
    }
}

TEST_CASE("Publication validity is version bound and safe for worker callbacks", "[torrent][publication-guard]") {
    application();
    QTemporaryDir dir;
    const auto settings = isolated(dir.filePath("source"));
    TorrentEngine engine(dir.filePath("state"));
    const auto epoch = engine.configure(settings, {});
    const auto id = engine.add(fixture(settings.downloadPath));
    REQUIRE(eventually([&] { return !engine.jobs().isEmpty() && engine.jobs().first().complete; }));
    const auto revision = engine.jobs().first().revision;
    auto valid = std::async(std::launch::async, [&] { return engine.publicationValid(id, revision, epoch); });
    REQUIRE(valid.get());
    REQUIRE_FALSE(engine.publicationValid(id, revision + 1, epoch));
    REQUIRE_FALSE(engine.publicationValid(id, revision, epoch + 1));
    REQUIRE_FALSE(engine.publicationValid(id, revision, 0));
    REQUIRE_FALSE(engine.publicationValid("missing-job", revision, epoch));
    engine.shutdown();
    REQUIRE_FALSE(engine.publicationValid(id, revision, epoch));
}

TEST_CASE("Overlapping torrents cannot acquire or delete another job's payload", "[torrent][ownership]") {
    application();
    QTemporaryDir dir;
    auto s = isolated(dir.filePath("source-a"));
    const auto a = fixture(s.downloadPath, lt::create_torrent::v1_only, false, {}, false);
    const auto b = fixture(dir.filePath("source-b"));
    const auto payload = dir.filePath("source-a/payload.bin");
    const auto expected = hashFile(payload);
    QString idA, idB, collision;
    {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(s, {});
        idA = engine.add(a);
        REQUIRE(eventually([&] { return jobSnapshot(engine, idA).complete; }));
        idB = engine.add(b);
        REQUIRE(eventually([&] { return !jobSnapshot(engine, idB).error.isEmpty(); }));
        collision = jobSnapshot(engine, idB).error;
        REQUIRE_FALSE(jobSnapshot(engine, idB).complete);
        REQUIRE_FALSE(TorrentEngineTestAccess::call(engine, [=](auto &d) { return d.records.at(idB).handle.is_valid(); }));
        engine.setWantedFiles(idB, {0});
        engine.pause(idB, false);
        engine.recheck(idB);
        engine.remove(idB, true);
        REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &) { return true; }));
        REQUIRE(jobSnapshot(engine, idB).error == collision);
        REQUIRE(hashFile(payload) == expected);
        REQUIRE(jobSnapshot(engine, idA).complete);
        engine.shutdown();
    }
    TorrentEngine restored(dir.filePath("state"));
    restored.configure(s, {});
    REQUIRE(eventually([&] { return jobSnapshot(restored, idA).complete; }));
    REQUIRE(jobSnapshot(restored, idB).error == collision);
    REQUIRE_FALSE(jobSnapshot(restored, idB).complete);
    restored.remove(idB, true);
    REQUIRE(TorrentEngineTestAccess::call(restored, [](auto &) { return true; }));
    REQUIRE(hashFile(payload) == expected);
    restored.remove(idB, false);
    REQUIRE(TorrentEngineTestAccess::call(restored, [](auto &) { return true; }));
    REQUIRE(jobSnapshot(restored, idB).id.isEmpty());
    REQUIRE(jobSnapshot(restored, idA).complete);
}

TEST_CASE("Ownership covers unselected metadata paths on both sides", "[torrent][ownership]") {
    application();
    for (bool multiFirst : {false, true}) {
        DYNAMIC_SECTION("multi-file owner first " << multiFirst) {
            QTemporaryDir dir;
            const auto multi = fixture(dir.filePath("multi"), lt::create_torrent::v1_only, true);
            const auto single = fixture(dir.filePath("single"), lt::create_torrent::v1_only, false, {}, false, {"b.bin"});
            const auto root = dir.filePath("downloads");
            TorrentEngine engine(dir.filePath("state"));
            engine.configure(isolated(root), {});
            const auto first = multiFirst ? engine.add(multi, root, {0}, true) : engine.add(single, root + "/bundle", {}, true);
            REQUIRE(eventually([&] { return !engine.files(first).isEmpty(); }));
            const auto second = multiFirst ? engine.add(single, root + "/bundle", {}, true) : engine.add(multi, root, {0}, true);
            REQUIRE(eventually([&] { return !jobSnapshot(engine, second).error.isEmpty(); }));
            REQUIRE(jobSnapshot(engine, first).error.isEmpty());
            REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
            REQUIRE_FALSE(QFileInfo::exists(root + "/bundle/b.bin"));
        }
    }
}

TEST_CASE("Disjoint torrents may share a root and delete only their own data", "[torrent][ownership]") {
    application();
    QTemporaryDir dir;
    const auto root = dir.filePath("source-a");
    const auto a = fixture(root, lt::create_torrent::v1_only, false, {}, false, {"one.bin"});
    const auto b = fixture(dir.filePath("source-b"), lt::create_torrent::v1_only, false, {}, false, {"two.bin"});
    REQUIRE(QFile::copy(dir.filePath("source-b/two.bin"), root + "/two.bin"));
    const auto expected = hashFile(root + "/one.bin");
    TorrentEngine engine(dir.filePath("state"));
    const auto epoch = engine.configure(isolated(root), {});
    const auto idA = engine.add(a);
    REQUIRE(eventually([&] { return jobSnapshot(engine, idA).complete; }));
    const auto idB = engine.add(b);
    REQUIRE(eventually([&] { return jobSnapshot(engine, idB).complete; }));
    engine.remove(idB, true);
    REQUIRE(eventually([&] { return jobSnapshot(engine, idB).id.isEmpty() && !QFileInfo::exists(root + "/two.bin"); }));
    REQUIRE(hashFile(root + "/one.bin") == expected);
    const auto owner = jobSnapshot(engine, idA);
    REQUIRE(engine.publicationValid(idA, owner.revision, epoch));
    REQUIRE(eventually([&] { return TorrentEngineTestAccess::call(engine, [](auto &d) { return d.removals.empty(); }); }));
    const auto replacement = engine.add(b, root, {}, true);
    REQUIRE(eventually([&] { return !engine.files(replacement).isEmpty(); }));
    REQUIRE(jobSnapshot(engine, replacement).error.isEmpty());
}

TEST_CASE("Payload ownership rejects file-directory and filesystem name aliases", "[torrent][ownership]") {
    application();
    QStringList aliases{"bundle/a.bin"};
#if defined(Q_OS_DARWIN) || defined(Q_OS_WIN)
    aliases << "BUNDLE";
#endif
    for (const auto &alias : aliases) {
        DYNAMIC_SECTION(alias.toStdString()) {
            QTemporaryDir dir;
            const auto a = fixture(dir.filePath("source-a"), lt::create_torrent::v1_only, false, {}, false, {"bundle"});
            const bool nested = alias.contains('/');
            const auto b = fixture(dir.filePath("source-b"), lt::create_torrent::v1_only, false, {}, false,
                                   {nested ? QString("a.bin") : alias});
            const auto root = dir.filePath("downloads");
            TorrentEngine engine(dir.filePath("state"));
            engine.configure(isolated(root), {});
            const auto idA = engine.add(a, root, {}, true);
            REQUIRE(eventually([&] { return !engine.files(idA).isEmpty(); }));
            const auto idB = engine.add(b, nested ? root + "/bundle" : root, {}, true);
            REQUIRE(eventually([&] { return !jobSnapshot(engine, idB).error.isEmpty(); }));
            REQUIRE_FALSE(QFileInfo::exists(root + "/bundle"));
            REQUIRE(jobSnapshot(engine, idA).error.isEmpty());
        }
    }
}

TEST_CASE("Magnet metadata collision stays blocked before payload is enabled", "[torrent][ownership][swarm]") {
    application();
    QTemporaryDir dir;
    const auto root = dir.filePath("source-a");
    const auto a = fixture(root, lt::create_torrent::v1_only, false, {}, false);
    const auto b = fixture(dir.filePath("source-b"), lt::create_torrent::v2_only, false, {}, false);
    const auto expected = hashFile(root + "/payload.bin");
    auto seedSettings = sessionSettings(isolated(dir.filePath("source-b")), {});
    seedSettings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    lt::session seed{lt::session_params(seedSettings)};
    auto params = lt::load_torrent_file(b.toStdString());
    params.save_path = dir.filePath("source-b").toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    const auto handle = seed.add_torrent(params);
    REQUIRE(eventually([&] { return handle.status().is_seeding && seed.listen_port() != 0; }));
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(root), {});
    const auto idA = engine.add(a);
    REQUIRE(eventually([&] { return jobSnapshot(engine, idA).complete; }));
    const auto idB = engine.add(QString::fromStdString(lt::make_magnet_uri(params)));
    REQUIRE(eventually([&] { return jobSnapshot(engine, idB).state == "Fetching metadata" && !jobSnapshot(engine, idB).paused; }));
    REQUIRE(TorrentEngineTestAccess::connect(engine, idB, seed.listen_port()));
    const bool rejected = eventually([&] { return !jobSnapshot(engine, idB).error.isEmpty(); });
    INFO("state=" << jobSnapshot(engine, idB).state.toStdString() << " peers=" << jobSnapshot(engine, idB).peers
        << " files=" << engine.files(idB).size() << " seed-private=" << params.ti->priv());
    REQUIRE(rejected);
    const auto collision = jobSnapshot(engine, idB).error;
    engine.setWantedFiles(idB, {0});
    engine.pause(idB, false);
    engine.recheck(idB);
    engine.remove(idB, true);
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &) { return true; }));
    REQUIRE(jobSnapshot(engine, idB).error == collision);
    REQUIRE_FALSE(jobSnapshot(engine, idB).complete);
    REQUIRE(TorrentEngineTestAccess::status(engine, idB).flags & lt::torrent_flags::upload_mode);
    REQUIRE(jobSnapshot(engine, idA).complete);
    REQUIRE(hashFile(root + "/payload.bin") == expected);
}

TEST_CASE("Relocation cannot claim another job's unwritten destination", "[torrent][ownership]") {
    application();
    QTemporaryDir dir;
    const auto a = fixture(dir.filePath("source-a"), lt::create_torrent::v1_only, false, {}, false);
    const auto b = fixture(dir.filePath("source-b"));
    auto s = isolated(dir.filePath("source-a"));
    s.completedPath = dir.filePath("completed");
    const auto expected = hashFile(dir.filePath("source-a/payload.bin"));
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(s, {});
    const auto idB = engine.add(b, s.completedPath, {}, true);
    REQUIRE(eventually([&] { return !engine.files(idB).isEmpty(); }));
    const auto idA = engine.add(a);
    REQUIRE(eventually([&] { return !jobSnapshot(engine, idA).error.isEmpty(); }));
    const auto collision = jobSnapshot(engine, idA).error;
    REQUIRE_FALSE(jobSnapshot(engine, idA).complete);
    REQUIRE_FALSE(QFileInfo::exists(s.completedPath + "/payload.bin"));
    REQUIRE(hashFile(dir.filePath("source-a/payload.bin")) == expected);
    engine.recheck(idA);
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &) { return true; }));
    REQUIRE(jobSnapshot(engine, idA).error == collision);
    REQUIRE(jobSnapshot(engine, idB).error.isEmpty());
}

TEST_CASE("Changing completed directory permits a second verified relocation", "[torrent][relocation]") {
    application();
    QTemporaryDir dir;
    auto s = isolated(dir.filePath("source"));
    const auto a = fixture(s.downloadPath, lt::create_torrent::v1_only, false, {}, false);
    const auto b = fixture(dir.filePath("other-source"));
    const auto expected = hashFile(s.downloadPath + "/payload.bin");
    s.completedPath = dir.filePath("first-completed");
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(s, {});
    const auto id = engine.add(a);
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    const auto firstPath = jobSnapshot(engine, id).savePath;
    s.completedPath = dir.filePath("second-completed");
    const auto epoch = engine.configure(s, {});
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete && jobSnapshot(engine, id).savePath != firstPath; }));
    const auto job = jobSnapshot(engine, id);
    REQUIRE(QFileInfo(job.savePath).canonicalFilePath() == QFileInfo(s.completedPath).canonicalFilePath());
    REQUIRE(hashFile(job.savePath + "/payload.bin") == expected);
    REQUIRE_FALSE(QFileInfo::exists(firstPath + "/payload.bin"));
    REQUIRE(engine.publicationValid(id, job.revision, epoch));
    const auto newOwner = engine.add(b, firstPath, {}, true);
    REQUIRE(eventually([&] { return !engine.files(newOwner).isEmpty(); }));
    REQUIRE(jobSnapshot(engine, newOwner).error.isEmpty());
}

TEST_CASE("Created seed never relocates its original files", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source, lt::create_torrent::v1_only, false, {}, false);
    const auto payload = source + "/payload.bin";
    const auto original = hashFile(payload);
    const auto modified = QFileInfo(payload).lastModified();
    auto settings = isolated(source);
    settings.completedPath = dir.filePath("completed");
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(settings, {});
    const auto id = engine.seedCreated(metadata, source);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    REQUIRE(hashFile(payload) == original);
    REQUIRE(QFileInfo(payload).lastModified() == modified);
    REQUIRE(jobSnapshot(engine, id).savePath == QFileInfo(source).canonicalFilePath());
    REQUIRE_FALSE(QFileInfo::exists(settings.completedPath + "/payload.bin"));
}

TEST_CASE("Created seed stays upload-only through every restart and control path", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    auto settings = isolated(source);
    const auto state = dir.filePath("state");
    QString id;
    auto uploadOnly = [&](TorrentEngine &engine) {
        const auto flags = TorrentEngineTestAccess::status(engine, id).flags;
        REQUIRE(bool(flags & lt::torrent_flags::upload_mode));
        REQUIRE_FALSE(bool(flags & lt::torrent_flags::auto_managed));
        REQUIRE_FALSE(bool(flags & lt::torrent_flags::seed_mode));
        REQUIRE_FALSE(bool(flags & lt::torrent_flags::no_verify_files));
    };
    {
        TorrentEngine engine(state);
        engine.configure(settings, {});
        id = engine.seedCreated(metadata, source);
        REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
        uploadOnly(engine);
        engine.configure(settings, {});
        REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
        uploadOnly(engine);
        engine.pause(id, true);
        engine.pause(id, false);
        uploadOnly(engine);
        engine.setWantedFiles(id, {});
        engine.setWantedFiles(id, {0});
        uploadOnly(engine);
        engine.recheck(id);
        REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
        uploadOnly(engine);
        engine.shutdown();
    }
    QFile file(state + "/jobs.json");
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    REQUIRE(document["version"].toInt() == 2);
    REQUIRE(document["jobs"].toArray().first().toObject()["sourceReadOnly"].toBool());
    file.close();
    TorrentEngine restored(state);
    restored.configure(settings, {});
    REQUIRE(eventually([&] { return jobSnapshot(restored, id).complete; }));
    uploadOnly(restored);
    restored.recheck(id);
    REQUIRE(eventually([&] { return jobSnapshot(restored, id).complete; }));
    uploadOnly(restored);
}

TEST_CASE("Deleting a created seed removes only its job and reports retained originals", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    const auto original = hashFile(source + "/payload.bin");
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(source), {});
    const auto id = engine.seedCreated(metadata, source);
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString error) { errors << error; });
    engine.remove(id, true);
    REQUIRE(eventually([&] {
        return engine.jobs().isEmpty() && !TorrentEngineTestAccess::active(engine) && !errors.isEmpty();
    }));
    REQUIRE(hashFile(source + "/payload.bin") == original);
    REQUIRE_FALSE(errors.isEmpty());
    REQUIRE(QFileInfo::exists(metadata));
}

TEST_CASE("Created seed rejects file selection changes without truncating grown originals", "[torrent][created-seed][source-selection]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(source), {});
    const auto id = engine.seedCreated(metadata, source);
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    QFile payload(source + "/payload.bin");
    REQUIRE(payload.open(QIODevice::WriteOnly | QIODevice::Append));
    REQUIRE(payload.write("keep appended data") == 18);
    payload.close();
    const auto original = hashFile(payload.fileName());
    const auto size = QFileInfo(payload).size();
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString error) { errors << error; });
    engine.setWantedFiles(id, {});
    engine.setWantedFiles(id, {0});
    const bool rejected = eventually([&] { return errors.size() == 2; });
    CHECK(hashFile(payload.fileName()) == original);
    CHECK(QFileInfo(payload).size() == size);
    REQUIRE(rejected);
    REQUIRE(TorrentEngineTestAccess::call(engine, [id](auto &d) {
        const auto &record = d.records.at(id);
        return !record.wanted && !record.selectionPending &&
            record.handle.get_file_priorities().front() == lt::default_priority;
    }));
}

TEST_CASE("Invalid source protection in saved jobs is rejected before starting", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    const auto settings = isolated(source);
    const auto state = dir.filePath("state");
    {
        TorrentEngine engine(state);
        engine.configure(settings, {});
        const auto id = engine.seedCreated(metadata, source);
        REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    }
    QFile file(state + "/jobs.json");
    REQUIRE(file.open(QIODevice::ReadOnly));
    auto document = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    auto item = document["jobs"].toArray().first().toObject();
    SECTION("non-boolean legacy field") { item["sourceReadOnly"] = "true"; document["version"] = 1; }
    SECTION("non-boolean current field") { item["sourceReadOnly"] = 1; document["version"] = 2; }
    SECTION("missing current field") { item.remove("sourceReadOnly"); document["version"] = 2; }
    SECTION("null current field") { item["sourceReadOnly"] = QJsonValue(); document["version"] = 2; }
    document["jobs"] = QJsonArray{item};
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(document).toJson());
    file.close();
    TorrentEngine restored(state);
    restored.configure(settings, {});
    REQUIRE_FALSE(TorrentEngineTestAccess::active(restored));
    REQUIRE(restored.jobs().isEmpty());
}

TEST_CASE("Created seeds rehash changed originals and never repair them from peers", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source, lt::create_flags_t{}, false, {}, false);
    const auto settings = isolated(source);
    const auto state = dir.filePath("state");
    QString id;
    {
        TorrentEngine engine(state);
        engine.configure(settings, {});
        id = engine.seedCreated(metadata, source);
        REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    }
    QFile payload(source + "/payload.bin");
    const auto oldTime = QFileInfo(payload).lastModified();
    REQUIRE(payload.open(QIODevice::ReadWrite));
    REQUIRE(payload.write("changed") == 7);
    REQUIRE(payload.flush());
    REQUIRE(payload.setFileTime(oldTime, QFileDevice::FileModificationTime));
    payload.close();
    REQUIRE(QFileInfo(payload).lastModified() == oldTime);
    const auto changed = hashFile(payload.fileName());
    const auto entries = QDir(source).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    TorrentEngine engine(state);
    engine.configure(settings, {});
    REQUIRE(eventually([&] {
        if (engine.files(id).isEmpty()) return false;
        const auto status = TorrentEngineTestAccess::status(engine, id);
        return status.has_metadata && status.state != lt::torrent_status::checking_files &&
            status.state != lt::torrent_status::checking_resume_data;
    }));
    REQUIRE_FALSE(jobSnapshot(engine, id).complete);
    REQUIRE(TorrentEngineTestAccess::status(engine, id).total_wanted_done < 131072);
    engine.recheck(id);
    REQUIRE(eventually([&] {
        return TorrentEngineTestAccess::call(engine, [id](auto &d) { return !d.records.at(id).checking; });
    }));
    const auto peerSource = dir.filePath("peer");
    const auto peerMetadata = fixture(peerSource, lt::create_flags_t{}, false, {}, false);
    auto peerSettings = sessionSettings(isolated(peerSource), {});
    peerSettings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    peerSettings.set_int(lt::settings_pack::alert_mask, static_cast<std::uint32_t>(lt::alert_category::connect));
    lt::session peer{lt::session_params(peerSettings)};
    auto params = lt::load_torrent_file(peerMetadata.toStdString());
    params.save_path = peerSource.toStdString();
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    auto handle = peer.add_torrent(params);
    REQUIRE(eventually([&] { return handle.status().is_seeding && peer.listen_port() != 0; }));
    REQUIRE(TorrentEngineTestAccess::connect(engine, id, peer.listen_port()));
    bool connected = false;
    REQUIRE(eventually([&] {
        std::vector<lt::alert *> alerts;
        peer.pop_alerts(&alerts);
        for (const auto *alert : alerts) if (lt::alert_cast<lt::peer_connect_alert>(alert)) connected = true;
        return connected;
    }));
    REQUIRE_FALSE(eventually([&] { return TorrentEngineTestAccess::status(engine, id).total_payload_download != 0; }, 500));
    REQUIRE(hashFile(payload.fileName()) == changed);
    REQUIRE(bool(TorrentEngineTestAccess::status(engine, id).flags & lt::torrent_flags::upload_mode));
    REQUIRE(QDir(source).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == entries);
}

TEST_CASE("Created seed rejects sources that would need payload files created", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    TorrentEngine engine(dir.filePath("state"));
    engine.configure(isolated(source), {});
    REQUIRE(engine.seedCreated("magnet:?xt=urn:btih:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", source).isEmpty());
    REQUIRE(engine.seedCreated(metadata, {}).isEmpty());
    REQUIRE(engine.seedCreated(metadata, dir.filePath("missing")).isEmpty());
    REQUIRE_FALSE(QFileInfo::exists(dir.filePath("missing")));
    REQUIRE(QFile::remove(source + "/payload.bin"));
    const auto id = engine.seedCreated(metadata, source);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(eventually([&] { return !jobSnapshot(engine, id).error.isEmpty(); }));
    REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
    REQUIRE_FALSE(QFileInfo::exists(source + "/payload.bin"));
    engine.remove(id, true);
    REQUIRE(eventually([&] { return engine.jobs().isEmpty(); }));
}

TEST_CASE("Legacy normal downloads retain their writable behavior after state migration", "[torrent][created-seed]") {
    application();
    QTemporaryDir dir;
    const auto source = dir.filePath("source");
    const auto metadata = fixture(source);
    const auto settings = isolated(source);
    const auto state = dir.filePath("state");
    QString id;
    {
        TorrentEngine engine(state);
        engine.configure(settings, {});
        id = engine.add(metadata, source);
        REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    }
    QFile file(state + "/jobs.json");
    REQUIRE(file.open(QIODevice::ReadOnly));
    auto document = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    auto item = document["jobs"].toArray().first().toObject();
    REQUIRE(item["sourceReadOnly"].isBool());
    REQUIRE_FALSE(item["sourceReadOnly"].toBool());
    item.remove("sourceReadOnly");
    document["version"] = 1;
    document["jobs"] = QJsonArray{item};
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(document).toJson());
    file.close();
    TorrentEngine restored(state);
    restored.configure(settings, {});
    REQUIRE(eventually([&] { return jobSnapshot(restored, id).complete; }));
    REQUIRE_FALSE(bool(TorrentEngineTestAccess::status(restored, id).flags & lt::torrent_flags::upload_mode));
}

TEST_CASE("Last-confirmed listener history is not a live inventory after interface removal", "[torrent][listeners][listener-lifecycle]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString error) { errors << error; });
    auto settings = isolated(dir.filePath("downloads"));
    settings.bindAddress6 = "::1";
    settings.utp = true;
    engine.configure(settings, {});
    engine.add(fixture(dir.filePath("source")));
    const bool ready = eventually([&] { return engine.effectiveListeners().size() == 4; });
    INFO("listeners=" << engine.effectiveListeners().join(';').toStdString()
         << " errors=" << errors.join(';').toStdString());
    REQUIRE(ready);
    const auto confirmed = engine.effectiveListeners();
    const auto port = TorrentEngineTestAccess::port(engine);
    QString interfaces = "127.0.0.1:" + QString::number(port);
    SECTION("one interface removed; other sockets remain unchanged") {}
    SECTION("all interfaces removed") { interfaces.clear(); }
    REQUIRE(TorrentEngineTestAccess::call(engine, [interfaces](auto &d) {
        lt::settings_pack pack;
        pack.set_str(lt::settings_pack::listen_interfaces, interfaces.toStdString());
        // Exercises the same reopen/partition/close path as on_ip_change, with
        // no machine-wide network edits and no TorrentEngine::configure call.
        d.session->apply_settings(pack);
        return d.session->get_settings().get_str(lt::settings_pack::listen_interfaces) == interfaces.toStdString();
    }));
    QTcpSocket removed;
    removed.connectToHost(QHostAddress::LocalHostIPv6, port);
    REQUIRE_FALSE(removed.waitForConnected(500));
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) { return d.session->is_listening(); }) == !interfaces.isEmpty());
    // Approved fallback: the UI must call this "last confirmed", never current
    // listeners. Public libtorrent 2.1 APIs do not enumerate live endpoints.
    REQUIRE(engine.effectiveListeners() == confirmed);
    REQUIRE(engine.settings().bindAddress6 == "::1");
    engine.shutdown();
    REQUIRE(engine.effectiveListeners().isEmpty());
}

TEST_CASE("Recoverable accept failure does not revoke a live listener", "[torrent][listeners][listener-lifecycle]") {
    application();
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    auto settings = isolated(dir.filePath("downloads"));
    engine.configure(settings, {});
    engine.add(fixture(dir.filePath("source")));
    REQUIRE(eventually([&] { return engine.effectiveListeners().size() == 2; }));
    const auto expected = engine.effectiveListeners();
    const auto port = TorrentEngineTestAccess::port(engine);
    QStringList errors;
    QObject::connect(&engine, &TorrentEngine::error, &engine, [&](QString error) { errors << error; });
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &d) {
        // Exercise the production alert handler without exhausting process
        // descriptors. The actual loopback listener remains open.
        d.listenerFailed(lt::operation_t::sock_accept);
        return true;
    }));
    REQUIRE(eventually([&] { return !errors.isEmpty(); }));
    REQUIRE(engine.effectiveListeners() == expected);
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, port);
    REQUIRE(probe.waitForConnected(2000));
}

namespace {
template<class Engine> void priorityAndStopRegression(Engine &engine, const QString &id)
{
    if constexpr (requires { engine.setWantedFiles(id, QList<int>{0, 1}, QMap<int, int>{{0, 1}, {1, 7}}); engine.stop(id); engine.jobs().first().stopped; engine.files(id).first().priority; }) {
        engine.setWantedFiles(id, {0, 1}, {{0, 1}, {1, 7}});
        REQUIRE(eventually([&] { const auto f = engine.files(id); return f.size() == 2 && f[0].priority == 1 && f[1].priority == 7; }));
        engine.stop(id);
        REQUIRE(engine.jobs().first().stopped);
        REQUIRE(engine.jobs().first().paused);
        REQUIRE(engine.jobs().first().downloadRate == 0);
        REQUIRE(eventually([&] { return engine.jobs().first().state == "Stopped"; }));
        REQUIRE(TorrentEngineTestAccess::call(engine, [=](auto &d) {
            const auto &r = d.records.at(id);
            return r.userStopped && r.userPaused;
        }));
    } else {
        FAIL("Per-file priorities and persistent Stop are not implemented");
    }
}
}
TEST_CASE("Torrent priorities and explicit stop survive engine restart", "[torrent-controls]") {
    application();
    QTemporaryDir dir;
    const auto meta = fixture(dir.path(), lt::create_torrent::v1_only, true);
    const auto settings = isolated(dir.filePath("downloads"));
    QString id;
    {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(settings, {});
        id = engine.add(meta, settings.downloadPath, {}, true);
        REQUIRE(eventually([&] { return engine.files(id).size() == 2; }));
        priorityAndStopRegression(engine, id);
        engine.shutdown();
    }
    TorrentEngine restored(dir.filePath("state"));
    restored.configure(settings, {});
    REQUIRE(eventually([&] { return restored.files(id).size() == 2; }));
    REQUIRE(restored.jobs().first().state == "Stopped");
    REQUIRE(TorrentEngineTestAccess::call(restored, [=](auto &d) {
        auto &r = d.records.at(id);
        const auto values = d.priorities(r, *r.resume.ti);
        return values[0] == lt::download_priority_t{1} && values[1] == lt::download_priority_t{7};
    }));
}

namespace {
// Disposable public CA fixtures; no private keys or deployed identities are used.
const QByteArray gostCaOne = R"PEM(
-----BEGIN CERTIFICATE-----
MIIBlTCCATugAwIBAgIUBDNJbMswkge9lQxwjWuuqYixaU4wCgYIKoZIzj0EAwIw
IDEeMBwGA1UEAwwVbG9vcGJhY2stdGFzazEtY2Etb25lMB4XDTI2MDkyNzAzMDA0
N1oXDTM2MDkyNDAzMDA0N1owIDEeMBwGA1UEAwwVbG9vcGJhY2stdGFzazEtY2Et
b25lMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEgWXWghOzZNDmMD2Vjx1xXpHf
UgR8YcCTdD48dziijrCVOC3GuJslpZ/vDabqTGsj2rpu3BZTztAifyH9xmZyoaNT
MFEwHQYDVR0OBBYEFPlpSvDDb8bWa1fnu7IlQ9ggDatCMB8GA1UdIwQYMBaAFPlp
SvDDb8bWa1fnu7IlQ9ggDatCMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwID
SAAwRQIgKIks2+FJuOJmzm23pbvRY7ylorqUJRdo3+WWk4pHpOECIQDAonjZGCbh
KSMaHYO8s5iWvmzuEa7Q3EUEZGTATh4CNQ==
-----END CERTIFICATE-----
)PEM";
const QByteArray gostCaTwo = R"PEM(
-----BEGIN CERTIFICATE-----
MIIBlTCCATugAwIBAgIUby25/Nzik2Csqp+MB3CBON/YOzswCgYIKoZIzj0EAwIw
IDEeMBwGA1UEAwwVbG9vcGJhY2stdGFzazEtY2EtdHdvMB4XDTI2MDkyNzAzMDA0
N1oXDTM2MDkyNDAzMDA0N1owIDEeMBwGA1UEAwwVbG9vcGJhY2stdGFzazEtY2Et
dHdvMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEJb5ofAsmTYIOmJIIL67qggKS
bKqg+XsYI0Ko4eeoEgv+4++h0PKU6/AeK/4nwOHURvPmqhvhJcQllAvSoe2iP6NT
MFEwHQYDVR0OBBYEFIkHJgokp4ghXyGpMVKIJIh50rmlMB8GA1UdIwQYMBaAFIkH
Jgokp4ghXyGpMVKIJIh50rmlMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwID
SAAwRQIhAO6pEvKFq9/EoKjNIDJD7FA+r1DPoxKxcVVT8xLIckE+AiAWSnxijk3l
IWNTe7y/VtSLB0uiaivvetv5BFeGPSpicg==
-----END CERTIFICATE-----
)PEM";
const QByteArray gostLeaf = R"PEM(
-----BEGIN CERTIFICATE-----
MIIBjzCCATSgAwIBAgIUMn6jZz9G/BadHFDw9n7cZuGcnugwCgYIKoZIzj0EAwIw
HjEcMBoGA1UEAwwTbG9vcGJhY2stdGFzazEtbGVhZjAeFw0yNjA5MjcwMzAwNDda
Fw0zNjA5MjQwMzAwNDdaMB4xHDAaBgNVBAMME2xvb3BiYWNrLXRhc2sxLWxlYWYw
WTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATdm9Mu+lG2vrlPHZ4OSE87Ehgl0OV+
linoDK64EJPDTMbLkdUJ+/Xx4mOiK1z0mutJ3hbnOMpPS9B6g1aHlD0Fo1AwTjAd
BgNVHQ4EFgQUZcJTgx8LEI8R/x794/lwMC67apYwHwYDVR0jBBgwFoAUZcJTgx8L
EI8R/x794/lwMC67apYwDAYDVR0TAQH/BAIwADAKBggqhkjOPQQDAgNJADBGAiEA
qWg2sj/xOKFjMHkbl4M+v4ggDSGmHFiI29qME3puzpsCIQD11ndusnhr6Vxin+8Q
coP+okRGxzX/7mzlkhGv/FtibQ==
-----END CERTIFICATE-----
)PEM";

void writeGostFixture(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    file.close();
}

QByteArray readGostFixture(const QString &path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

ProxyConfig gostProfile() {
    ProxyConfig proxy;
    proxy.type = ProxyType::Gost;
    proxy.host = "127.0.0.1";
    proxy.port = 1080;
    proxy.user = "fixture-user";
    proxy.password = "fixture-password";
    proxy.udp = true;
    return proxy;
}
}

TEST_CASE("GOST has stable serialized identity and empty deployment defaults", "[gost][profile]") {
    REQUIRE(int(ProxyType::Direct) == 0);
    REQUIRE(int(ProxyType::Socks5) == 1);
    REQUIRE(int(ProxyType::Socks5Tls) == 2);
    REQUIRE(int(ProxyType::Shadowsocks) == 3);
    REQUIRE(int(ProxyType::Gost) == 4);
    Settings settings;
    REQUIRE(settings.gostProxy.type == ProxyType::Gost);
    REQUIRE(settings.gostProxy.host.isEmpty());
    REQUIRE(settings.gostProxy.user.isEmpty());
    REQUIRE(settings.gostProxy.password.isEmpty());
    REQUIRE(settings.gostProxy.caFile.isEmpty());
    REQUIRE(settings.gostProxy.caPem.isEmpty());
    REQUIRE(validateSettings(settings).isEmpty());
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    REQUIRE_FALSE(validateSettings(settings).isEmpty());
}

TEST_CASE("GOST profile round trips without serializing runtime trust or losing other settings", "[gost][profile]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("settings.json");
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = gostProfile();
    settings.gostProxy.caFile = dir.filePath("private-ca.pem");
    settings.gostProxy.caPem = "runtime-only-trust-sentinel";
    settings.socks5Proxy = {ProxyType::Socks5Tls, "127.0.0.1", "socks-user", "socks-pass", {}, 1081, true, true};
    settings.shadowsocksProxy.host = "127.0.0.1";
    settings.shadowsocksProxy.password = "ss-fixture-secret";
    settings.enabled = false;
    settings.downloadPath = dir.filePath("downloads");
    settings.completedPath = dir.filePath("completed");
    settings.downloadLimitKiB = 13;
    settings.uploadLimitKiB = 17;
    settings.activeDownloads = 7;
    settings.connectionLimit = 71;
    settings.perTorrentConnections = 19;
    settings.seedRatio = 3.25;
    settings.seedMinutes = 11;
    settings.listenPort = 19001;
    settings.randomizePort = false;
    settings.bindAddress = "127.0.0.1";
    settings.bindAddress6 = "::1";
    settings.bootstrapNodes = "127.0.0.1:19002";
    settings.dht = settings.pex = settings.localDiscovery = false;
    settings.utp = settings.portMapping = false;
    settings.encryptionMode = EncryptionMode::Required;
    settings.blockedCountries = {"US", "UA"};
    settings.blockUnknownClients = true;
    settings.shareCompleted = false;
    settings.shareName = "Fixture downloads";
    REQUIRE(saveSettings(path, settings));
    const auto bytes = readGostFixture(path);
    REQUIRE_FALSE(bytes.contains("caPem"));
    REQUIRE_FALSE(bytes.contains("runtime-only-trust-sentinel"));
    const auto saved = QJsonDocument::fromJson(bytes).object();
    REQUIRE(saved["torrentProxy"].toObject()["gost"].toObject()["caFile"].toString() == settings.gostProxy.caFile);
    settings.gostProxy.caPem.clear();
    REQUIRE(loadSettings(path) == settings);
    REQUIRE(saveSettings(path, loadSettings(path)));
    REQUIRE(readGostFixture(path) == bytes);
    for (const auto type : {ProxyType::Socks5, ProxyType::Shadowsocks, ProxyType::Gost}) {
        settings.customProxyType = type;
        REQUIRE(saveSettings(path, settings));
        REQUIRE(loadSettings(path) == settings);
    }
}

TEST_CASE("Legacy settings without GOST retain profiles and malformed profile types fail closed", "[gost][profile]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("settings.json");
    Settings settings;
    settings.socks5Proxy.host = "127.0.0.1";
    REQUIRE(saveSettings(path, settings));
    auto document = QJsonDocument::fromJson(readGostFixture(path)).object();
    auto profiles = document["torrentProxy"].toObject();
    profiles.remove("gost");
    document["torrentProxy"] = profiles;
    writeGostFixture(path, QJsonDocument(document).toJson());
    REQUIRE(loadSettings(path) == settings);
    SECTION("Unknown selected type") { profiles["type"] = 99; }
    SECTION("Unknown GOST profile type") {
        profiles["gost"] = QJsonObject{{"type", 99}, {"host", "127.0.0.1"}, {"port", 1080},
            {"user", "fixture"}, {"password", "fixture"}, {"cipher", ""}, {"udp", true}, {"caFile", ""}};
    }
    SECTION("Malformed CA path") {
        profiles["gost"] = QJsonObject{{"type", 4}, {"host", "127.0.0.1"}, {"port", 1080},
            {"user", "fixture"}, {"password", "fixture"}, {"cipher", ""}, {"udp", true}, {"caFile", 42}};
    }
    document["torrentProxy"] = profiles;
    writeGostFixture(path, QJsonDocument(document).toJson());
    const auto rejected = loadSettings(path);
    REQUIRE_FALSE(rejected.enabled);
    REQUIRE(rejected.proxyMode == ProxyMode::RequireProxy);
}

TEST_CASE("GOST requires both credentials and enforces UTF8 byte limits", "[gost][profile]") {
    auto proxy = gostProfile();
    REQUIRE(validateProxy(proxy).isEmpty());
    for (auto member : {&ProxyConfig::user, &ProxyConfig::password}) {
        proxy = gostProfile();
        proxy.*member = {};
        REQUIRE_FALSE(validateProxy(proxy).isEmpty());
        proxy.*member = QString(255, QLatin1Char('a'));
        REQUIRE(validateProxy(proxy).isEmpty());
        proxy.*member += QLatin1Char('a');
        REQUIRE_FALSE(validateProxy(proxy).isEmpty());
        proxy.*member = QString(127, QChar(0x00e9)) + QLatin1Char('a');
        REQUIRE((proxy.*member).toUtf8().size() == 255);
        REQUIRE(validateProxy(proxy).isEmpty());
        proxy.*member = QString(128, QChar(0x00e9));
        REQUIRE((proxy.*member).toUtf8().size() == 256);
        REQUIRE_FALSE(validateProxy(proxy).isEmpty());
        proxy.*member = QString("nul") + QChar(0);
        REQUIRE_FALSE(validateProxy(proxy).isEmpty());
    }
    proxy = gostProfile();
    proxy.remoteDns = false;
    REQUIRE_FALSE(validateProxy(proxy).isEmpty());
    proxy = gostProfile();
    proxy.type = static_cast<ProxyType>(99);
    REQUIRE_FALSE(validateProxy(proxy).isEmpty());
    proxy = gostProfile();
    proxy.type = ProxyType::Socks5;
    proxy.user.clear();
    proxy.password.clear();
    REQUIRE(validateProxy(proxy).isEmpty());
    proxy.type = ProxyType::Socks5Tls;
    REQUIRE(validateProxy(proxy).isEmpty());
}

TEST_CASE("GOST selection preserves tunneled UDP and separate inactive profiles", "[gost][profile]") {
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = gostProfile();
    settings.gostProxy.caFile = "/fixture/ca.pem";
    settings.gostProxy.caPem = gostCaOne;
    REQUIRE(selectedProxy(settings, {}) == settings.gostProxy);
    settings.customProxyType = ProxyType::Socks5;
    REQUIRE(selectedProxy(settings, {}) == settings.socks5Proxy);
    settings.customProxyType = ProxyType::Shadowsocks;
    REQUIRE(selectedProxy(settings, {}) == settings.shadowsocksProxy);
    settings.proxyMode = ProxyMode::FollowApplication;
    REQUIRE(selectedProxy(settings, settings.socks5Proxy) == settings.socks5Proxy);
}

TEST_CASE("GOST settings replacement remains atomic and owner only", "[gost][profile]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("nested/settings.json");
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = gostProfile();
    REQUIRE(saveSettings(path, settings));
    QFile oldDocument(path);
    REQUIRE(oldDocument.open(QIODevice::ReadOnly));
    const auto before = readGostFixture(path);
    settings.gostProxy.password = "replacement-fixture";
    REQUIRE(saveSettings(path, settings));
#ifdef Q_OS_UNIX
    REQUIRE(oldDocument.readAll() == before);
    const auto permissions = QFile::permissions(path);
    REQUIRE((permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)) == 0);
    REQUIRE((permissions & (QFileDevice::ReadOwner | QFileDevice::WriteOwner)) ==
        (QFileDevice::ReadOwner | QFileDevice::WriteOwner));
#endif
    const auto valid = readGostFixture(path);
    REQUIRE(valid != before);
    settings.gostProxy.password.clear();
    QString error;
    REQUIRE_FALSE(saveSettings(path, settings, &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(readGostFixture(path) == valid);
}

TEST_CASE("GOST trust snapshots distinguish replacement at the same path", "[gost][profile][trust]") {
    application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto proxy = gostProfile();
    proxy.caFile = dir.filePath("ca.pem");
    writeGostFixture(proxy.caFile, gostCaOne);
    QString error = "previous failure";
    REQUIRE(loadProxyTrust(proxy, &error));
    REQUIRE(error.isEmpty());
    REQUIRE(proxy.caPem == gostCaOne);
    const auto first = proxy;
    REQUIRE(loadProxyTrust(proxy, &error));
    REQUIRE(proxy == first);
    writeGostFixture(proxy.caFile, gostCaTwo);
    REQUIRE(loadProxyTrust(proxy, &error));
    REQUIRE(proxy.caPem == gostCaTwo);
    REQUIRE(proxy != first);
    Settings a, b;
    a.gostProxy = first;
    b.gostProxy = proxy;
    REQUIRE(a != b);
    writeGostFixture(proxy.caFile, gostCaOne + gostCaTwo);
    REQUIRE(loadProxyTrust(proxy, nullptr));
    REQUIRE(proxy.caPem == gostCaOne + gostCaTwo);
    proxy.caFile.clear();
    REQUIRE(loadProxyTrust(proxy, &error));
    REQUIRE(proxy.caPem.isEmpty());
}

TEST_CASE("GOST supplied trust fails closed for missing malformed oversized or non CA data", "[gost][profile][trust]") {
    application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto proxy = gostProfile();
    proxy.caFile = dir.filePath("ca.pem");
    proxy.caPem = gostCaOne;
    SECTION("Missing file") {}
    SECTION("Directory") { proxy.caFile = dir.path(); }
    SECTION("Empty file") { writeGostFixture(proxy.caFile, {}); }
    SECTION("Invalid PEM") { writeGostFixture(proxy.caFile, "-----BEGIN CERTIFICATE-----\ninvalid\n-----END CERTIFICATE-----\n"); }
    SECTION("Truncated bundle") { writeGostFixture(proxy.caFile, gostCaOne + "-----BEGIN CERTIFICATE-----\n"); }
    SECTION("Trailing garbage") { writeGostFixture(proxy.caFile, gostCaOne + "garbage"); }
    SECTION("Leaf certificate") { writeGostFixture(proxy.caFile, gostLeaf); }
    SECTION("CA and leaf bundle") { writeGostFixture(proxy.caFile, gostCaOne + gostLeaf); }
    SECTION("Oversized file") { writeGostFixture(proxy.caFile, gostCaOne + QByteArray(1024 * 1024, ' ')); }
    QString error;
    REQUIRE_FALSE(loadProxyTrust(proxy, &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(proxy.caFile.isEmpty());
    REQUIRE(proxy.caPem.isEmpty());
}

TEST_CASE("GOST trust accepts the inclusive one MiB limit", "[gost][profile][trust]") {
    application();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto proxy = gostProfile();
    proxy.caFile = dir.filePath("ca.pem");
    const auto maximum = gostCaOne + QByteArray(1024 * 1024 - gostCaOne.size(), ' ');
    writeGostFixture(proxy.caFile, maximum);
    QString error;
    REQUIRE(loadProxyTrust(proxy, &error));
    REQUIRE(error.isEmpty());
    REQUIRE(proxy.caPem == maximum);
}

TEST_CASE("GOST adapted capability respects UDP preference and strict engine policy", "[gost][routing]") {
    Settings settings;
    settings.proxyMode = ProxyMode::Custom;
    settings.customProxyType = ProxyType::Gost;
    settings.gostProxy = gostProfile();
    settings.dht = settings.utp = settings.localDiscovery = settings.portMapping = true;
    settings.bootstrapNodes.clear();
    const auto upstream = selectedProxy(settings, {});
    REQUIRE_FALSE(routeError(settings, upstream).isEmpty());
    // Runtime supplies this authenticated loopback endpoint, never the raw GOST enum.
    ProxyConfig endpoint{ProxyType::Socks5, "127.0.0.1", "local-user", "local-token", {}, 23456, true, true};
    for (bool udp : {true, false}) {
        endpoint.udp = udp;
        REQUIRE(routeError(settings, endpoint).isEmpty());
        const auto pack = sessionSettings(settings, endpoint);
        using P = lt::settings_pack;
        REQUIRE(pack.get_bool(P::enable_dht) == udp);
        REQUIRE(pack.get_bool(P::enable_outgoing_utp) == udp);
        REQUIRE_FALSE(pack.get_bool(P::enable_incoming_tcp));
        REQUIRE_FALSE(pack.get_bool(P::enable_incoming_utp));
        REQUIRE_FALSE(pack.get_bool(P::enable_lsd));
        REQUIRE_FALSE(pack.get_bool(P::enable_upnp));
        REQUIRE_FALSE(pack.get_bool(P::enable_natpmp));
        REQUIRE(pack.get_bool(P::proxy_peer_connections));
        REQUIRE(pack.get_bool(P::proxy_tracker_connections));
        REQUIRE(pack.get_bool(P::proxy_hostnames));
        REQUIRE(pack.get_str(P::listen_interfaces).empty());
        REQUIRE(pack.get_int(P::proxy_type) == P::socks5_pw);
    }
    settings.dht = settings.utp = false;
    endpoint.udp = true;
    const auto pack = sessionSettings(settings, endpoint);
    REQUIRE_FALSE(pack.get_bool(lt::settings_pack::enable_dht));
    REQUIRE_FALSE(pack.get_bool(lt::settings_pack::enable_outgoing_utp));
}

TEST_CASE("GOST mapped UDP choice controls retained trackers without starting transfers", "[gost][routing]") {
    application();
    for (bool udp : {true, false}) {
        DYNAMIC_SECTION("UDP enabled " << udp) {
            QTemporaryDir dir;
            auto settings = isolated(dir.filePath("downloads"));
            settings.proxyMode = ProxyMode::Custom;
            settings.customProxyType = ProxyType::Gost;
            settings.gostProxy = gostProfile();
            settings.gostProxy.udp = udp;
            ProxyConfig endpoint{ProxyType::Socks5, "127.0.0.1", "local-user", "local-token", {}, 23456, true, udp};
            TorrentEngine engine(dir.filePath("state"));
            engine.configure(settings, endpoint);
            const QStringList trackers{"http://fixture.invalid/announce", "udp://fixture.invalid:80/announce"};
            const auto id = engine.add(fixture(dir.filePath("source"), lt::create_torrent::v1_only,
                                              false, trackers), {}, {}, true);
            REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
            const auto resume = TorrentEngineTestAccess::call(engine, [id](auto &d) { return d.records.at(id).resume; });
            REQUIRE(resume.trackers.size() == (udp ? 2 : 1));
            REQUIRE(resume.trackers.front() == "http://fixture.invalid/announce");
            if (udp) REQUIRE(resume.trackers.back() == "udp://fixture.invalid:80/announce");
            REQUIRE_FALSE(TorrentEngineTestAccess::active(engine));
        }
    }
}

namespace {
template<class Engine> void setExcluded(Engine &engine, const QString &id, bool excluded) {
    if constexpr (requires { engine.setDcShareExcluded(id, excluded); })
        engine.setDcShareExcluded(id, excluded);
    else FAIL("TorrentEngine has no per-job DC sharing exclusion");
}
template<class Snapshot> bool isExcluded(const Snapshot &job) {
    if constexpr (requires { job.dcShareExcluded; }) return job.dcShareExcluded;
    else { FAIL("Job has no persistent DC sharing exclusion"); return false; }
}
}

TEST_CASE("DC exclusion persists across configuration and restart and can follow global again", "[torrent][dc-exclusion]") {
    application();
    QTemporaryDir dir;
    const auto s = isolated(dir.filePath("payload"));
    const auto meta = fixture(s.downloadPath, lt::create_torrent::v1_only, false, {}, false);
    QString id;
    {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(s, {});
        id = engine.add(meta, s.downloadPath, {}, true);
        REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
        REQUIRE_FALSE(isExcluded(jobSnapshot(engine, id)));
        setExcluded(engine, id, true);
        REQUIRE(isExcluded(jobSnapshot(engine, id)));
        engine.configure(s, {});
        REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &) { return true; }));
        REQUIRE(isExcluded(jobSnapshot(engine, id)));
        engine.shutdown();
    }
    {
        TorrentEngine restored(dir.filePath("state"));
        restored.configure(s, {});
        REQUIRE(eventually([&] { return !restored.jobs().isEmpty(); }));
        REQUIRE(isExcluded(jobSnapshot(restored, id)));
        setExcluded(restored, id, false);
        REQUIRE_FALSE(isExcluded(jobSnapshot(restored, id)));
        restored.shutdown();
    }
    TorrentEngine restored(dir.filePath("state"));
    restored.configure(s, {});
    REQUIRE(eventually([&] { return !restored.jobs().isEmpty(); }));
    REQUIRE_FALSE(isExcluded(jobSnapshot(restored, id)));
}

TEST_CASE("DC exclusion malformed values fail closed while absent legacy values follow global", "[torrent][dc-exclusion]") {
    application();
    QTemporaryDir dir;
    const auto s = isolated(dir.filePath("payload"));
    QString id;
    {
        TorrentEngine engine(dir.filePath("state"));
        engine.configure(s, {});
        id = engine.add(fixture(s.downloadPath), s.downloadPath, {}, true);
        REQUIRE(eventually([&] { return !engine.files(id).isEmpty(); }));
        engine.shutdown();
    }
    QFile state(dir.filePath("state/jobs.json"));
    REQUIRE(state.open(QIODevice::ReadOnly));
    auto root = QJsonDocument::fromJson(state.readAll()).object();
    state.close();
    auto jobs = root["jobs"].toArray();
    auto item = jobs.first().toObject();
    bool excluded = true;
    SECTION("legacy absent") { item.remove("dcShareExcluded"); excluded = false; }
    SECTION("boolean false") { item.insert("dcShareExcluded", false); excluded = false; }
    SECTION("boolean true") { item.insert("dcShareExcluded", true); }
    SECTION("string false") { item.insert("dcShareExcluded", "false"); }
    SECTION("number zero") { item.insert("dcShareExcluded", 0); }
    SECTION("null") { item.insert("dcShareExcluded", QJsonValue::Null); }
    SECTION("array") { item.insert("dcShareExcluded", QJsonArray{}); }
    SECTION("object") { item.insert("dcShareExcluded", QJsonObject{}); }
    jobs[0] = item;
    root["jobs"] = jobs;
    REQUIRE(state.open(QIODevice::WriteOnly | QIODevice::Truncate));
    state.write(QJsonDocument(root).toJson());
    state.close();
    TorrentEngine restored(dir.filePath("state"));
    restored.configure(s, {});
    REQUIRE(eventually([&] { return !restored.jobs().isEmpty(); }));
    REQUIRE(isExcluded(jobSnapshot(restored, id)) == excluded);
}

TEST_CASE("DC exclusion fences a stale worker publication synchronously", "[torrent][dc-exclusion]") {
    application();
    QTemporaryDir dir;
    auto s = isolated(dir.filePath("payload"));
    TorrentEngine engine(dir.filePath("state"));
    const auto epoch = engine.configure(s, {});
    const auto id = engine.add(fixture(s.downloadPath, lt::create_torrent::v1_only, false, {}, false));
    REQUIRE(eventually([&] { return jobSnapshot(engine, id).complete; }));
    const auto before = jobSnapshot(engine, id).revision;
    REQUIRE(engine.publicationValid(id, before, epoch));
    struct Barrier { std::atomic_bool entered{false}, release{false}, published{false}, finish{false}; };
    auto barrier = std::make_shared<Barrier>();
    struct Release { std::shared_ptr<Barrier> b; ~Release() { b->release = true; b->finish = true; } } release{barrier};
    TorrentEngineTestAccess::call(engine, [&](auto &d) {
        return d.enqueue([&d, barrier] {
            barrier->entered = true;
            while (!barrier->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            d.publish();
            barrier->published = true;
            while (!barrier->finish) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    });
    REQUIRE(eventually([&] { return barrier->entered.load(); }));
    setExcluded(engine, id, true);
    REQUIRE(isExcluded(jobSnapshot(engine, id)));
    REQUIRE_FALSE(engine.publicationValid(id, before, epoch));
    REQUIRE_FALSE(engine.publicationValid(id, jobSnapshot(engine, id).revision, epoch));
    barrier->release = true;
    REQUIRE(eventually([&] { return barrier->published.load(); }));
    REQUIRE(isExcluded(jobSnapshot(engine, id)));
    REQUIRE_FALSE(engine.publicationValid(id, before, epoch));
    barrier->finish = true;
    REQUIRE(TorrentEngineTestAccess::call(engine, [](auto &) { return true; }));
    setExcluded(engine, id, false);
    REQUIRE(eventually([&] { return engine.publicationValid(id, jobSnapshot(engine, id).revision, epoch); }));
    REQUIRE_FALSE(engine.publicationValid(id, before, epoch));
}
