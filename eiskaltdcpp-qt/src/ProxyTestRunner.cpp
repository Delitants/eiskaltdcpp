#include "dcpp/stdinc.h"
#include "dcpp/format.h"
#include "ProxyTestRunner.h"
#include "dcpp/GostProtocol.h"
#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkProxy>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <atomic>
#include <chrono>
#include <array>

namespace {
using Config = dcpp::Socket::StreamProxyConfig;
using Status = ProxyTestRunner::Status;
using Check = ProxyTestRunner::CheckResult;
using Targets = ProxyTestRunner::ProbeTargets;
using Clock = std::chrono::steady_clock;
QString text(const char* message) { return ProxyTestRunner::tr(message); }

bool dnsName(QString name) {
    if (name.endsWith(QLatin1Char('.'))) name.chop(1);
    if (name.isEmpty() || name.size() > 253) return false;
    for (const auto& label : name.split(QLatin1Char('.'))) {
        if (label.isEmpty() || label.size() > 63 || label.startsWith(QLatin1Char('-')) ||
            label.endsWith(QLatin1Char('-'))) return false;
        for (const auto c : label) {
            const auto u = c.unicode();
            if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                  (u >= '0' && u <= '9') || u == '-')) return false;
        }
    }
    return true;
}

bool numericTarget(const QString& host) {
    if (host.isEmpty() || host.size() > 45 || host.contains(QLatin1Char('%'))) return false;
    // Reject whitespace and delimiters even if a platform address parser accepts them.
    for (const auto c : host) {
        const auto u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') ||
              (u >= 'A' && u <= 'F') || u == ':' || u == '.')) return false;
    }
    const QHostAddress address(host);
    return !address.isNull() && !address.isMulticast() && address != QHostAddress::AnyIPv4 &&
        address != QHostAddress::AnyIPv6 && address != QHostAddress::Broadcast;
}

QString endpoint(const QString& host, int port) {
    return (host.contains(QLatin1Char(':')) ? QStringLiteral("[%1]").arg(host) : host)
        + QLatin1Char(':') + QString::number(port);
}

QByteArray dnsQuery(QString name) {
    if (name.endsWith(QLatin1Char('.'))) name.chop(1);
    QByteArray query = QByteArray::fromHex("000001000001000000000000");
    for (const auto& label : name.split(QLatin1Char('.'))) {
        query += char(label.size());
        query += label.toLatin1();
    }
    query += QByteArray::fromHex("0000010001");
    const quint16 transaction = quint16(QRandomGenerator::global()->generate());
    query[0] = char(transaction >> 8);
    query[1] = char(transaction);
    return query;
}

QByteArray dnsEnvelope(const Targets& targets) {
    const QHostAddress address(targets.dnsResolver);
    QByteArray bytes(3, '\0');
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        bytes += char(4);
        const auto raw = address.toIPv6Address();
        bytes.append(reinterpret_cast<const char*>(raw.c), 16);
    } else {
        bytes += char(1);
        const auto raw = address.toIPv4Address();
        for (int shift : {24, 16, 8, 0}) bytes += char(raw >> shift);
    }
    bytes += char(targets.dnsPort >> 8);
    bytes += char(targets.dnsPort);
    return bytes;
}

struct Failure {
    Status status;
    QString message;
    int code = -1;
};

struct Budget {
    std::shared_ptr<std::atomic<bool>> cancelled;
    Clock::time_point deadline;
    int left() const {
        if (cancelled->load()) throw Failure{Status::Cancelled, text("Cancelled")};
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (ms <= 0) throw Failure{Status::TimedOut, text("Test deadline exceeded")};
        return int(ms);
    }
    int slice() const { return std::min(40, left()); }
};

