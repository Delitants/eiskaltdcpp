#pragma once

#include <QObject>
#include <QUrl>
#include <memory>

namespace dcpp { struct ProxyRouteSnapshot; }
namespace updates {
// GUI-thread local IPC only; deliberately has no remote transport. The backend
// downloads the feed first and verifies the archive before mapping it at quit.
class UpdateLoopbackBridge final : public QObject {
public:
    explicit UpdateLoopbackBridge(QObject* parent = nullptr);
    ~UpdateLoopbackBridge() override;
    QUrl beginSession(const QString& stagedFeed,
        std::shared_ptr<const dcpp::ProxyRouteSnapshot>);
    QUrl mapArchive(const QString& verifiedStagedArchive, qint64 expectedSize);
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
