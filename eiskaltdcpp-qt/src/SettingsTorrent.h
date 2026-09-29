#pragma once

#ifdef USE_TORRENT
#include <QPointer>
#include <QWidget>
#include <memory>
#include "torrent/TorrentTypes.h"

namespace eiskalt::torrent { class TorrentEngine; }

class SettingsTorrent : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsTorrent(const QString &settingsPath,
                             const eiskalt::torrent::ProxyConfig &proxy,
                             QWidget *parent = nullptr);
    ~SettingsTorrent() override;

    eiskalt::torrent::Settings settings() const;
    bool save(QString *error = nullptr);
    void setApplicationProxy(const eiskalt::torrent::ProxyConfig &proxy);
    // Observes runtime only; opening preferences never starts networking.
    void setEngine(eiskalt::torrent::TorrentEngine *engine);

signals:
    void saved();

private:
    void updateEnabled();
    void updateListeners();
    void loadProxyControls();
    void storeProxyControls();
    void testProxy();
    void invalidateProxyTest();
    void saveProbeTargets();
    struct Controls;
    std::unique_ptr<Controls> ui;
    QString path;
    eiskalt::torrent::ProxyConfig applicationProxy;
    QPointer<eiskalt::torrent::TorrentEngine> engine;
};
#endif
