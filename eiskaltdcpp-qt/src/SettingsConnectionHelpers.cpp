#include "SettingsConnectionHelpers.h"

namespace settings_connection {

QStringList bindAddressOptions(const QString& defaultAddress,
                               const QStringList& discoveredAddresses,
                               const QString& currentAddress)
{
    QStringList result;

    auto addUnique = [&result](const QString& value) {
        const QString address = value.trimmed();
        if (!address.isEmpty() && !result.contains(address))
            result << address;
    };

    addUnique(defaultAddress);
    for (const QString& address : discoveredAddresses)
        addUnique(address);
    addUnique(currentAddress);

    return result;
}

ProxyUiState switchProxyUiState(ProxyUiState& socks,
                                ProxyUiState& shadowsocks,
                                int& currentMode,
                                int selectedMode,
                                const ProxyUiState& visibleState)
{
    if (currentMode == ProxyUiSocks5) {
        socks = visibleState;
    } else if (currentMode == ProxyUiShadowsocks) {
        shadowsocks = visibleState;
    }

    currentMode = selectedMode;

    if (selectedMode == ProxyUiSocks5)
        return socks;
    if (selectedMode == ProxyUiShadowsocks)
        return shadowsocks;

    return visibleState;
}

bool shadowsocksUsesUdp(int transport)
{
    return transport == ShadowsocksTransportTcpAndUdp;
}

}
