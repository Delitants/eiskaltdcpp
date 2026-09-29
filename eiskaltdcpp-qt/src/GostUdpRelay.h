#pragma once

#ifdef USE_TORRENT
#include "dcpp/Socket.h"
#include <QHostAddress>
#include <memory>

// One association per instance; a stopped helper cannot be restarted.
// Called by an adapter worker, never by the GUI. All UDP/TLS I/O and socket
// destruction stay on the dedicated relay thread.
class GostUdpRelay {
public:
    explicit GostUdpRelay(dcpp::Socket::StreamProxyConfig config);
    ~GostUdpRelay();
    quint16 start(const QHostAddress& localBind, const QHostAddress& expectedSender,
                  quint16 expectedPort, QString* error);
    void requestStop();
    void join();
    bool isRunning() const;
    GostUdpRelay(const GostUdpRelay&) = delete;
    GostUdpRelay& operator=(const GostUdpRelay&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
#endif
