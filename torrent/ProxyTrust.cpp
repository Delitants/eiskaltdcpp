#include "ProxyTrust.h"
#include "dcpp/ProxyTrust.h"

#include <QCoreApplication>

namespace eiskalt::torrent {
namespace {
class ProxyTrustText {
    Q_DECLARE_TR_FUNCTIONS(ProxyTrust)
};

}

bool loadProxyTrust(ProxyConfig &proxy, QString *error) {
    if (error) error->clear();
    proxy.caPem.clear();
    auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    try {
        proxy.caPem = QByteArray::fromStdString(dcpp::loadProxyCaPem(proxy.caFile.toUtf8().toStdString()));
        return true;
    } catch (const dcpp::ProxyTrustError& failure) {
        switch (failure.reason()) {
        case dcpp::ProxyTrustError::Unreadable:
            return fail(ProxyTrustText::tr("Cannot read the Torrent proxy CA certificate file."));
        case dcpp::ProxyTrustError::TooLarge:
            return fail(ProxyTrustText::tr("Torrent proxy CA certificate files must not exceed 1 MiB."));
        case dcpp::ProxyTrustError::Invalid:
            return fail(ProxyTrustText::tr("The Torrent proxy CA file must contain valid PEM CA certificates only."));
        }
    }
    return false;
}

} // namespace eiskalt::torrent
