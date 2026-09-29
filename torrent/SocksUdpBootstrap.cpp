#include "SocksUdpBootstrap.h"

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>
#include <QtEndian>
#include <libtorrent/bdecode.hpp>
#include <algorithm>

namespace eiskalt::torrent {
namespace {

struct Budget {
    QDeadlineTimer deadline;
    const std::function<bool()> &cancelled;
    int slice() const {
        if (cancelled && cancelled()) return 0;
        return int(std::clamp<qint64>(deadline.remainingTime(), 0, 100));
    }
};

bool send(QTcpSocket &socket, const QByteArray &bytes, const Budget &budget) {
    if (!budget.slice() || socket.write(bytes) != bytes.size()) return false;
    while (socket.bytesToWrite()) {
        const auto wait = budget.slice();
        if (!wait || socket.state() != QAbstractSocket::ConnectedState) return false;
        socket.waitForBytesWritten(wait);
    }
    return true;
}

QByteArray receive(QTcpSocket &socket, int count, const Budget &budget) {
    QByteArray bytes;
    while (bytes.size() < count) {
        const auto wait = budget.slice();
        if (!wait || socket.state() != QAbstractSocket::ConnectedState) return {};
        bytes += socket.read(count - bytes.size());
        if (bytes.size() < count) socket.waitForReadyRead(wait);
    }
    return bytes;
}

QHostAddress address(const QByteArray &bytes) {
    if (bytes.size() == 4)
        return QHostAddress(qFromBigEndian<quint32>(bytes.constData()));
    if (bytes.size() == 16) {
        Q_IPV6ADDR ip;
        std::copy_n(reinterpret_cast<const quint8 *>(bytes.constData()), 16, ip.c);
        return QHostAddress(ip);
    }
    return {};
}

bool usable(const QHostAddress &ip) {
    return !ip.isNull() && ip != QHostAddress::AnyIPv4 && ip != QHostAddress::AnyIPv6 &&
        ip != QHostAddress::Broadcast && !ip.isMulticast() && ip.scopeId().isEmpty();
}

QString endpoint(const QHostAddress &ip, quint16 port) {
    const auto host = ip.protocol() == QAbstractSocket::IPv6Protocol ? '[' + ip.toString() + ']' : ip.toString();
    return host + ':' + QString::number(port);
}

struct Name {
    QByteArray host;
    quint16 port;
    qsizetype index;
};

} // namespace

QStringList proxyBootstrapNodes(const ProxyConfig &proxy, const QStringList &nodes,
                               const std::function<bool()> &cancelled, int timeoutMs) {
    const Budget budget{QDeadlineTimer(std::clamp(timeoutMs, 0, 5000), Qt::PreciseTimer), cancelled};
    static const QRegularExpression hostName(QStringLiteral("^[a-zA-Z0-9_.-]{1,253}$"));
    static const QRegularExpression portNumber(QStringLiteral("^[0-9]{1,5}$"));
    QStringList resolved;
    QList<Name> names;
    for (const auto &input : nodes) {
        resolved.append(QString{});
        const auto node = input.trimmed();
        const auto colon = node.lastIndexOf(':');
        if (colon <= 0 || !portNumber.match(node.mid(colon + 1)).hasMatch()) continue;
        const int port = node.mid(colon + 1).toInt();
        if (port <= 0 || port > 65535) continue;
        auto host = node.left(colon);
        const bool bracketed = host.startsWith('[') && host.endsWith(']');
        if (bracketed) host = host.mid(1, host.size() - 2);
        else if (host.contains(':')) continue;
        QHostAddress ip;
        if (ip.setAddress(host)) {
            if (usable(ip)) resolved.last() = endpoint(ip, quint16(port));
        } else if (!bracketed && hostName.match(host).hasMatch()) {
            names.append({host.toLatin1(), quint16(port), resolved.size() - 1});
        }
    }
    auto result = [&] {
        QStringList numeric;
        for (const auto &node : resolved) if (!node.isEmpty() && !numeric.contains(node)) numeric << node;
        return numeric;
    };
    if (names.isEmpty() || !budget.slice() || proxy.type != ProxyType::Socks5 || !proxy.udp || !proxy.remoteDns ||
        proxy.host.trimmed().isEmpty() || proxy.host.contains(QChar(0)) || proxy.port <= 0 || proxy.port > 65535)
        return result();
    const auto user = proxy.user.toUtf8(), password = proxy.password.toUtf8();
    if (user.size() > 255 || password.size() > 255 || (user.isEmpty() && !password.isEmpty())) return result();

    QTcpSocket control;
    control.setProxy(QNetworkProxy::NoProxy);
    control.setReadBufferSize(4096);
    QEventLoop connecting;
    QTimer connectionPoll;
    connectionPoll.setSingleShot(true);
    QObject::connect(&connectionPoll, &QTimer::timeout, &connecting, &QEventLoop::quit);
    QObject::connect(&control, &QTcpSocket::connected, &connecting, &QEventLoop::quit);
    QObject::connect(&control, &QTcpSocket::errorOccurred, &connecting, &QEventLoop::quit);
    // This is the only hostname passed to a local resolver: the explicitly configured proxy.
    control.connectToHost(proxy.host, quint16(proxy.port));
    while (control.state() != QAbstractSocket::ConnectedState) {
        const auto wait = budget.slice();
        if (!wait || control.state() == QAbstractSocket::UnconnectedState) return result();
        // waitForConnected aborts on timeout and can resolve synchronously. A
        // worker-local event loop keeps both asynchronous DNS and connect cancellable.
        connectionPoll.start(wait);
        connecting.exec(QEventLoop::ExcludeUserInputEvents);
    }
    connectionPoll.stop();
    const auto method = user.isEmpty() ? QByteArray::fromHex("0500") : QByteArray::fromHex("0502");
    if (!send(control, QByteArray::fromHex("0501") + method[1], budget) || receive(control, 2, budget) != method)
        return result();
    if (!user.isEmpty()) {
        const auto auth = QByteArray(1, char(1)) + char(user.size()) + user + char(password.size()) + password;
        if (!send(control, auth, budget) || receive(control, 2, budget) != QByteArray::fromHex("0100")) return result();
    }
    if (!send(control, QByteArray::fromHex("05030001000000000000"), budget)) return result();
    const auto response = receive(control, 4, budget);
    if (response.size() != 4 || response.left(3) != QByteArray::fromHex("050000")) return result();
    const int addressSize = response[3] == 1 ? 4 : response[3] == 4 ? 16 : 0;
    if (!addressSize) return result();
    const auto bound = receive(control, addressSize + 2, budget);
    if (bound.size() != addressSize + 2) return result();
    auto relay = address(bound.left(addressSize));
    if (relay == QHostAddress::AnyIPv4 || relay == QHostAddress::AnyIPv6) relay = control.peerAddress();
    const auto relayPort = qFromBigEndian<quint16>(bound.constData() + addressSize);
    if (!usable(relay) || !relayPort) return result();

    QUdpSocket udp;
    udp.setProxy(QNetworkProxy::NoProxy);
    if (!udp.bind(QHostAddress(relay.protocol() == QAbstractSocket::IPv6Protocol ?
            QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4), quint16(0))) return result();
    QHash<QByteArray, Name> pending;
    // Pipeline names under one deadline; a silent node cannot multiply the wait budget.
    const auto nodeId = QUuid::createUuid().toRfc4122() + QUuid::createUuid().toRfc4122().left(4);
    for (const auto &name : names) {
        if (!budget.slice()) break;
        const auto transaction = QUuid::createUuid().toRfc4122();
        const auto ping = "d1:ad2:id20:" + nodeId + "e1:q4:ping1:t16:" + transaction + "1:y1:qe";
        const auto packet = QByteArray::fromHex("00000003") + char(name.host.size()) + name.host +
            char(name.port >> 8) + char(name.port & 0xff) + ping;
        if (udp.writeDatagram(packet, relay, relayPort) == packet.size()) pending.insert(transaction, name);
    }
    while (!pending.isEmpty()) {
        const auto wait = budget.slice();
        if (!wait) break;
        control.waitForReadyRead(0);
        if (control.state() != QAbstractSocket::ConnectedState || control.bytesAvailable()) break;
        if (!udp.hasPendingDatagrams()) {
            udp.waitForReadyRead(wait);
            continue;
        }
        const auto size = udp.pendingDatagramSize();
        const auto packet = udp.receiveDatagram(4096);
        const auto &bytes = packet.data();
        if (size > 4096 || packet.senderAddress() != relay || packet.senderPort() != relayPort || bytes.size() < 10 ||
            bytes.left(3) != QByteArray::fromHex("000000")) continue;
        const int sourceSize = bytes[3] == 1 ? 4 : bytes[3] == 4 ? 16 : 0;
        if (!sourceSize || bytes.size() <= 6 + sourceSize) continue;
        const auto source = address(bytes.mid(4, sourceSize));
        const auto sourcePort = qFromBigEndian<quint16>(bytes.constData() + 4 + sourceSize);
        if (!usable(source) || !sourcePort) continue;
        const auto payload = bytes.mid(6 + sourceSize);
        libtorrent::error_code error;
        const auto message = libtorrent::bdecode(libtorrent::span<char const>(payload.constData(), payload.size()),
                                                error, nullptr, 10, 128);
        if (error || message.type() != libtorrent::bdecode_node::dict_t || message.data_section().size() != payload.size())
            continue;
        const auto transaction = message.dict_find_string_value("t");
        const auto it = pending.find(QByteArray(transaction.data(), transaction.size()));
        if (it == pending.end() || sourcePort != it->port) continue;
        const auto type = message.dict_find_string_value("y");
        const auto failure = message.dict_find_list("e");
        const bool validResponse = type == "r" && bool(message.dict_find_dict("r"));
        const bool validError = type == "e" && failure && failure.list_size() == 2 &&
            failure.list_at(0).type() == libtorrent::bdecode_node::int_t &&
            failure.list_at(1).type() == libtorrent::bdecode_node::string_t;
        // A matching KRPC error also proves the source endpoint (e.g. BEP 42 rejection).
        if (!validResponse && !validError) continue;
        resolved[it->index] = endpoint(source, sourcePort);
        pending.erase(it);
    }
    return result();
}

} // namespace eiskalt::torrent
