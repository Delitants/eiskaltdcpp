#pragma once

#include "UpdateUrlPolicy.h"
#include <cstddef>
#include <cstdint>
#include <QObject>
#include <functional>
#include <memory>
#include <vector>

namespace dcpp { class DCContext; struct HttpRequestPolicy; struct ProxyRouteSnapshot; }
namespace updates {
namespace transport_detail {
using Resolver = std::function<std::vector<std::string>(const std::string&, uint32_t, const std::function<bool()>&)>;
using Sink = std::function<void(const uint8_t*, size_t)>;
using Backend = std::function<void(const dcpp::HttpRequestPolicy&, const std::string&, const Sink&)>;
dcpp::HttpRequestPolicy policy(ResourceKind, std::shared_ptr<const dcpp::ProxyRouteSnapshot>, Resolver = {});
}

struct UpdateTransportTestAccess;
class UpdateTransport final : public QObject {
    Q_OBJECT
public:
    explicit UpdateTransport(dcpp::DCContext&, QObject* parent = nullptr);
    ~UpdateTransport() override;
    quint64 fetch(const QUrl&, ResourceKind, const QString& destination,
        std::shared_ptr<const dcpp::ProxyRouteSnapshot>);
    void cancel(quint64);
    /// Owner-thread terminal fence, not installer consent. Joins without an
    /// event-loop pump; suppresses pending replies and retracts undelivered data.
    /// Completed, delivered files belong to staging and are left untouched.
    [[nodiscard]] bool stopWorkersForQuit();
signals:
    void progress(quint64 id, quint64 received, quint64 total);
    void finished(quint64 id, const QString& path);
    void failed(quint64 id, const QString& redactedMessage);
private:
    friend struct UpdateTransportTestAccess;
    friend class MacUpdateBackend;
    UpdateTransport(dcpp::DCContext&, transport_detail::Backend, QObject*);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
