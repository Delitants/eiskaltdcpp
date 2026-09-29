#pragma once

#ifdef USE_TORRENT
#include "ArenaWidget.h"
#include "torrent/TorrentTypes.h"
#include <QWidget>
#include <QPointer>
#include <QStringList>
#include <memory>

namespace eiskalt::torrent { class TorrentEngine; }
class QCloseEvent;

class TorrentWindow : public QWidget, public ArenaWidget
{
    Q_OBJECT
    Q_INTERFACES(ArenaWidget)
public:
    TorrentWindow(eiskalt::torrent::TorrentEngine *engine, const QString &layoutPath,
                  QWidget *parent = nullptr);
    ~TorrentWindow() override;
    void setManuallySharedDirectories(const QStringList &directories);
    void setSecuritySettings(const eiskalt::torrent::Settings &settings);
    QWidget *getWidget() override { return this; }
    QString getArenaTitle() override { return tr("Torrents"); }
    QString getArenaShortTitle() override { return getArenaTitle(); }
    QMenu *getMenu() override { return nullptr; }
    const QPixmap &getPixmap() override { return tabPixmap; }
    QIcon getIcon() override { return windowIcon(); }
    ArenaWidget::Role role() const override { return ArenaWidget::Torrent; }

public slots:
    void addSource(const QString &fileOrMagnet);
    void createTorrent();
    void selectJob(const QString &id);
    void showError(const QString &message);
    void setSharingStatus(const QString &id, const QString &status);
    void reloadIcons();

signals:
    void settingsRequested();
    void magnetShareRequested(QStringList ids, bool dc);
    void securitySettingsRequested(eiskalt::torrent::EncryptionMode encryption,
                                   QStringList blockedCountries, bool blockUnknownClients);
    // Same-thread direct connection required before remove/delete/recheck/selection.
    void shareInvalidationRequested(const QString &id);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void refreshFiles();
    void refreshPeers();
    void renderPeers(const QString &id, const QList<eiskalt::torrent::Peer> &peers);
    void updateActions();
    void scheduleLayoutSave();
    void saveLayout();
    void updateSecurityActions();
    QStringList checkedCountries() const;
    void removeSelected(bool deleteFiles);
    void recheckSelected();
    void applyFileSelection();
    QString selectedId() const;
    QStringList selectedIds() const;
    struct Controls;
    std::unique_ptr<Controls> ui;
    QPointer<eiskalt::torrent::TorrentEngine> engine;
    QString layoutPath;
    QString fileJobId;
    QStringList manuallySharedDirectories;
    QPixmap tabPixmap;
    bool selectionDirty = false;
    friend struct TorrentWindowTestAccess;
};
#endif
