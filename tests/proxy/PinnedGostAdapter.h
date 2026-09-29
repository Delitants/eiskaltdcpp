#pragma once

#include <QHostAddress>
#include <QJsonArray>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <memory>
#include <vector>

namespace proxy_test {
// Test-only address translation in a separate process. All udpSlots are bound before
// sandboxing the engine child. This forwards ONLY to a real TorrentProxyAdapter;
// it cannot connect to torrent destinations or replace authentication/TLS.
class PinnedGostAdapter : public QObject {
    struct Slot {
        QUdpSocket socket;
        bool used = false;
        quint16 upstream = 0, client = 0;
        Slot() { socket.setProxy(QNetworkProxy::NoProxy); }
    };
    struct Session : QObject {
        PinnedGostAdapter &owner;
        QTcpSocket *client;
        QTcpSocket upstream;
        QByteArray fromClient, fromServer;
        int clientStage = 0, serverStage = 0;
        Slot *slot = nullptr;
        bool ended = false;
        Session(PinnedGostAdapter &parent, QTcpSocket *socket, quint16 port)
            : QObject(&parent), owner(parent), client(socket) {
            client->setParent(this);
            upstream.setProxy(QNetworkProxy::NoProxy);
            connect(client, &QTcpSocket::readyRead, this, [this] { fromClient += client->readAll(); pumpClient(); });
            connect(&upstream, &QTcpSocket::connected, this, [this] { pumpClient(); });
            connect(&upstream, &QTcpSocket::readyRead, this, [this] { fromServer += upstream.readAll(); pumpServer(); });
            connect(client, &QTcpSocket::disconnected, this, [this] { finish(); });
            connect(&upstream, &QTcpSocket::disconnected, this, [this] {
                fromServer += upstream.readAll();
                pumpServer();
                // Drain the HTTP tracker response before delivering EOF.
                client->disconnectFromHost();
            });
            connect(&upstream, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
                if (error != QAbstractSocket::RemoteHostClosedError) finish();
            });
            upstream.connectToHost(QHostAddress::LocalHost, port);
        }
        ~Session() override {
            ended = true;
            disconnect(client, nullptr, this, nullptr);
            disconnect(&upstream, nullptr, this, nullptr);
            if (slot) { slot->used = false; slot->upstream = slot->client = 0;
                while (slot->socket.hasPendingDatagrams()) slot->socket.receiveDatagram(); }
        }
        void finish() {
            if (ended) return;
            ended = true;
            client->abort(); upstream.abort(); deleteLater();
        }
        static int addressSize(const QByteArray &bytes) {
            if (bytes.size() < 4) return 0;
            switch (uint8_t(bytes[3])) {
            case 1: return 10;
            case 4: return 22;
            case 3: return bytes.size() < 5 ? 0 : 7 + uint8_t(bytes[4]);
            default: return -1;
            }
        }
        void pumpClient() {
            if (ended || upstream.state() != QAbstractSocket::ConnectedState) return;
            while (!fromClient.isEmpty()) {
                int count = 0;
                if (clientStage == 0) {
                    if (fromClient.size() < 2) return;
                    count = 2 + uint8_t(fromClient[1]);
                } else if (clientStage == 1) {
                    if (fromClient.size() < 2) return;
                    int length = uint8_t(fromClient[1]);
                    if (fromClient.size() < 3 + length) return;
                    count = 3 + length + uint8_t(fromClient[2 + length]);
                } else if (clientStage == 2) count = addressSize(fromClient);
                else count = fromClient.size();
                if (count < 0) { finish(); return; }
                if (!count || fromClient.size() < count) return;
                auto message = fromClient.left(count);
                fromClient.remove(0, count);
                if (clientStage == 2 && uint8_t(message[1]) == 3) {
                    for (auto &candidate : owner.udpSlots) if (!candidate->used) { slot = candidate.get(); break; }
                    if (!slot) { finish(); return; }
                    slot->used = true;
                    // The actual adapter sees this forwarder's fixed source, not the
                    // engine's ephemeral UDP socket. Datagram bytes stay unchanged.
                    message = QByteArray::fromHex("050300017f000001");
                    message += char(slot->socket.localPort() >> 8);
                    message += char(slot->socket.localPort());
                }
                upstream.write(message);
                if (clientStage < 3) ++clientStage;
            }
        }
        void pumpServer() {
            while (!fromServer.isEmpty()) {
                const int count = serverStage < 2 ? 2 : serverStage == 2 ? addressSize(fromServer) : fromServer.size();
                if (count < 0) { finish(); return; }
                if (!count || fromServer.size() < count) return;
                auto message = fromServer.left(count);
                fromServer.remove(0, count);
                if (serverStage == 2 && slot && uint8_t(message[1]) == 0) {
                    if (message.left(8) != QByteArray::fromHex("050000017f000001")) { finish(); return; }
                    slot->upstream = (uint8_t(message[8]) << 8) | uint8_t(message[9]);
                    message[8] = char(slot->socket.localPort() >> 8);
                    message[9] = char(slot->socket.localPort());
                    ++owner.associations;
                }
                client->write(message);
                if (serverStage < 3) ++serverStage;
            }
        }
    };
    QTcpServer server;
    std::vector<std::unique_ptr<Slot>> udpSlots;
public:
    int associations = 0;
    qint64 outboundDatagrams = 0, inboundDatagrams = 0;
    QJsonArray udpHeaders;
    explicit PinnedGostAdapter(QObject *parent = nullptr) : QObject(parent) {}
    bool start(quint16 adapterPort) {
        if (!server.listen(QHostAddress::LocalHost)) return false;
        for (int i = 0; i < 12; ++i) {
            auto slot = std::make_unique<Slot>();
            if (!slot->socket.bind(QHostAddress(QHostAddress::LocalHost), quint16(0))) return false;
            auto *s = slot.get();
            connect(&s->socket, &QUdpSocket::readyRead, this, [this, s] {
                while (s->socket.hasPendingDatagrams()) {
                    const auto packet = s->socket.receiveDatagram();
                    if (!s->used || !s->upstream || packet.senderAddress() != QHostAddress::LocalHost) continue;
                    if (packet.senderPort() == s->upstream) {
                        if (s->client) {
                            s->socket.writeDatagram(packet.data(), QHostAddress::LocalHost, s->client);
                            ++inboundDatagrams;
                        }
                    } else if (!s->client || packet.senderPort() == s->client) {
                        s->client = packet.senderPort();
                        if (udpHeaders.size() < 32)
                            udpHeaders.append(QString::fromLatin1(packet.data().left(40).toHex()));
                        s->socket.writeDatagram(packet.data(), QHostAddress::LocalHost, s->upstream);
                        ++outboundDatagrams;
                    }
                }
            });
            udpSlots.push_back(std::move(slot));
        }
        connect(&server, &QTcpServer::newConnection, this, [this, adapterPort] {
            while (auto *socket = server.nextPendingConnection()) new Session(*this, socket, adapterPort);
        });
        return true;
    }
    quint16 port() const { return server.serverPort(); }
    QJsonArray udpPorts() const {
        QJsonArray ports;
        for (const auto &slot : udpSlots) ports.append(slot->socket.localPort());
        return ports;
    }
    ~PinnedGostAdapter() override {
        // Sessions reference udpSlots; tear them down before destroying the pool.
        const auto children = this->children();
        for (auto *child : children) delete child;
    }
};
} // namespace proxy_test