QHostAddress resolve(const QString& host, const Budget& budget, bool ipv4Only = false) {
    budget.left();
    QHostAddress numeric(host);
    if (!numeric.isNull()) {
        if (ipv4Only && numeric.protocol() != QAbstractSocket::IPv4Protocol)
            throw Failure{Status::Failed, text("Local SOCKS target resolution requires IPv4")};
        return numeric;
    }
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(20);
    bool complete = false;
    QHostInfo answer;
    const int lookup = QHostInfo::lookupHost(host, &loop, [&](const QHostInfo& value) {
        answer = value;
        complete = true;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (budget.cancelled->load() || Clock::now() >= budget.deadline) loop.quit();
    });
    timer.start();
    loop.exec();
    QHostInfo::abortHostLookup(lookup);
    budget.left();
    if (complete) {
        // Prefer IPv4 for the legacy local-target encoder, never convert IPv6
        // text using inet_addr(). No target DNS lookup occurs in remote mode.
        for (const auto& address : answer.addresses())
            if (address.protocol() == QAbstractSocket::IPv4Protocol) return address;
        if (!ipv4Only && !answer.addresses().isEmpty()) return answer.addresses().first();
    }
    throw Failure{Status::Failed, text("DNS resolution failed for %1").arg(host)};
}

QString rejection(int code) {
    switch (code) {
    case 1: return text("general server failure (server DNS/routing/policy is not distinguishable)");
    case 2: return text("connection not allowed by proxy rules");
    case 3: return text("network unreachable");
    case 4: return text("host unreachable");
    case 5: return text("connection refused");
    case 6: return text("TTL expired");
    case 7: return text("command not supported");
    case 8: return text("address type not supported");
    default: return text("unknown server rejection");
    }
}

Check failed(const Failure& error) { return {error.status, error.message, error.code}; }

Failure gostFailure(const dcpp::SocketException& error, const Budget& budget, bool udp) {
    budget.left();
    using Stage = dcpp::SocketException::ProxyStage;
    switch (error.getProxyStage()) {
    case Stage::Negotiation:
        return {Status::NegotiationFailed, ProxyTestRunner::tr("GOST TLS-AUTH negotiation failed.")};
    case Stage::Certificate:
        return {Status::CertificateFailed, ProxyTestRunner::tr("GOST proxy TLS or certificate verification failed.")};
    case Stage::Authentication:
        return {Status::AuthenticationFailed, ProxyTestRunner::tr("GOST authentication failed.")};
    case Stage::Tunnel:
        if (const auto code = error.getSocksReplyCode())
            return {udp ? Status::TunnelFailed : Status::DestinationRejected,
                (udp ? ProxyTestRunner::tr("GOST UDP-TUN rejected (REP=%1).")
                     : ProxyTestRunner::tr("GOST CONNECT destination rejected (REP=%1).")).arg(int(*code)), int(*code)};
        return {Status::TunnelFailed, ProxyTestRunner::tr("GOST tunnel failed or returned malformed data.")};
    default:
        return {Status::Failed, ProxyTestRunner::tr("GOST proxy connection failed.")};
    }
}

void write(QTcpSocket& socket, const QByteArray& bytes, const Budget& budget) {
    budget.left();
    if (socket.write(bytes) != bytes.size())
        throw Failure{Status::Failed, text("Proxy control write failed")};
    while (socket.bytesToWrite()) {
        socket.waitForBytesWritten(budget.slice());
        if (socket.state() != QAbstractSocket::ConnectedState)
            throw Failure{Status::Failed, text("Proxy closed the control connection")};
    }
}

QByteArray read(QTcpSocket& socket, int size, const Budget& budget) {
    QByteArray bytes;
    while (bytes.size() < size) {
        budget.left();
        bytes += socket.read(size - bytes.size());
        if (bytes.size() == size) break;
        if (socket.state() != QAbstractSocket::ConnectedState)
            throw Failure{Status::Failed, text("Proxy closed an incomplete control reply")};
        socket.waitForReadyRead(budget.slice());
    }
    return bytes;
}

void authenticate(QTcpSocket& socket, const Config& config, const Budget& budget) {
    const bool auth = !config.user.empty() || !config.password.empty();
    write(socket, auth ? QByteArray::fromHex("050102") : QByteArray::fromHex("050100"), budget);
    const auto selected = read(socket, 2, budget);
    if (selected[0] != 5 || static_cast<unsigned char>(selected[1]) != (auth ? 2 : 0))
        throw Failure{Status::AuthenticationFailed, text("Proxy did not accept the offered authentication method")};
    if (!auth) return;
    QByteArray request(1, '\1');
    request += char(config.user.size());
    request += QByteArray::fromStdString(config.user);
    request += char(config.password.size());
    request += QByteArray::fromStdString(config.password);
    write(socket, request, budget);
    if (read(socket, 2, budget) != QByteArray::fromHex("0100"))
        throw Failure{Status::AuthenticationFailed, text("Proxy rejected username/password authentication")};
}

