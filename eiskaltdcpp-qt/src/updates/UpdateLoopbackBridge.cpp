#include "dcpp/stdinc.h"
#include "dcpp/ProxyRoute.h"
#include "UpdateLoopbackBridge.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <openssl/rand.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace updates {
namespace {
constexpr int maxClients = 8, maxHeaders = 8192, chunkSize = 32768;
struct Resource {
    QFile file;
    qint64 size = 0;
#ifndef _WIN32
    struct stat identity{};
#endif
    bool unchanged() const {
#ifdef _WIN32
        return false;
#else
        struct stat now{};
        if(::fstat(file.handle(), &now) != 0) return false;
#ifdef __APPLE__
        const bool times = now.st_mtimespec.tv_sec == identity.st_mtimespec.tv_sec &&
            now.st_mtimespec.tv_nsec == identity.st_mtimespec.tv_nsec &&
            now.st_ctimespec.tv_sec == identity.st_ctimespec.tv_sec &&
            now.st_ctimespec.tv_nsec == identity.st_ctimespec.tv_nsec;
#else
        const bool times = now.st_mtim.tv_sec == identity.st_mtim.tv_sec &&
            now.st_mtim.tv_nsec == identity.st_mtim.tv_nsec &&
            now.st_ctim.tv_sec == identity.st_ctim.tv_sec &&
            now.st_ctim.tv_nsec == identity.st_ctim.tv_nsec;
#endif
        return times && now.st_dev == identity.st_dev && now.st_ino == identity.st_ino &&
            now.st_size == size && now.st_mode == identity.st_mode &&
            now.st_uid == identity.st_uid && now.st_nlink == 1;
#endif
    }
};

std::shared_ptr<Resource> openPrivate(const QString& path, qint64 maximum, qint64 expected = -1) {
#ifdef _WIN32
    return {};
#else
    const QFileInfo info(path);
    if(path.contains(QChar('\0')) || !info.isAbsolute() || info.fileName().isEmpty()) return {};
    struct Descriptor {
        int fd;
        ~Descriptor() { if(fd >= 0) ::close(fd); }
    };
    Descriptor parent{::open(QFile::encodeName(info.absolutePath()).constData(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    struct stat status{};
    if(parent.fd < 0 || ::fstat(parent.fd, &status) != 0 || status.st_uid != getuid() || (status.st_mode & 0077)) return {};
    Descriptor source{::openat(parent.fd, QFile::encodeName(info.fileName()).constData(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    if(source.fd < 0 || ::fstat(source.fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_uid != getuid() || (status.st_mode & 0077) || status.st_nlink != 1 ||
        status.st_size <= 0 || status.st_size > maximum || (expected >= 0 && status.st_size != expected)) return {};
    auto result = std::make_shared<Resource>();
    if(!result->file.open(source.fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) return {};
    source.fd = -1;
    result->identity = status;
    result->size = status.st_size;
    return result;
#endif
}
bool tokenCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        QByteArrayLiteral("!#$%&'*+-.^_`|~").contains(c);
}
}

struct UpdateLoopbackBridge::Impl {
    struct Client {
        QByteArray header;
        std::shared_ptr<Resource> resource;
        qint64 sent = 0;
        QTimer headerDeadline, idleDeadline, totalDeadline;
        bool serving = false, pumping = false;
    };
    UpdateLoopbackBridge& owner;
    QTcpServer server;
    QTimer routeTimer;
    QByteArray authority, prefix;
    std::shared_ptr<const dcpp::ProxyRouteSnapshot> route;
    quint64 generation = 0;
    std::shared_ptr<Resource> feed, archive;
    QHash<QTcpSocket*, std::shared_ptr<Client>> clients;

    explicit Impl(UpdateLoopbackBridge& bridge) : owner(bridge) {
        server.setParent(&owner);
        server.setMaxPendingConnections(maxClients);
        QObject::connect(&server, &QTcpServer::newConnection, &owner, [this] { accept(); });
        routeTimer.setInterval(100);
        QObject::connect(&routeTimer, &QTimer::timeout, &owner, [this] { if(!active()) owner.stop(); });
    }
    bool active() const {
        return server.isListening() && route && route->valid && route->revoked &&
            !route->revoked->load() && route->generation == generation;
    }
    void close(QTcpSocket* socket) {
        QObject::disconnect(socket, nullptr, &owner, nullptr);
        clients.remove(socket);
        socket->abort();
        socket->deleteLater();
    }
    void accept() {
        while(server.hasPendingConnections()) {
            auto* socket = server.nextPendingConnection();
            if(!socket) break;
            if(!active() || clients.size() >= maxClients || socket->peerAddress() != QHostAddress::LocalHost) {
                socket->abort(); socket->deleteLater(); continue;
            }
            socket->setReadBufferSize(maxHeaders + 1);
            const auto client = std::make_shared<Client>();
            clients.insert(socket, client);
            for(auto* timer : {&client->headerDeadline, &client->idleDeadline, &client->totalDeadline}) {
                timer->setSingleShot(true);
                QObject::connect(timer, &QTimer::timeout, &owner, [this, socket] { close(socket); });
            }
            client->headerDeadline.start(5000);
            client->idleDeadline.start(30000);
            client->totalDeadline.start(30 * 60 * 1000);
            QObject::connect(socket, &QTcpSocket::readyRead, &owner, [this, socket] { read(socket); });
            QObject::connect(socket, &QTcpSocket::bytesWritten, &owner, [this, socket](qint64) {
                const auto client = clients.value(socket);
                if(client) { client->idleDeadline.start(30000); send(socket); }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, &owner, [this, socket] { close(socket); });
            if(socket->bytesAvailable()) read(socket);
        }
    }
    void read(QTcpSocket* socket) {
        const auto client = clients.value(socket);
        if(!client) return;
        if(!active()) { owner.stop(); return; }
        if(client->serving) { close(socket); return; }
        client->header += socket->read(maxHeaders + 1 - client->header.size());
        if(client->header.size() > maxHeaders) { close(socket); return; }
        const auto end = client->header.indexOf("\r\n\r\n");
        if(end < 0) return;
        if(end + 4 != client->header.size()) { close(socket); return; }
        auto lines = client->header.left(end).split('\n');
        for(int i = 0; i + 1 < lines.size(); ++i) {
            if(!lines[i].endsWith('\r')) { close(socket); return; }
            lines[i].chop(1);
        }
        const auto request = lines.takeFirst().split(' ');
        if(request.size() != 3 || request[0] != "GET" || request[2] != "HTTP/1.1" || lines.size() > 32) { close(socket); return; }
        QSet<QByteArray> seen;
        bool hostSeen = false;
        for(const auto& line : lines) {
            const auto colon = line.indexOf(':');
            const auto name = line.left(colon).toLower();
            if(colon <= 0 || !std::all_of(name.begin(), name.end(), tokenCharacter) || seen.contains(name)) { close(socket); return; }
            seen.insert(name);
            const auto rawValue = line.mid(colon + 1);
            if(!std::all_of(rawValue.begin(), rawValue.end(), [](unsigned char c) { return c == '\t' || (c >= 32 && c <= 126); })) { close(socket); return; }
            const auto value = rawValue.trimmed();
            if(name == "host") { hostSeen = value == authority; if(!hostSeen) { close(socket); return; } }
            if(name == "transfer-encoding" || name == "range" || name == "upgrade" || name == "expect" ||
                name == "authorization" || name == "proxy-authorization" ||
                (name == "content-length" && value != "0")) { close(socket); return; }
        }
        if(!hostSeen) { close(socket); return; }
        QByteArray mime;
        if(request[1] == prefix + "feed.xml") { client->resource = feed; mime = "application/xml"; }
        if(request[1] == prefix + "update.zip") { client->resource = archive; mime = "application/zip"; }
        if(!client->resource || !client->resource->unchanged()) { close(socket); return; }
        client->headerDeadline.stop();
        client->header.clear();
        client->serving = true;
        const QByteArray header = "HTTP/1.1 200 OK\r\nContent-Type: " + mime +
            "\r\nContent-Length: " + QByteArray::number(client->resource->size) +
            "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
        if(socket->write(header) != header.size()) { close(socket); return; }
        send(socket);
    }
    void send(QTcpSocket* socket) {
        const auto client = clients.value(socket);
        if(!client || !client->serving || client->pumping) return;
        if(!active()) { owner.stop(); return; }
        if(!client->resource->unchanged()) { close(socket); return; }
        client->pumping = true;
        const auto& resource = client->resource;
        // Keep Qt's socket queue bounded even if the installer stops reading.
        if(client->sent < resource->size && socket->bytesToWrite() < chunkSize) {
            const auto count = std::min<qint64>(chunkSize, resource->size - client->sent);
            if(!resource->file.seek(client->sent)) { close(socket); return; }
            const auto bytes = resource->file.read(count);
            if(bytes.size() != count || socket->write(bytes) != count) { close(socket); return; }
            client->sent += count;
        }
        client->pumping = false;
        if(client->sent == resource->size && socket->bytesToWrite() == 0) socket->disconnectFromHost();
    }
    QUrl url(const QByteArray& name) const { return QUrl::fromEncoded("http://" + authority + prefix + name); }
};

UpdateLoopbackBridge::UpdateLoopbackBridge(QObject* parent) : QObject(parent), impl(std::make_unique<Impl>(*this)) {}
UpdateLoopbackBridge::~UpdateLoopbackBridge() { stop(); }

QUrl UpdateLoopbackBridge::beginSession(const QString& path, std::shared_ptr<const dcpp::ProxyRouteSnapshot> route) {
    Q_ASSERT(QThread::currentThread() == thread());
    stop();
    if(!route || !route->valid || !route->revoked || route->revoked->load()) return {};
    auto feed = openPrivate(path, 1024 * 1024);
    if(!feed) return {};
    QByteArray capability(32, '\0');
    if(RAND_bytes(reinterpret_cast<unsigned char*>(capability.data()), capability.size()) != 1 ||
        !impl->server.listen(QHostAddress::LocalHost, 0)) return {};
    impl->route = std::move(route);
    impl->generation = impl->route->generation;
    impl->feed = std::move(feed);
    impl->prefix = "/" + capability.toHex() + "/";
    impl->authority = "127.0.0.1:" + QByteArray::number(impl->server.serverPort());
    impl->routeTimer.start();
    return impl->url("feed.xml");
}

QUrl UpdateLoopbackBridge::mapArchive(const QString& path, qint64 expectedSize) {
    Q_ASSERT(QThread::currentThread() == thread());
    if(!impl->active() || impl->archive || expectedSize <= 0 || expectedSize > 512LL * 1024 * 1024) return {};
    auto archive = openPrivate(path, 512LL * 1024 * 1024, expectedSize);
    if(!archive) return {};
    impl->archive = std::move(archive);
    return impl->url("update.zip");
}

void UpdateLoopbackBridge::stop() {
    Q_ASSERT(QThread::currentThread() == thread());
    impl->routeTimer.stop();
    impl->server.close();
    while(impl->server.hasPendingConnections()) {
        auto* socket = impl->server.nextPendingConnection();
        if(socket) { socket->abort(); socket->deleteLater(); }
    }
    const auto sockets = impl->clients.keys();
    for(auto* socket : sockets) impl->close(socket);
    impl->feed.reset(); impl->archive.reset(); impl->route.reset();
    impl->authority.clear(); impl->prefix.clear();
}
}
