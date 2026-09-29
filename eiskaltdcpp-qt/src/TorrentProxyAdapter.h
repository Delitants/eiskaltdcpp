#pragma once

#ifdef USE_TORRENT
#include "torrent/TorrentTypes.h"
#include <QObject>
#include <memory>

class TorrentProxyAdapter : public QObject {
    Q_OBJECT
public:
    explicit TorrentProxyAdapter(QObject *parent = nullptr);
    ~TorrentProxyAdapter() override;
    bool start(const eiskalt::torrent::ProxyConfig &upstream, QString *error = nullptr);
    void stop();
    eiskalt::torrent::ProxyConfig endpoint() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
#endif
