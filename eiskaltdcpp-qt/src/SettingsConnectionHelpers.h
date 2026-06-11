#pragma once

#include <QString>
#include <QStringList>

namespace settings_connection {

enum ProxyUiMode {
    ProxyUiDirect = 0,
    ProxyUiSocks5 = 1,
    ProxyUiShadowsocks = 2
};

struct ProxyUiState {
    QString server;
    QString port;
    QString user;
    QString password;
    QString method;
};

QStringList bindAddressOptions(const QString& defaultAddress,
                               const QStringList& discoveredAddresses,
                               const QString& currentAddress);

ProxyUiState switchProxyUiState(ProxyUiState& socks,
                                ProxyUiState& shadowsocks,
                                int& currentMode,
                                int selectedMode,
                                const ProxyUiState& visibleState);

}