quint16 word(const QByteArray& bytes, int at) {
    return (quint16(static_cast<unsigned char>(bytes[at])) << 8) | static_cast<unsigned char>(bytes[at + 1]);
}

bool validDns(const QByteArray& bytes, const QByteArray& query) {
    if (bytes.size() < query.size() || bytes.left(2) != query.left(2) ||
        (word(bytes, 2) & 0xfa00) != 0x8000 || (word(bytes, 2) & 15) > 3 ||
        word(bytes, 4) != 1 || bytes.mid(12, query.size() - 12) != query.mid(12)) return false;
    int offset = query.size();
    const int records = word(bytes, 6) + word(bytes, 8) + word(bytes, 10);
    for (int i = 0; i < records; ++i) {
        bool nameEnded = false;
        while (offset < bytes.size()) {
            const auto length = static_cast<unsigned char>(bytes[offset++]);
            if (!length) { nameEnded = true; break; }
            if ((length & 0xc0) == 0xc0) {
                if (offset >= bytes.size() || (((length & 0x3f) << 8) |
                    static_cast<unsigned char>(bytes[offset])) >= bytes.size()) return false;
                ++offset;
                nameEnded = true;
                break;
            }
            if (length > 63 || offset + length > bytes.size()) return false;
            offset += length;
        }
        if (!nameEnded || offset + 10 > bytes.size()) return false;
        const int length = word(bytes, offset + 8);
        offset += 10 + length;
        if (offset > bytes.size()) return false;
    }
    return offset == bytes.size();
}

Check gostUdpTest(Config config, const Targets& targets, const Budget& budget) {
    config.cancelled = [budget] { return budget.cancelled->load() || Clock::now() >= budget.deadline; };
    dcpp::Socket socket;
    try {
        socket.gostOpenUdpTunnel(config, budget.left());
        const auto query = dnsQuery(targets.dnsQuery);
        const dcpp::gost::Datagram request{targets.dnsResolver.toStdString(), uint16_t(targets.dnsPort),
            dcpp::ByteVector(query.begin(), query.end())};
        const auto frame = dcpp::gost::encodeTunnel(request);
        socket.writeAll(frame.data(), int(frame.size()), budget.left());
        dcpp::ByteVector input;
        std::array<uint8_t, 4096> chunk{};
        while (true) {
            budget.left();
            const int count = socket.read(chunk.data(), int(std::min(chunk.size(), dcpp::gost::MaxTunnelFrame - input.size())));
            if (count == 0)
                throw Failure{Status::TunnelFailed, ProxyTestRunner::tr("GOST tunnel failed or returned malformed data.")};
            if (count < 0) {
                QThread::msleep(std::min(5, budget.left()));
                continue;
            }
            input.insert(input.end(), chunk.begin(), chunk.begin() + count);
            while (!input.empty()) {
                dcpp::gost::Datagram reply;
                size_t consumed = 0;
                const auto result = dcpp::gost::decodeTunnel(input, reply, consumed);
                if (result == dcpp::gost::DecodeResult::Invalid ||
                    (result == dcpp::gost::DecodeResult::NeedMore && input.size() == dcpp::gost::MaxTunnelFrame))
                    throw Failure{Status::TunnelFailed, ProxyTestRunner::tr("GOST tunnel failed or returned malformed data.")};
                if (result == dcpp::gost::DecodeResult::NeedMore) break;
                if (QHostAddress(QString::fromStdString(reply.host)) == QHostAddress(targets.dnsResolver) &&
                    reply.port == targets.dnsPort && validDns(QByteArray(reinterpret_cast<const char*>(reply.payload.data()),
                        qsizetype(reply.payload.size())), query))
                    return {Status::Success, text("UDP relay verified by a matching DNS reply from %1 for %2")
                        .arg(endpoint(targets.dnsResolver, targets.dnsPort), targets.dnsQuery), 0};
                input.erase(input.begin(), input.begin() + consumed);
            }
        }
    } catch (const dcpp::SocketException& error) {
        throw gostFailure(error, budget, true);
    }
}

