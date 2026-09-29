#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QHash>
#include <QSet>
#include <functional>
#include <memory>

// Loopback-only fixture. It never resolves arbitrary names or contacts the Internet.
class LocalSocksProxy {
public:
    QTcpServer server;
    int authenticated = 0, forwarded = 0, udpRejected = 0, domainRequests = 0;
    qint64 tcpClientBytes = 0, tcpTargetBytes = 0;
    bool udpEnabled = false;
    bool udpTransientFailure = false;
    bool requireAuthentication = true, udpRelayV6 = false, udpWrongReplyPort = false;
    std::function<QByteArray(QByteArray)> udpReplyTransform, associateReplyTransform;
    QSet<quint16> udpAllowedPorts;
    int udpAssociated = 0, udpActive = 0, udpDropped = 0, udpDomainForwarded = 0;
    QHash<quint16, int> udpForwarded, udpReturned;
    QList<quint16> udpEgressPorts;
    QSet<QByteArray> udpDomainNames;

    LocalSocksProxy() {
        server.setProxy(QNetworkProxy::NoProxy);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) accept(server.nextPendingConnection());
        });
    }
private:
    struct Channel {
        QByteArray bytes;
        int stage = 0;
        QTcpSocket *target = nullptr;
        QUdpSocket *relay = nullptr, *outbound = nullptr;
        quint16 clientPort = 0;
        QHostAddress clientAddress;
        QSet<quint16> contactedPorts;
    };
    static QByteArray udpHeader(quint16 port, bool ipv6 = false) {
        auto header = ipv6 ? QByteArray::fromHex("00000004") + QByteArray(15, '\0') + char(1) :
                            QByteArray::fromHex("000000017f000001");
        header.append(char(port >> 8));
        header.append(char(port & 0xff));
        return header;
    }
    void associate(QTcpSocket *client, const std::shared_ptr<Channel> &state, quint16 port) {
        auto *relay = new QUdpSocket(client);
        auto *outbound = new QUdpSocket(client);
        relay->setProxy(QNetworkProxy::NoProxy);
        outbound->setProxy(QNetworkProxy::NoProxy);
        if (!relay->bind(QHostAddress(udpRelayV6 ? QHostAddress::LocalHostIPv6 : QHostAddress::LocalHost), quint16(0)) ||
            !outbound->bind(QHostAddress(QHostAddress::LocalHost), quint16(0))) {
            client->abort();
            return;
        }
        state->relay = relay;
        state->outbound = outbound;
        state->clientPort = port;
        state->clientAddress = udpRelayV6 ? QHostAddress(QHostAddress::LocalHostIPv6) : client->peerAddress();
        state->stage = 4;
        ++udpAssociated;
        ++udpActive;
        udpEgressPorts << outbound->localPort();
        auto reply = udpHeader(relay->localPort(), udpRelayV6);
        reply[0] = 5;
        if (associateReplyTransform) reply = associateReplyTransform(reply);
        client->write(reply);
        QObject::connect(relay, &QUdpSocket::readyRead, client, [this, client, state] {
            while (state->relay->hasPendingDatagrams()) {
                const auto packet = state->relay->receiveDatagram(65536);
                const auto &b = packet.data();
                if (packet.senderAddress() != state->clientAddress ||
                    (state->clientPort && packet.senderPort() != state->clientPort) ||
                    b.size() < 10 || b[0] || b[1] || b[2]) { ++udpDropped; continue; }
                int addressLength = 0;
                if (b[3] == 1 && b.mid(4, 4) == QByteArray::fromHex("7f000001")) addressLength = 4;
                else if (b[3] == 3) {
                    const auto name = b.mid(5, quint8(b[4]));
                    if (name == "fixture.invalid" || name == "127.0.0.1") addressLength = 1 + quint8(b[4]);
                }
                // Never resolve a name or forward an unknown address family/destination.
                if (!addressLength || b.size() <= 6 + addressLength) { ++udpDropped; continue; }
                const quint16 port = quint16((quint8(b[4 + addressLength]) << 8) | quint8(b[5 + addressLength]));
                if (!udpAllowedPorts.contains(port)) { ++udpDropped; continue; }
                state->clientPort = packet.senderPort();
                const auto payload = b.mid(6 + addressLength);
                if (state->outbound->writeDatagram(payload, QHostAddress::LocalHost, port) == payload.size()) {
                    state->contactedPorts.insert(port);
                    ++udpForwarded[port];
                    if (b[3] == 3) { ++udpDomainForwarded; udpDomainNames.insert(b.mid(5, quint8(b[4]))); }
                }
            }
        });
        QObject::connect(outbound, &QUdpSocket::readyRead, client, [this, state] {
            while (state->outbound->hasPendingDatagrams()) {
                const auto packet = state->outbound->receiveDatagram(65536);
                if (!state->clientPort || packet.senderAddress() != QHostAddress::LocalHost ||
                    !state->contactedPorts.contains(packet.senderPort()) || packet.data().size() > 65497) {
                    ++udpDropped;
                    continue;
                }
                auto wrapped = udpHeader(packet.senderPort()) + packet.data();
                if (udpReplyTransform) wrapped = udpReplyTransform(wrapped);
                if (wrapped.isEmpty()) continue;
                auto *sender = udpWrongReplyPort ? state->outbound : state->relay;
                if (sender->writeDatagram(wrapped, state->clientAddress, state->clientPort) == wrapped.size())
                    ++udpReturned[packet.senderPort()];
            }
        });
        QObject::connect(client, &QTcpSocket::disconnected, client, [this, state] {
            // RFC 1928: the UDP association ends with its authenticated TCP channel.
            state->relay->close();
            state->outbound->close();
            --udpActive;
        });
    }
    void accept(QTcpSocket *client) {
        auto state = std::make_shared<Channel>();
        QObject::connect(client, &QTcpSocket::readyRead, client, [this, client, state] {
            if (state->stage == 4) { client->abort(); return; }
            if (state->stage == 3) {
                if (state->target->bytesToWrite() > 1024 * 1024) { client->abort(); return; }
                const auto data = client->readAll();
                tcpClientBytes += data.size();
                state->target->write(data);
                return;
            }
            state->bytes += client->readAll();
            if (state->bytes.size() > 65536) { client->abort(); return; }
            auto &b = state->bytes;
            if (state->stage == 0) {
                if (b.size() < 2 || b.size() < 2 + quint8(b[1])) return;
                const auto methods = b.mid(2, quint8(b[1]));
                const bool auth = requireAuthentication || !methods.contains(char(0));
                if (b[0] != 5 || !methods.contains(char(auth ? 2 : 0))) { client->abort(); return; }
                b.remove(0, 2 + quint8(b[1]));
                client->write(auth ? QByteArray::fromHex("0502") : QByteArray::fromHex("0500"));
                state->stage = auth ? 1 : 2;
            }
            if (state->stage == 1) {
                if (b.size() < 2) return;
                const int userLength = quint8(b[1]);
                if (b.size() < 3 + userLength) return;
                const int passwordLength = quint8(b[2 + userLength]);
                if (b.size() < 3 + userLength + passwordLength) return;
                if (b[0] != 1 || b.mid(2, userLength) != "fixture-user" ||
                    b.mid(3 + userLength, passwordLength) != "fixture-password") { client->abort(); return; }
                b.remove(0, 3 + userLength + passwordLength);
                ++authenticated;
                client->write(QByteArray::fromHex("0100"));
                state->stage = 2;
            }
            if (state->stage == 2) {
                if (b.size() < 5) return;
                if (b[0] != 5 || b[2] || (b[3] != 1 && b[3] != 3 && b[3] != 4)) { client->abort(); return; }
                const bool domain = b[3] == 3;
                const int addressLength = domain ? 1 + quint8(b[4]) : b[3] == 1 ? 4 : 16;
                if (b.size() < 6 + addressLength) return;
                const quint16 port = quint16((quint8(b[4 + addressLength]) << 8) | quint8(b[5 + addressLength]));
                if (b[1] == 3) {
                    if (!udpEnabled) {
                        ++udpRejected;
                        if (udpTransientFailure) { client->abort(); return; }
                        client->write(QByteArray::fromHex("05070001000000000000"));
                        client->disconnectFromHost(); return;
                    }
                    if (b[3] != 1 || (b.mid(4, 4) != QByteArray::fromHex("00000000") &&
                        b.mid(4, 4) != QByteArray::fromHex("7f000001"))) { client->abort(); return; }
                    b.clear();
                    associate(client, state, port);
                    return;
                }
                if (b[1] != 1 || (domain && b.mid(5, addressLength - 1) != "fixture.invalid") ||
                    (!domain && b.mid(4, addressLength) != QByteArray::fromHex("7f000001"))) { client->abort(); return; }
                if (domain) ++domainRequests;
                b.remove(0, 6 + addressLength);
                auto *target = new QTcpSocket(client);
                target->setProxy(QNetworkProxy::NoProxy);
                state->target = target;
                state->stage = 3;
                QObject::connect(target, &QTcpSocket::connected, client, [this, client, target, state] {
                    ++forwarded;
                    client->write(QByteArray::fromHex("050000017f0000010000"));
                    if (!state->bytes.isEmpty()) { tcpClientBytes += state->bytes.size(); target->write(state->bytes); state->bytes.clear(); }
                });
                QObject::connect(target, &QTcpSocket::readyRead, client, [this, client, target] {
                    if (client->bytesToWrite() > 1024 * 1024) { target->abort(); return; }
                    const auto data = target->readAll();
                    tcpTargetBytes += data.size();
                    client->write(data);
                });
                QObject::connect(target, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
                QObject::connect(target, &QTcpSocket::errorOccurred, client, [client] { client->abort(); });
                QObject::connect(client, &QTcpSocket::disconnected, target, &QTcpSocket::abort);
                target->connectToHost(QHostAddress::LocalHost, port);
            }
        });
        QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
    }
};
