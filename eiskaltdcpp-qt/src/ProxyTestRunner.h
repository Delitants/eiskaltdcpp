#pragma once

#include "dcpp/stdinc.h"
#include "dcpp/Socket.h"
#include <QObject>
#include <QString>
#include <memory>
#include <utility>

// All methods are called on the owning (GUI) thread. Network work owns a copy
// of the configuration and never reads or changes application proxy settings.
class ProxyTestRunner : public QObject {
    Q_OBJECT
public:
    enum class Status {
        NotRequested, Success, Failed, Unsupported, Cancelled, TimedOut,
        DestinationRejected, AuthenticationFailed,
        NegotiationFailed, CertificateFailed, TunnelFailed
    };
    Q_ENUM(Status)
    struct CheckResult {
        Status status = Status::NotRequested;
        QString message;
        int socksReply = -1;
    };
    struct Result {
        CheckResult tcp;
        CheckResult udp;
    };

    // Immutable diagnostic choices, independent of stored proxy profiles.
    struct ProbeTargets {
        const QString tcpHost;
        const int tcpPort;
        const QString dnsResolver;
        const int dnsPort;
        const QString dnsQuery;
        ProbeTargets(QString host = QStringLiteral("example.com"), int port = 443,
                     QString resolver = QStringLiteral("1.1.1.1"), int resolverPort = 53,
                     QString query = QStringLiteral("example.com"))
            : tcpHost(std::move(host)), tcpPort(port), dnsResolver(std::move(resolver)),
              dnsPort(resolverPort), dnsQuery(std::move(query)) {}
    };

    explicit ProxyTestRunner(QObject* parent = nullptr);
    ~ProxyTestRunner() override;
    bool start(const dcpp::Socket::StreamProxyConfig& config, bool requestUdp,
               int timeoutMs = 10000);
    bool start(const dcpp::Socket::StreamProxyConfig& config, const ProbeTargets& targets,
               bool requestUdp, int timeoutMs = 10000);
    static QString validateTargets(const ProbeTargets& targets, bool requestUdp);
    void cancel();
    bool isRunning() const;

signals:
    void finished(ProxyTestRunner::Result result);

private:
    struct State;
    std::shared_ptr<State> state;
};

Q_DECLARE_METATYPE(ProxyTestRunner::Result)