Check udpTest(const Config& config, const Targets& targets, const Budget& budget) {
    QTcpSocket control;
    control.setProxy(QNetworkProxy::NoProxy);
    QEventLoop connecting;
    QTimer connectTimer;
    QObject::connect(&control, &QTcpSocket::connected, &connecting, &QEventLoop::quit);
    QObject::connect(&control, &QTcpSocket::errorOccurred, &connecting, &QEventLoop::quit);
    QObject::connect(&connectTimer, &QTimer::timeout, &connecting, [&] {
        if (budget.cancelled->load() || Clock::now() >= budget.deadline) connecting.quit();
    });
    connectTimer.start(20);
    control.connectToHost(QHostAddress(QString::fromStdString(config.connectHost)), quint16(config.port));
    if (control.state() == QAbstractSocket::ConnectingState) connecting.exec();
    connectTimer.stop();
    budget.left();
    if (control.state() != QAbstractSocket::ConnectedState)
        throw Failure{Status::Failed, text("Cannot connect to proxy for UDP ASSOCIATE: %1").arg(control.errorString())};
    authenticate(control, config, budget);
    QUdpSocket udp;
    udp.setProxy(QNetworkProxy::NoProxy);
    const bool ipv6 = control.peerAddress().protocol() == QAbstractSocket::IPv6Protocol;
    if (!udp.bind(ipv6 ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0))
        throw Failure{Status::Failed, text("Cannot bind UDP test socket")};
    QByteArray associate = QByteArray::fromHex(ipv6 ? "05030004" : "05030001");
    associate += QByteArray(ipv6 ? 16 : 4, '\0');
    associate += char(udp.localPort() >> 8);
    associate += char(udp.localPort());
    write(control, associate, budget);
    const auto header = read(control, 4, budget);
    if (header[0] != 5 || header[2] != 0)
        throw Failure{Status::Failed, text("Malformed UDP ASSOCIATE response")};
    const auto code = static_cast<unsigned char>(header[1]);
    if (code)
        throw Failure{code == 7 ? Status::Unsupported : Status::Failed,
            text("UDP ASSOCIATE rejected (SOCKS REP=%1): %2").arg(code).arg(rejection(code)), code};
    QHostAddress relay;
    if (header[3] == 1) {
        const auto address = read(control, 4, budget);
        relay = QHostAddress((quint32(word(address, 0)) << 16) | word(address, 2));
    } else if (header[3] == 4) {
        const auto address = read(control, 16, budget);
        Q_IPV6ADDR raw{};
        std::copy(address.begin(), address.end(), raw.c);
        relay = QHostAddress(raw);
    } else if (header[3] == 3) {
        const int length = static_cast<unsigned char>(read(control, 1, budget)[0]);
        if (!length) throw Failure{Status::Failed, text("Empty UDP relay hostname")};
        relay = resolve(QString::fromLatin1(read(control, length, budget)), budget);
    } else {
        throw Failure{Status::Failed, text("Invalid UDP relay address type")};
    }
    const quint16 port = word(read(control, 2, budget), 0);
    if (relay == QHostAddress::AnyIPv4 || relay == QHostAddress::AnyIPv6) relay = control.peerAddress();
    if (!port || relay.isNull() || relay.isMulticast() || relay == QHostAddress::Broadcast)
        throw Failure{Status::Failed, text("Invalid UDP relay endpoint")};
    if (relay.protocol() != udp.localAddress().protocol()) {
        // The relay family need not match the TCP control peer. Keep the
        // client port already advertised in UDP ASSOCIATE when rebinding.
        const quint16 clientPort = udp.localPort();
        udp.close();
        const auto bindAddress = relay.protocol() == QAbstractSocket::IPv6Protocol
            ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4;
        if (!udp.bind(bindAddress, clientPort))
            throw Failure{Status::Failed, text("Cannot bind the advertised UDP port for the negotiated relay family")};
    }

    // This DNS datagram is sent only to the negotiated proxy relay. There is
    // no direct resolver socket, and association alone is never success.
    const QByteArray query = dnsQuery(targets.dnsQuery);
    const QByteArray envelope = dnsEnvelope(targets);
    const QByteArray packet = envelope + query;
    if (udp.writeDatagram(packet, relay, port) != packet.size())
        throw Failure{Status::Failed, text("Cannot send DNS probe to UDP relay")};
    while (true) {
        budget.left();
        if (control.state() != QAbstractSocket::ConnectedState)
            throw Failure{Status::Failed, text("UDP association control connection closed")};
        if (!udp.hasPendingDatagrams()) udp.waitForReadyRead(budget.slice());
        while (udp.hasPendingDatagrams()) {
            budget.left();
            QByteArray bytes(int(std::min<qint64>(udp.pendingDatagramSize(), 4096)), '\0');
            QHostAddress sender;
            quint16 senderPort{};
            const auto count = udp.readDatagram(bytes.data(), bytes.size(), &sender, &senderPort);
            if (count < 0) continue;
            bytes.resize(int(count));
            if (sender == relay && senderPort == port && bytes.startsWith(envelope) &&
                validDns(bytes.mid(envelope.size()), query))
                return {Status::Success, text("UDP relay verified by a matching DNS reply from %1 for %2")
                    .arg(endpoint(targets.dnsResolver, targets.dnsPort), targets.dnsQuery), 0};
        }
    }
}

