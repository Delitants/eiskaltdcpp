#pragma once

#include <QHostAddress>
#include <QTcpServer>
#include <QUdpSocket>

// Fixture-only ownership; these descriptors cannot be handed to libtorrent.
class LoopbackPortPair {
public:
    bool reserve(quint16 port = 0)
    {
        close();
        for (int attempt = 0; attempt < (port ? 1 : 32); ++attempt) {
            if (!tcp.listen(QHostAddress::LocalHost, port)) return false;
            if (udp.bind(QHostAddress::LocalHost, tcp.serverPort(), QUdpSocket::DontShareAddress)) return true;
            const bool occupied = udp.error() == QAbstractSocket::AddressInUseError;
            close();
            if (port || !occupied) return false;
        }
        return false;
    }

    quint16 serverPort() const { return tcp.serverPort(); }
    void close()
    {
        udp.close();
        tcp.close();
    }

private:
    QTcpServer tcp;
    QUdpSocket udp;
};
