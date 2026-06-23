#pragma once

#include <QString>
#include <QStringList>

namespace settings_connection {

enum ProxyUiMode {
    ProxyUiDirect = 0,
    ProxyUiSocks5 = 1,
    ProxyUiShadowsocks = 2
};

enum ShadowsocksTransport {
    ShadowsocksTransportTcpOnly = 0,
    ShadowsocksTransportTcpAndUdp = 1
};

enum ShadowsocksPasswordError {
    ShadowsocksPasswordValid = 0,
    ShadowsocksPasswordEmpty,
    ShadowsocksPasswordInvalidBase64,
    ShadowsocksPasswordWrongLength
};

struct ShadowsocksPasswordValidation {
    ShadowsocksPasswordError error = ShadowsocksPasswordValid;
    int expectedBytes = 0;
    int segment = 0;

    bool isValid() const { return error == ShadowsocksPasswordValid; }
};

struct ProxyUiState {
    QString server;
    QString port;
    QString user;
    QString password;
    QString method;
    bool useTls = false;
    int shadowsocksTransport = ShadowsocksTransportTcpOnly;
};

QStringList bindAddressOptions(const QString& defaultAddress,
                               const QStringList& discoveredAddresses,
                               const QString& currentAddress);

ProxyUiState switchProxyUiState(ProxyUiState& socks,
                                ProxyUiState& shadowsocks,
                                int& currentMode,
                                int selectedMode,
                                const ProxyUiState& visibleState);

bool shadowsocksUsesUdp(int transport);
bool isShadowsocks2022Method(const QString& method);
ShadowsocksPasswordValidation validateShadowsocksPassword(const QString& method,
                                                          const QString& password);

}