Check tcpTest(Config config, const Targets& targets, const Budget& budget) {
    const bool shadow = config.type == Config::Shadowsocks;
    const QString target = targets.tcpHost;
    const auto port = std::to_string(targets.tcpPort);
    const auto destination = endpoint(target, targets.tcpPort);
    const QString dialTarget = config.type == Config::Gost || config.remoteDns ? target : resolve(target, budget, !shadow).toString();
    config.cancelled = [budget] { return budget.cancelled->load() || Clock::now() >= budget.deadline; };
    dcpp::Socket socket;
    try {
        socket.proxyConnect(dialTarget.toStdString(), port, config, budget.left());
        if (shadow) {
            // Shadowsocks has no CONNECT acknowledgement; require an actual
            // target response before reporting transport success.
            const std::string request = "HEAD / HTTP/1.0\r\nHost: " + destination.toStdString()
                + "\r\nConnection: close\r\n\r\n";
            socket.writeAll(request.data(), int(request.size()), budget.left());
            char prefix[5]{};
            if (socket.readAll(prefix, sizeof(prefix), budget.left()) != sizeof(prefix) ||
                std::string(prefix, sizeof(prefix)) != "HTTP/")
                throw Failure{Status::Failed, text("Shadowsocks HTTP target %1 did not return an HTTP response; choose a plain HTTP service, not TLS. Target reachability is not proxy liveness")
                    .arg(destination)};
        }
        budget.left();
        return {Status::Success, shadow ? text("TCP verified by an HTTP response from %1").arg(destination)
            : text("TCP: proxy accepted CONNECT to %1 (not an application download test)").arg(destination), shadow ? -1 : 0};
    } catch (const dcpp::SocketException& error) {
        if (config.type == Config::Gost) throw gostFailure(error, budget, false);
        budget.left();
        if (const auto code = error.getSocksReplyCode())
            throw Failure{Status::DestinationRejected,
                text("Proxy reachable, but CONNECT to %1 was rejected (SOCKS REP=%2): %3")
                    .arg(destination).arg(int(*code)).arg(rejection(*code)), int(*code)};
        const auto message = QString::fromStdString(error.getError());
        if (message.contains(QString::fromUtf8(_("Connection timeout"))) ||
            message.contains(QStringLiteral("timeout"), Qt::CaseInsensitive))
            throw Failure{Status::TimedOut, text("TCP test deadline exceeded")};
        const bool authentication = message.contains(QString::fromUtf8(_("The socks server requires authentication"))) ||
            message.contains(QString::fromUtf8(_("The socks server doesn't support login / password authentication"))) ||
            message.contains(QString::fromUtf8(_("Socks server authentication failed (bad login / password?)")));
        throw Failure{authentication
            ? Status::AuthenticationFailed : Status::Failed, text("TCP: %1").arg(message)};
    }
}
}

struct ProxyTestRunner::State {
    std::shared_ptr<std::atomic<bool>> cancelled = std::make_shared<std::atomic<bool>>(false);
    Result result;
};

ProxyTestRunner::ProxyTestRunner(QObject* parent) : QObject(parent) {
    qRegisterMetaType<Result>();
}

ProxyTestRunner::~ProxyTestRunner() { cancel(); }

bool ProxyTestRunner::start(const Config& snapshot, bool requestUdp, int timeoutMs) {
    return start(snapshot, ProbeTargets(QStringLiteral("example.com"),
        snapshot.type == Config::Shadowsocks ? 80 : 443), requestUdp, timeoutMs);
}

