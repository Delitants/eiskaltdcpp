#pragma once
#include "UpdateFeedVerifier.h"
#include <QObject>
#include <memory>

namespace dcpp { struct ProxyRouteSnapshot; }
namespace updates {
struct MacUpdateDiscoveryTestAccess;
// Information-only Sparkle adapter. No archive mapping, download, extraction,
// installer or install-consent API exists here. Use only on the GUI thread.
class MacUpdateDiscovery final : public QObject {
    Q_OBJECT
public:
    explicit MacUpdateDiscovery(QObject* parent = nullptr);
    ~MacUpdateDiscovery() override;
    bool check(quint64 operation, const QByteArray& signedFeed, Channel,
        std::shared_ptr<const dcpp::ProxyRouteSnapshot>);
    void cancel();
    bool active() const;
signals:
    void available(quint64 operation, updates::SignedUpdate);
    void noUpdate(quint64 operation);
    void failed(quint64 operation);
private:
    friend struct MacUpdateDiscoveryTestAccess;
    friend class MacUpdateBackend;
    MacUpdateDiscovery(const QString& fixtureBundle, QObject* parent);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
Q_DECLARE_METATYPE(updates::SignedUpdate)
