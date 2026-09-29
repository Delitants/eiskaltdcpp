#pragma once

#ifdef USE_TORRENT
#include <QDialog>
#include <QPointer>
#include <memory>

namespace eiskalt::torrent { class TorrentEngine; class TorrentCreator; }

class TorrentCreateDialog : public QDialog {
    Q_OBJECT
public:
    explicit TorrentCreateDialog(eiskalt::torrent::TorrentEngine *engine, QWidget *parent = nullptr);
    ~TorrentCreateDialog() override;

public slots:
    void reject() override;

signals:
    void created(QString metadataPath, QString sourceParentDirectory);

private:
    void startCreation();
    void updateActions();
    void updatePrivateTrackers(bool privateTorrent);
    void setBusy(bool busy);
    struct Controls;
    std::unique_ptr<Controls> ui;
    QPointer<eiskalt::torrent::TorrentEngine> engine;
    eiskalt::torrent::TorrentCreator *creator;
    bool busy = false;
};
#endif