QString ProxyTestRunner::validateTargets(const ProbeTargets& targets, bool requestUdp) {
    if (targets.tcpHost.size() > 253 ||
        !(numericTarget(targets.tcpHost) || dnsName(targets.tcpHost)))
        return text("TCP test host must be a bounded ASCII hostname or IP address, without a URL, path, or whitespace.");
    if (targets.tcpPort < 1 || targets.tcpPort > 65535)
        return text("TCP test port must be between 1 and 65535.");
    if (requestUdp) {
        if (!numericTarget(targets.dnsResolver))
            return text("UDP DNS resolver must be a numeric unicast IPv4 or IPv6 address (no hostname or scope ID).");
        if (targets.dnsPort < 1 || targets.dnsPort > 65535)
            return text("UDP DNS port must be between 1 and 65535.");
        if (!dnsName(targets.dnsQuery))
            return text("UDP DNS query must be an ASCII DNS name with labels of 1-63 characters, not a URL or path.");
    }
    return {};
}

bool ProxyTestRunner::start(const Config& snapshot, const ProbeTargets& targets, bool requestUdp, int timeoutMs) {
    if (state) return false;
    auto job = std::make_shared<State>();
    state = job;
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::clamp(timeoutMs, 1, 30000));
    auto* thread = QThread::create([job, config = snapshot, targets, requestUdp, deadline]() mutable {
        const Budget budget{job->cancelled, deadline};
        const bool gost = config.type == Config::Gost;
        const bool udpSupported = gost || (config.type == Config::Socks5 && !config.tls);
        job->result.udp = requestUdp
            ? Check{udpSupported ? Status::Failed : Status::Unsupported,
                udpSupported ? text("UDP test not completed") : text("UDP test unsupported for SOCKS-over-TLS and Shadowsocks")}
            : Check{Status::NotRequested, text("UDP not requested")};
        try {
            const auto invalid = validateTargets(targets, requestUdp && udpSupported);
            if (!invalid.isEmpty()) throw Failure{Status::Failed, invalid};
            if ((!gost && config.type != Config::Socks5 && config.type != Config::Shadowsocks) ||
                config.host.empty() || config.port < 1 || config.port > 65535 ||
                ((gost || config.type == Config::Socks5) && (config.user.size() > 255 || config.password.size() > 255)) ||
                (gost && (config.user.empty() || config.password.empty() || !config.verifyTls ||
                    !(numericTarget(QString::fromStdString(config.host)) || dnsName(QString::fromStdString(config.host))))))
                throw Failure{Status::Failed, text("Invalid proxy endpoint or credentials")};
            config.cancelled = {};
            if (config.connectHost.empty())
                config.connectHost = resolve(QString::fromStdString(config.host), budget).toString().toStdString();
            else if (QHostAddress(QString::fromStdString(config.connectHost)).isNull())
                throw Failure{Status::Failed, text("Proxy dial address must be numeric")};
            // Reserve time for an independent UDP check even if TCP stalls.
            Budget tcpBudget = budget;
            if (requestUdp && udpSupported)
                tcpBudget.deadline = Clock::now() + std::chrono::milliseconds(std::max(1, budget.left() * 2 / 3));
            try { job->result.tcp = tcpTest(config, targets, tcpBudget); }
            catch (const Failure& error) { job->result.tcp = failed(error); }
            if (requestUdp && udpSupported) {
                try { job->result.udp = gost ? gostUdpTest(config, targets, budget) : udpTest(config, targets, budget); }
                catch (const Failure& error) { job->result.udp = failed(error); }
            }
        } catch (const Failure& error) {
            job->result.tcp = failed(error);
            if (requestUdp && udpSupported) job->result.udp = failed(error);
        } catch (const std::exception& error) {
            job->result.tcp = {Status::Failed, gost ? ProxyTestRunner::tr("GOST proxy connection failed.") :
                text("Proxy test failed: %1").arg(QString::fromUtf8(error.what()))};
        }
    });
    connect(thread, &QThread::finished, this, [this, job] {
        state.reset();
        emit finished(job->result);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

void ProxyTestRunner::cancel() {
    if (state) state->cancelled->store(true);
}

bool ProxyTestRunner::isRunning() const { return bool(state); }
