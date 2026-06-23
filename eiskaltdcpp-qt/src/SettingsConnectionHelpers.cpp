#include "SettingsConnectionHelpers.h"

#include <QByteArray>

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

bool isShadowsocks2022Method(const QString& method)
{
    const QString normalized = method.trimmed().toLower();
    return normalized == QStringLiteral("2022-blake3-aes-128-gcm") ||
        normalized == QStringLiteral("2022-blake3-aes-256-gcm") ||
        normalized == QStringLiteral("2022-blake3-chacha20-poly1305");
}

namespace {

int shadowsocks2022KeySize(const QString& method)
{
    const QString normalized = method.trimmed().toLower();
    if (normalized == QStringLiteral("2022-blake3-aes-128-gcm"))
        return 16;
    if (normalized == QStringLiteral("2022-blake3-aes-256-gcm") ||
            normalized == QStringLiteral("2022-blake3-chacha20-poly1305"))
        return 32;
    return 0;
}

bool isCanonicalBase64(const QByteArray& encoded, QByteArray& decoded)
{
    if (encoded.isEmpty() || encoded.size() % 4 != 0)
        return false;

    int padding = 0;
    bool sawPadding = false;
    for (const char ch : encoded) {
        if (ch == '=') {
            sawPadding = true;
            ++padding;
            if (padding > 2)
                return false;
            continue;
        }
        if (sawPadding)
            return false;
        const bool valid = (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '+' || ch == '/';
        if (!valid)
            return false;
    }

    decoded = QByteArray::fromBase64(encoded);
    return decoded.toBase64() == encoded;
}

}

ShadowsocksPasswordValidation validateShadowsocksPassword(const QString& method,
                                                          const QString& password)
{
    ShadowsocksPasswordValidation result;
    result.expectedBytes = shadowsocks2022KeySize(method);
    if (result.expectedBytes == 0)
        return result;

    const QStringList segments = password.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    for (int i = 0; i < segments.size(); ++i) {
        result.segment = i + 1;
        const QString segment = segments.at(i);
        if (segment.isEmpty()) {
            result.error = ShadowsocksPasswordEmpty;
            return result;
        }

        QByteArray decoded;
        if (!isCanonicalBase64(segment.toLatin1(), decoded)) {
            result.error = ShadowsocksPasswordInvalidBase64;
            return result;
        }
        if (decoded.size() != result.expectedBytes) {
            result.error = ShadowsocksPasswordWrongLength;
            return result;
        }
    }

    result.segment = 0;
    return result;
}

}
