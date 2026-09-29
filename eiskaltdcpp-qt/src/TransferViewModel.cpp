/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#include "TransferViewModel.h"
#include "QtContextAware.h"
#include "QtContext.h"

#include "WulforUtil.h"
#ifdef USE_TORRENT
#include "torrent/TorrentEngine.h"
#include "TorrentToolbar.h"
#endif

#include <QtWidgets>

#include <QFileInfo>
#include <QList>
#include <QStringList>
#include <QPalette>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QFontMetrics>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QSize>
#include <QStyleOptionProgressBar>
#include <QHash>
#include <QThread>

#include "dcpp/stdinc.h"
#include "dcpp/ShareManager.h"
#include "dcpp/Util.h"

#include <set>
#include <cmath>

#if _DEBUG_QT_UI
#include <QtDebug>
#endif


TransferViewModel::TransferViewModel(QObject *parent)
    : QAbstractItemModel(parent), iconsScaled(false), showTranferedFilesOnly(false)
{
    QList<QVariant> rootData;
    rootData << tr("Users") << tr("Speed") << tr("Status") << tr("Flags") << tr("Size")
             << tr("Time left") << tr("File name") << tr("Host") << tr("IP")
             << tr("Encryption");
#ifdef USE_TORRENT
    rootData << tr("Protocol");
#endif

    rootItem = new TransferViewItem(rootData, nullptr);

    column_map.insert("USER", COLUMN_TRANSFER_USERS);
    column_map.insert("SPEED", COLUMN_TRANSFER_SPEED);
    column_map.insert("STAT", COLUMN_TRANSFER_STATS);
    column_map.insert("FLAGS", COLUMN_TRANSFER_FLAGS);
    column_map.insert("ESIZE", COLUMN_TRANSFER_SIZE);
    column_map.insert("TLEFT", COLUMN_TRANSFER_TLEFT);
    column_map.insert("FNAME", COLUMN_TRANSFER_FNAME);
    column_map.insert("HOST", COLUMN_TRANSFER_HOST);
    column_map.insert("IP", COLUMN_TRANSFER_IP);
    column_map.insert("ENCRYPTION", COLUMN_TRANSFER_ENCRYPTION);

    sortColumn = COLUMN_TRANSFER_SIZE;
    sortOrder = Qt::DescendingOrder;
}

TransferViewModel::~TransferViewModel()
{
#ifdef USE_TORRENT
    disconnect(torrentChangedConnection);
    disconnect(torrentDestroyedConnection);
#endif
    if (rootItem)
        delete rootItem;
}

int TransferViewModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return static_cast<TransferViewItem*>(parent.internalPointer())->columnCount();
    else
        return rootItem->columnCount();
}

QVariant TransferViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this)
        return QVariant();

    TransferViewItem *item = reinterpret_cast<TransferViewItem*>(index.internalPointer());

    switch(role) {
        case Qt::UserRole:
        {
#ifdef USE_TORRENT
            if (item->isTorrent())
                return QString("torrent:%1").arg(item->torrentId);
#endif
            return QString("dc:%1:%2:%3").arg(item->download ? "download" : "upload",
                item->cid.isEmpty() ? "group" : "peer", item->cid.isEmpty() ? item->target : item->cid);
        }
        case Qt::DecorationRole:
        {
#ifdef USE_TORRENT
            // The torrent Users delegate paints both directions together.
            if (item->isTorrent() && index.column() == COLUMN_TRANSFER_USERS)
                break;
#endif
            if (!qtCtx() || !qtCtx()->wulforUtil())
                break;
#ifdef USE_TORRENT
            if (index.column() == COLUMN_TRANSFER_PROTOCOL)
                return item->isTorrent() ? torrent_toolbar::icon().pixmap(16, 16)
                    : qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiCONNECT).scaled(16, 16);
#endif
            if (index.column() != COLUMN_TRANSFER_USERS && index.column() != COLUMN_TRANSFER_FNAME)
                break;

            if (item->download && index.column() == COLUMN_TRANSFER_USERS)
                return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiDOWN).scaled(18, 18, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            else if (index.column() != COLUMN_TRANSFER_FNAME)
                return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiUP).scaled(18, 18, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            else
                return qtCtx()->wulforUtil()->getPixmapForFile(item->data(COLUMN_TRANSFER_FNAME).toString()).scaled(16, 16);
            break;
        }
        case Qt::DisplayRole:
        {
#ifdef USE_TORRENT
            if (item->isTorrent()) {
                if (index.column() == COLUMN_TRANSFER_SPEED)
                    return tr("D: %1/s | U: %2/s").arg(
                        WulforUtil::formatBytes(item->torrentDownloadRate),
                        WulforUtil::formatBytes(item->torrentUploadRate));
                if (index.column() == COLUMN_TRANSFER_TLEFT) {
                    const qint64 seconds = item->data(COLUMN_TRANSFER_TLEFT).toLongLong();
                    if (seconds <= 0)
                        return QString();
                    return QString("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
                        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
                }
            }
#endif
            if (item->download && index.column() != COLUMN_TRANSFER_SIZE && item->childCount() == 1)//This parent item has hidden child, so just copy child column text into parent
                                return data(createIndex(0, index.column(), reinterpret_cast<void*>(item->childItems.first())), role);

            if (index.column() == COLUMN_TRANSFER_SPEED)
                return WulforUtil::formatBytes(item->data(COLUMN_TRANSFER_SPEED).toDouble()) + tr("/s");
            else if (index.column() == COLUMN_TRANSFER_SIZE)
                return WulforUtil::formatBytes(item->data(COLUMN_TRANSFER_SIZE).toLongLong());
            else if (index.column() == COLUMN_TRANSFER_TLEFT){
                int time = item->data(COLUMN_TRANSFER_TLEFT).toInt();

                if (time < 0)
                    return QTime(0, 0, 0).toString("hh:mm:ss");
                else
                    return QTime(0, 0, 0).addSecs(time).toString("hh:mm:ss");
            }

            return item->data(index.column());
        }
        case Qt::AccessibleTextRole:
        {
#ifdef USE_TORRENT
            if (item->isTorrent() && index.column() == COLUMN_TRANSFER_USERS)
                return tr("Downloading peers: %1; uploading peers: %2")
                    .arg(item->torrentDownloadingPeers).arg(item->torrentUploadingPeers);
#endif
            break;
        }
        case Qt::TextAlignmentRole:
        {
            if (index.column() == COLUMN_TRANSFER_SPEED || index.column() == COLUMN_TRANSFER_SIZE)
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            else
                return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
        case Qt::ForegroundRole:
        {
            break;
        }
        case Qt::BackgroundRole:
            break;
        case Qt::ToolTipRole:
        {
#ifdef USE_TORRENT
            if (item->isTorrent()) {
                if (index.column() == COLUMN_TRANSFER_USERS)
                    return tr("Downloading peers: %1; uploading peers: %2\nConnected peers: %3\n"
                              "Active payload rates sampled once per second; a peer may count in both directions.\n"
                              "Sorted by downloading peers, then uploading peers.")
                        .arg(item->torrentDownloadingPeers).arg(item->torrentUploadingPeers)
                        .arg(item->torrentConnectedPeers);
                if (index.column() == COLUMN_TRANSFER_FLAGS)
                    return item->torrentFlagsTooltip;
                if (index.column() == COLUMN_TRANSFER_SPEED)
                    return tr("Download and upload rates; sorted by their combined bytes per second.");
            }
#endif
            if (index.column() == COLUMN_TRANSFER_FNAME)
                return item->target;
            if (index.column() == COLUMN_TRANSFER_STATS)
                return data(index, Qt::DisplayRole);

            break;
        }
    }

    return QVariant();
}

namespace {

template <Qt::SortOrder order>
struct Compare {
    void static sort(unsigned col, QList<TransferViewItem*>& items) {
        std::stable_sort(items.begin(), items.end(), attrs[col]);
    }

    void static insertSorted(unsigned col, QList<TransferViewItem*>& items, TransferViewItem* item) {
        auto it = std::lower_bound(items.begin(), items.end(), item, attrs[col]);
        items.insert(it, item);
    }

    private:
        typedef bool (*AttrComp)(const TransferViewItem * l, const TransferViewItem * r);

        template <int i>
        bool static AttrCmp(const TransferViewItem * l, const TransferViewItem * r) {
            return Cmp(QString::localeAwareCompare(l->data(i).toString(), r->data(i).toString()), 0);
        }
        bool static UsersCmp(const TransferViewItem *l, const TransferViewItem *r) {
#ifdef USE_TORRENT
            // Keep mixed-protocol ordering transitive while retaining DC name sorting.
            if (l->isTorrent() != r->isTorrent())
                return Cmp(l->isTorrent(), r->isTorrent());
            if (l->isTorrent()) {
                if (l->torrentDownloadingPeers != r->torrentDownloadingPeers)
                    return Cmp(l->torrentDownloadingPeers, r->torrentDownloadingPeers);
                return Cmp(l->torrentUploadingPeers, r->torrentUploadingPeers);
            }
#endif
            return AttrCmp<COLUMN_TRANSFER_USERS>(l, r);
        }
        template <int column>
        bool static NumCmp(const TransferViewItem * l, const TransferViewItem * r) {
            return Cmp(l->data(column).toULongLong(), r->data(column).toULongLong());
       }
        template <typename T>
        bool static Cmp(const T& l, const T& r);

        static AttrComp attrs[COLUMN_TRANSFER_COUNT];
};
template <Qt::SortOrder order>
typename Compare<order>::AttrComp Compare<order>::attrs[COLUMN_TRANSFER_COUNT] = {  UsersCmp,
                                                                 NumCmp<COLUMN_TRANSFER_SPEED>,
                                                                 AttrCmp<COLUMN_TRANSFER_STATS>,
                                                                 AttrCmp<COLUMN_TRANSFER_FLAGS>,
                                                                 NumCmp<COLUMN_TRANSFER_SIZE>,
                                                                 NumCmp<COLUMN_TRANSFER_TLEFT>,
                                                                 AttrCmp<COLUMN_TRANSFER_FNAME>,
                                                                 AttrCmp<COLUMN_TRANSFER_HOST>,
                                                                 AttrCmp<COLUMN_TRANSFER_IP>,
                                                                 AttrCmp<COLUMN_TRANSFER_ENCRYPTION>
#ifdef USE_TORRENT
                                                                 , AttrCmp<COLUMN_TRANSFER_PROTOCOL>
#endif
                                                             };

template <> template <typename T>
bool inline Compare<Qt::AscendingOrder>::Cmp(const T& l, const T& r) {
    return l < r;
}

template <> template <typename T>
bool inline Compare<Qt::DescendingOrder>::Cmp(const T& l, const T& r) {
    return l > r;
}
}

QVariant TransferViewModel::headerData(int section, Qt::Orientation orientation,
                               int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return rootItem->data(section);

    return QVariant();
}

QModelIndex TransferViewModel::index(int row, int column, const QModelIndex &parent)
            const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    TransferViewItem *parentItem;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TransferViewItem*>(parent.internalPointer());

    TransferViewItem *childItem = parentItem->child(row);
    if (childItem)
        return createIndex(row, column, childItem);
    else
        return QModelIndex();
}

QModelIndex TransferViewModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    TransferViewItem *childItem = static_cast<TransferViewItem*>(index.internalPointer());
    TransferViewItem *parentItem = childItem->parent();

    if (parentItem == rootItem || !parentItem)
        return QModelIndex();

    if (parentItem != rootItem && !rootItem->childItems.contains(parentItem))
        return QModelIndex();

    return createIndex(parentItem->row(), 0, parentItem);
}

int TransferViewModel::rowCount(const QModelIndex &parent) const
{
    TransferViewItem *parentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        parentItem = rootItem;
    else
        parentItem = static_cast<TransferViewItem*>(parent.internalPointer());

    return parentItem->childCount();
}

bool TransferViewModel::hasChildren(const QModelIndex &parent) const{
    if (!parent.isValid())
        return (rootItem->childCount() > 0);

    TransferViewItem *parentItem = static_cast<TransferViewItem*>(parent.internalPointer());

    if (!parentItem)
        return false;

    if (parentItem->download && parentItem->childCount() == 1)
        return false;

    return (parentItem->childCount() > 0);
}

void TransferViewModel::sort(int column, Qt::SortOrder order) {
    sortColumn = column;
    sortOrder = order;

    if (!rootItem || rootItem->childItems.empty() || column < 0 || column > columnCount()-1)
        return;

    auto sorted = rootItem->childItems;
    if (order == Qt::AscendingOrder)
        Compare<Qt::AscendingOrder>().sort(column, sorted);
    else if (order == Qt::DescendingOrder)
        Compare<Qt::DescendingOrder>().sort(column, sorted);

    if (sorted == rootItem->childItems)
        return;

    emit layoutAboutToBeChanged();
    const QModelIndexList oldIndexes = persistentIndexList();
    rootItem->childItems = std::move(sorted);

    QModelIndexList newIndexes;
    for (const auto &old : oldIndexes) {
        auto *item = static_cast<TransferViewItem *>(old.internalPointer());
        newIndexes.append(createIndex(item->row(), old.column(), item));
    }
    changePersistentIndexList(oldIndexes, newIndexes);
    emit layoutChanged();
}

bool TransferViewModel::dcActionsAllowed(const QModelIndexList &selection) const
{
    if (selection.isEmpty())
        return false;
    for (const auto &index : selection) {
        if (!index.isValid() || index.model() != this)
            return false;
        const auto *item = static_cast<TransferViewItem *>(index.internalPointer());
        if (!item || item->isTorrent())
            return false;
    }
    return true;
}

#ifdef USE_TORRENT
void TransferViewModel::setTorrentEngine(eiskalt::torrent::TorrentEngine *engine)
{
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(!engine || engine->thread() == thread());
    disconnect(torrentChangedConnection);
    disconnect(torrentDestroyedConnection);
    const quint64 attachment = ++torrentAttachment;
    torrentEngine = engine;
    setTorrentJobs({});
    if (!engine)
        return;
    torrentChangedConnection = connect(engine, &eiskalt::torrent::TorrentEngine::changed,
        this, [this, attachment] {
            // A queued notification can survive disconnect/replacement.
            if (attachment == torrentAttachment && torrentEngine)
                setTorrentJobs(torrentEngine->jobs());
        }, Qt::QueuedConnection);
    torrentDestroyedConnection = connect(engine, &QObject::destroyed, this, [this, attachment] {
        if (attachment == torrentAttachment) {
            ++torrentAttachment;
            torrentEngine = nullptr;
            setTorrentJobs({});
        }
    });
    setTorrentJobs(engine->jobs());
}

void TransferViewModel::setTorrentJobs(const QList<eiskalt::torrent::Job> &jobs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QSet<QString> present;
    bool needSort = false;
    QList<int> changedRows;
    for (const auto &job : jobs) {
        if (job.id.isEmpty() || job.stopped)
            continue;
        present.insert(job.id);
    }
    for (int row = rootItem->childCount() - 1; row >= 0; --row) {
        auto *item = rootItem->child(row);
        if (!item->isTorrent() || present.contains(item->torrentId))
            continue;
        beginRemoveRows({}, row, row);
        torrentRows.remove(item->torrentId);
        rootItem->childItems.removeAt(row);
        delete item;
        endRemoveRows();
    }
    for (const auto &job : jobs) {
        if (job.id.isEmpty() || job.stopped)
            continue;
        auto *item = torrentRows.value(job.id, nullptr);
        const bool inserted = !item;
        if (!item) {
            item = new TransferViewItem(QList<QVariant>(COLUMN_TRANSFER_COUNT), rootItem);
            item->torrentId = job.id;
        }
        const bool stopped = job.paused || !job.error.isEmpty();
        const qint64 downloadRate = stopped ? 0 : qMax<qint64>(0, job.downloadRate);
        const qint64 uploadRate = stopped ? 0 : qMax<qint64>(0, job.uploadRate);
        const int downloadingPeers = stopped ? 0 : qMax(0, job.downloadingPeers);
        const int uploadingPeers = stopped ? 0 : qMax(0, job.uploadingPeers);
        const int connectedPeers = qMax(0, job.peers);
        const qint64 size = qMax<qint64>(0, job.size);
        const qint64 bytes = qMax<qint64>(0, job.downloaded);
        const qint64 uploaded = qMax<qint64>(0, job.uploaded);
        const double progress = std::isfinite(job.progress) ? qBound(0.0, job.progress, 1.0) : 0.0;
        QString state = job.state;
        if (state.isEmpty())
            state = job.paused ? tr("Paused") : (job.complete ? tr("Seeding") : tr("Waiting"));
        if (!job.error.isEmpty())
            state += QString(": %1").arg(job.error);
        QStringList flags, flagDetails;
        if (job.v1) {
            flags << QStringLiteral("[v1]");
            flagDetails << tr("v1: SHA-1 piece hashes (BEP 3).");
        }
        if (job.v2) {
            flags << QStringLiteral("[v2]");
            flagDetails << tr("v2: SHA-256 Merkle trees (BEP 52). Both v1 and v2 indicate a hybrid torrent.");
        }
        if (job.privateTorrent) {
            flags << QStringLiteral("[P]");
            flagDetails << tr("Private torrent (BEP 27): tracker-managed peer discovery; DHT and PEX are disabled.");
        }
        const QString flagsTooltip = flagDetails.join(QLatin1Char('\n'));
        QList<QVariant> values(COLUMN_TRANSFER_COUNT);
        values[COLUMN_TRANSFER_USERS] = tr("D: %1 | U: %2").arg(downloadingPeers).arg(uploadingPeers);
        // Preserve the numeric comparator while displaying both directions.
        values[COLUMN_TRANSFER_SPEED] = qulonglong(downloadRate) + qulonglong(uploadRate);
        values[COLUMN_TRANSFER_STATS] = tr("%1 | Downloaded %2 (%3%) | Uploaded %4")
            .arg(state, WulforUtil::formatBytes(bytes)).arg(progress * 100, 0, 'f', 1)
            .arg(WulforUtil::formatBytes(uploaded));
        values[COLUMN_TRANSFER_FLAGS] = flags.join(QStringLiteral(" | "));
        values[COLUMN_TRANSFER_SIZE] = size;
        const qint64 remaining = qMax<qint64>(0, size - qMin(size, bytes));
        values[COLUMN_TRANSFER_TLEFT] = !job.complete && downloadRate > 0
            ? remaining / downloadRate + (remaining % downloadRate != 0) : qint64(0);
        values[COLUMN_TRANSFER_FNAME] = job.name.isEmpty() ? job.id : job.name;
        values[COLUMN_TRANSFER_PROTOCOL] = tr("Torrent");
        const double percent = progress * 100;
        const QString target = QDir(job.savePath).filePath(job.name);
        // Engine snapshots arrive even for idle jobs. Do not make the view
        // repaint and recalculate row/column sizes for identical values.
        if (!inserted && item->itemData == values && item->dpos == bytes &&
            item->percent == percent && item->fail == !job.error.isEmpty() &&
            item->torrentPaused == job.paused && item->target == target &&
            item->download == !job.complete && item->torrentDownloadRate == downloadRate &&
            item->torrentUploadRate == uploadRate && item->torrentFlagsTooltip == flagsTooltip &&
            item->torrentConnectedPeers == connectedPeers)
            continue;
        needSort |= inserted || (sortColumn >= 0 && sortColumn < values.size() &&
                                  item->data(sortColumn) != values.at(sortColumn));
        item->itemData = values;
        item->dpos = bytes;
        item->percent = percent;
        item->fail = !job.error.isEmpty();
        item->torrentPaused = job.paused;
        item->download = !job.complete;
        item->torrentDownloadRate = downloadRate;
        item->torrentUploadRate = uploadRate;
        item->torrentDownloadingPeers = downloadingPeers;
        item->torrentUploadingPeers = uploadingPeers;
        item->torrentConnectedPeers = connectedPeers;
        item->torrentFlagsTooltip = flagsTooltip;
        item->target = target;
        if (inserted) {
            const int row = rootItem->childCount();
            beginInsertRows({}, row, row);
            rootItem->appendChild(item);
            torrentRows.insert(job.id, item);
            endInsertRows();
        } else {
            changedRows.append(item->row());
        }
    }
    notifyRootRows(std::move(changedRows));
    if (needSort)
        sort();
}
#endif

void TransferViewModel::initTransfer(const VarMap &params){
    if (params.empty())
        return;

    TransferViewItem *item, *to;

    if (!findTransfer(vstr(params["CID"]), vbol(params["DOWN"]), &item))
        return;

    bool needParent = (vstr(params["FNAME"]) != tr("File list"));

    if (needParent){
        to = getParent(vstr(params["TARGET"]), params);
        TransferViewItem *p = item->parent();

        if (p == to)
            return;

        moveTransfer(item, p, to);

        if (p != rootItem && !p->childCount() && rootItem->childItems.contains(p)){
            beginRemoveRows(QModelIndex(), p->row(), p->row());
            {
                rootItem->childItems.removeAt(p->row());

                delete p;
            }
            endRemoveRows();
        }

        sort(sortColumn, sortOrder);
    }

    updateTransfer(params);
}

void TransferViewModel::addConnection(const VarMap &params){
    if (params.empty())
        return;

    bool bGroup = false;
    TransferViewItem *i;
    TransferViewItem *to = nullptr;
    bool bDownload = vbol(params["DOWN"]);

    if (findTransfer(vstr(params["CID"]), vbol(params["DOWN"]), &i)) {
        return;
    } else if (bDownload) {
        bGroup = vbol(params["BGROUP"]);
        if (bGroup)
            to = getParent(vstr(params["TARGET"]), params);
    }

    QList<QVariant> data;

    data << params["USER"] << "" << params["STAT"] << "" << "" << "" << params["FNAME"] << params["HOST"] << "" << "";
    TransferViewItem *item = new TransferViewItem(data, (to && bGroup) ? to : rootItem);

    item->download = vbol(params["DOWN"]);
    item->cid = vstr(params["CID"]);

    if (item->download && bGroup)
        item->target = vstr(params["TARGET"]);

    transfer_hash.insert(item->cid, item);

    if (showTranferedFilesOnly){
        if (vstr(params["FNAME"]).isEmpty() || (tr("File list") == params["FNAME"]) ){
            return;
        };
    };


    if (!to)
        rootItem->appendChild(item);
    else
        to->appendChild(item);

    emit layoutChanged();
}

void TransferViewModel::updateTransfer(const VarMap &params){
    if (params.empty())
        return;

    TransferViewItem *item;

    if (!findTransfer(vstr(params["CID"]), vbol(params["DOWN"]), &item))
        return;

    const auto oldData = item->itemData;
    const auto oldPosition = item->dpos;
    const auto oldPercent = item->percent;
    const auto oldFail = item->fail;
    const auto oldTarget = item->target;
    const auto oldTth = item->tth;
    for (auto i = column_map.constBegin(); i != column_map.constEnd(); ++i) {
        if (params.contains(i.key())) {
            item->updateColumn(i.value(), params[i.key()]);
        }
    }

    qlonglong dpos = vlng(params["DPOS"]);
    item->dpos = dpos;
    item->segmentBytes = dpos;
    item->percent = vdbl(params["PERC"]);
    item->target = vstr(params["TARGET"]);
    item->fail = vbol(params["FAIL"]);
    item->tth = vstr(params["TTH"]);



    if (!vbol(params["DOWN"])){

        if (showTranferedFilesOnly){
            if (vstr(params["FNAME"]).isEmpty() || (tr("File list") == params["FNAME"]) ){
                return;
            };
        };

        if (!rootItem->childItems.contains(item)) {
            const int row = rootItem->childCount();
            beginInsertRows({}, row, row);
            rootItem->appendChild(item);
            endInsertRows();
        }
    }

    if (item->parent() != rootItem && rootItem->childItems.contains(item->parent()) && params.contains("FPOS"))
        item->parent()->completedBytes = vlng(params["FPOS"]);

    if (oldData != item->itemData || oldPosition != item->dpos ||
        oldPercent != item->percent || oldFail != item->fail ||
        oldTarget != item->target || oldTth != item->tth) {
        notifyTransferRow(item);
    }
}

void TransferViewModel::removeTransfer(const VarMap &params){
    if (params.empty() || vstr(params["CID"]).isEmpty())
        return;

    auto i = transfer_hash.find(vstr(params["CID"]));

    while (i != transfer_hash.end() && i.key() == vstr(params["CID"])){
        if (i.value()->download == vbol(params["DOWN"])){
            TransferViewItem *item = i.value();
            TransferViewItem *p = item->parent();

            beginRemoveRows(createIndexForItem(p), item->row(), item->row());
            {
                p->childItems.removeAt(item->row());
                delete item;
            }
            endRemoveRows();

            transfer_hash.erase(i);

            if (p != rootItem && !p->childCount()){
                beginRemoveRows(QModelIndex(), p->row(), p->row());
                {
                    rootItem->childItems.removeAt(p->row());

                    delete p;
                }
                endRemoveRows();
            }

            return;
        }

        ++i;
    }
}

bool TransferViewModel::findTransfer(const QString &cid, bool download, TransferViewItem **item){
    if (!item)
        return false;

    auto i = transfer_hash.find(cid);

    while (i != transfer_hash.end() && i.key() == cid && !cid.isEmpty()){
        if (i.value()->download == download){
           *item = i.value();

           return true;
        }

        ++i;
    }

    return false;
}

bool TransferViewModel::findParent(const QString &target, TransferViewItem **item, bool download){
    if (!item)
        return false;

    for (const auto &i : rootItem->childItems){
        if (!i->isTorrent() && (i->download == download) && i->target == target && i->cid.isEmpty()){
            *item = i;

            return true;
        }
    }

    return false;
}

TransferViewItem *TransferViewModel::getParent(const QString &target, const VarMap &params){
    TransferViewItem *p;

    if (findParent(target, &p))
        return p;

    QList<QVariant> data;

    data << params["USER"] << 0 << "" << "" << params["ESIZE"]
         << params["TLEFT"] << params["FNAME"] << params["HOST"] << "" << "";

    p = new TransferViewItem(data, rootItem);
    p->download = true;
    p->target = target;
    p->dpos = vlng(params["FPOS"]);
    p->completedBytes = p->dpos;

    rootItem->appendChild(p);

    return p;
}

void TransferViewModel::moveTransfer(TransferViewItem *item, TransferViewItem *from, TransferViewItem *to){
    if (!(item && from && to) || !from->childItems.contains(item))
        return;

    beginRemoveRows(createIndexForItem(from), item->row(), item->row());
    {
        from->childItems.removeAt(item->row());
    }
    endRemoveRows();

    beginInsertColumns(createIndexForItem(to), to->childCount(), to->childCount());
    {
        to->appendChild(item);
    }
    endInsertColumns();
}

void TransferViewModel::notifyTransferRow(TransferViewItem* item){
    const auto at = createIndexForItem(item);
    if (!at.isValid())
        return;
    emit dataChanged(at.siblingAtColumn(0), at.siblingAtColumn(COLUMN_TRANSFER_COUNT - 1));
    // A single download peer is hidden; its cells are displayed by the group.
    auto *parent = item->parent();
    if (parent != rootItem && parent->download && parent->childCount() == 1)
        notifyRootRows({parent->row()});
}

void TransferViewModel::notifyRootRows(QList<int> rows){
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    int first = rows.first(), last = first;
    for (int i = 1; i < rows.size(); ++i) {
        if (rows[i] == last + 1) {
            last = rows[i];
            continue;
        }
        emit dataChanged(index(first, 0), index(last, COLUMN_TRANSFER_COUNT - 1));
        first = last = rows[i];
    }
    emit dataChanged(index(first, 0), index(last, COLUMN_TRANSFER_COUNT - 1));
}

void TransferViewModel::updateParents(){
    QList<int> changedRows;
    for (int row = 0; row < rootItem->childCount(); ++row) {
        auto *item = rootItem->child(row);
        if (item->childCount() == 0)
            continue;
        const auto oldData = item->itemData;
        const auto oldPosition = item->dpos;
        const auto oldPercent = item->percent;
        updateParent(item);
        if (oldData != item->itemData || oldPosition != item->dpos || oldPercent != item->percent)
            changedRows.append(row);
    }
    notifyRootRows(std::move(changedRows));
}

void TransferViewModel::setShowTranferedFilesOnlyState(bool state){
    showTranferedFilesOnly = state;
};

bool TransferViewModel::getShowTranferedFilesOnlyState(){
    return showTranferedFilesOnly;
};

void TransferViewModel::updateParent(TransferViewItem *p){
    if (!p || p->childCount() < 1 || p == rootItem)
        return;

    QList<QString> hubs;
    int active = 0;
    double speed = 0.0;
    qint64 totalSize = 0;
    // The aggregate is derived, never the starting point for the next tick.
    qlonglong actual = p->completedBytes;
    qint64 timeLeft = 0;
    double progress = 0.0;

    totalSize = vlng(p->data(COLUMN_TRANSFER_SIZE));

    for (const auto &i : p->childItems){
        if (!i->fail){
            active++;
            speed += vdbl(i->data(COLUMN_TRANSFER_SPEED));
        }

        if (!hubs.contains(vstr(i->data(COLUMN_TRANSFER_HOST))))
            hubs.append(vstr(i->data(COLUMN_TRANSFER_HOST)));

        actual += i->segmentBytes;
    }

    if (actual <= vlng(p->data(COLUMN_TRANSFER_SIZE)))
        p->dpos = actual;

    if (totalSize > 0)
        progress = (double)(p->dpos * 100.0) / totalSize;
    if (speed > 0)
        timeLeft = (totalSize - p->dpos) / speed;

    if (active && !p->finished)
        p->updateColumn(COLUMN_TRANSFER_STATS, tr("Downloaded "));
    else if (!p->finished)
        p->updateColumn(COLUMN_TRANSFER_STATS, tr("Waiting for slot "));

    QString stat = vstr(p->data(COLUMN_TRANSFER_STATS)) + WulforUtil::formatBytes(p->dpos)
                   + QString(" (%1%)").arg(progress, 0, 'f', 1);

    QString hubs_str;
    for (const QString &s : hubs)
        hubs_str += s + " ";

    if (vstr(p->data(COLUMN_TRANSFER_FNAME)).startsWith(QString("TTH: "))){
        QString name = vstr(p->data(COLUMN_TRANSFER_FNAME));
        name.remove(0, QString("TTH: ").length());

        p->updateColumn(COLUMN_TRANSFER_FNAME, name);
    }

    p->updateColumn(COLUMN_TRANSFER_USERS, QString("%1/%2").arg(active).arg(p->childCount()));
    p->updateColumn(COLUMN_TRANSFER_FLAGS, "");
    p->updateColumn(COLUMN_TRANSFER_TLEFT, timeLeft);
    p->updateColumn(COLUMN_TRANSFER_HOST, hubs_str);
    p->updateColumn(COLUMN_TRANSFER_SPEED, speed);

    if (!p->finished)
        p->updateColumn(COLUMN_TRANSFER_STATS, stat);

    p->percent = p->finished ? 100.0 : progress;
}

void TransferViewModel::updateTransferPos(const VarMap &params, qint64 pos){
    if (params.empty() || !params.contains("CID"))
        return;

    TransferViewItem *item;

    if (!findTransfer(vstr(params["CID"]), vbol(params["DOWN"]), &item))
        return;

    if (!item->finished){
        auto *parent = item->parent();
        const bool grouped = parent != rootItem;
        if (item->dpos == pos && item->segmentBytes == 0 &&
            (!grouped || parent->completedBytes == pos))
            return;
        item->dpos = pos;
        // Complete/failed events supply queue bytes plus this terminal segment.
        // Move that total to the group base so retry-status updates cannot lose
        // it, and do not count this child's finished segment a second time.
        item->segmentBytes = 0;
        if (grouped) {
            parent->completedBytes = pos;
            updateParent(parent);
            if (parent->childCount() != 1)
                notifyRootRows({parent->row()});
        }

        notifyTransferRow(item);
    }
}

void TransferViewModel::finishParent(const VarMap &params){
    if (params.empty() || !params.contains("TARGET"))
        return;

    QString target = vstr(params["TARGET"]);
    TransferViewItem *p;

    if (!findParent(target, &p))
        return;

    p->updateColumn(COLUMN_TRANSFER_STATS, tr("Finished"));
    p->percent = 100.0;
    p->finished = true;
    p->updateColumn(COLUMN_TRANSFER_SPEED, qlonglong(0));

    for (const auto &i : p->childItems){
        i->updateColumn(COLUMN_TRANSFER_STATS, tr("Finished"));
        i->percent = 100.0;
        i->finished = true;
        i->updateColumn(COLUMN_TRANSFER_SPEED, qlonglong(0));
    }

    emit layoutChanged();
}

int TransferViewModel::getSortColumn() const {
    return sortColumn;
}

void TransferViewModel::setSortColumn(int c) {
    sortColumn = c;
}

Qt::SortOrder TransferViewModel::getSortOrder() const {
    return sortOrder;
}

void TransferViewModel::setSortOrder(Qt::SortOrder o) {
    sortOrder = o;
}

QModelIndex TransferViewModel::createIndexForItem(TransferViewItem *item){
    if (!(rootItem && item && item->parent()))
        return QModelIndex();

    if (item->parent() == rootItem)
        return index(item->row(), COLUMN_TRANSFER_FNAME, QModelIndex());
    else
        return index(item->row(), COLUMN_TRANSFER_FNAME, index(item->parent()->row(), 0, QModelIndex()));
}

void TransferViewModel::clear(){
    beginResetModel();

    qDeleteAll(rootItem->childItems);
    rootItem->childItems.clear();
    transfer_hash.clear();
#ifdef USE_TORRENT
    torrentRows.clear();
#endif

    endResetModel();
}

void TransferViewModel::repaint(){
    emit layoutChanged();
}

TransferViewItem::TransferViewItem(const QList<QVariant> &data, TransferViewItem *parent) :
    download(false),
    fail(false),
    finished(false),
    dpos(0L),
    percent(0.0),
    itemData(data),
    parentItem(parent)
{
#ifdef USE_TORRENT
    if (itemData.size() == COLUMN_TRANSFER_PROTOCOL)
        itemData.append(QStringLiteral("DC++"));
#endif
}

TransferViewItem::~TransferViewItem()
{
    qDeleteAll(childItems);
    childItems.clear();

    parentItem = nullptr;
}

void TransferViewItem::appendChild(TransferViewItem *item) {
    item->parentItem = this;
    childItems.append(item);
}

TransferViewItem *TransferViewItem::child(int row) {
    return childItems.value(row);
}

int TransferViewItem::childCount() const {
    return childItems.count();
}

int TransferViewItem::columnCount() const {
    return itemData.count();
}

QVariant TransferViewItem::data(int column) const {
    return itemData.value(column);
}

TransferViewItem *TransferViewItem::parent() {
    return parentItem;
}

int TransferViewItem::row() const {
    if (parentItem)
        return parentItem->childItems.indexOf(const_cast<TransferViewItem*>(this));

    return -1;
}

void TransferViewItem::updateColumn(int column, QVariant var){
    if (column > (itemData.size()-1))
        return;

    itemData[column] = var;
}

#ifdef USE_TORRENT
namespace {
constexpr int peerArrowSize = 12;
constexpr int peerArrowTextGap = 3;
constexpr int peerDirectionGap = 10;
constexpr int peerCellMargin = 4;

int peerCountsWidth(const TransferViewItem &item, const QFontMetrics &metrics) {
    return 2 * (peerCellMargin + peerArrowSize + peerArrowTextGap) + peerDirectionGap
        + metrics.horizontalAdvance(QString::number(item.torrentDownloadingPeers))
        + metrics.horizontalAdvance(QString::number(item.torrentUploadingPeers));
}
}
#endif

TransferViewDelegate::TransferViewDelegate(QObject *parent):
        QStyledItemDelegate(parent)
{
    if (!qtCtx() || !qtCtx()->settings())
        return;
    download_bar_color = qvariant_cast<QColor>(qtCtx()->settings()->getVar("transferview/download-bar-color", QColor()));
    upload_bar_color = qvariant_cast<QColor>(qtCtx()->settings()->getVar("transferview/upload-bar-color", QColor()));

    connect(qtCtx()->settings(), &WulforSettings::varValueChanged, this, &TransferViewDelegate::wsVarValueChanged);
}

TransferViewDelegate::~TransferViewDelegate(){
}

void TransferViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const{
    TransferViewItem *item = reinterpret_cast<TransferViewItem*>(index.internalPointer());

#ifdef USE_TORRENT
    if (item && item->isTorrent() && index.column() == COLUMN_TRANSFER_USERS) {
        QStyleOptionViewItem cell(option);
        initStyleOption(&cell, index);
        cell.text.clear();
        cell.icon = QIcon();
        cell.features &= ~QStyleOptionViewItem::HasDecoration;
        const auto *style = cell.widget ? cell.widget->style() : QApplication::style();
        painter->save();
        painter->setClipRect(cell.rect, Qt::IntersectClip);
        // Let the native style retain selection, focus and alternating backgrounds.
        style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, cell.widget);
        painter->setFont(cell.font);
        painter->setRenderHint(QPainter::Antialiasing);
        int x = cell.rect.left() + peerCellMargin;
        const qreal y = cell.rect.top() + (cell.rect.height() - peerArrowSize) / 2.0;
        const auto drawDirection = [&](int count, bool down, const QColor &color) {
            const QPolygonF arrow{{4, 0}, {8, 0}, {8, 6}, {12, 6}, {6, 12}, {0, 6}, {4, 6}};
            QTransform transform;
            transform.translate(x, y + (down ? 0 : peerArrowSize));
            transform.scale(1, down ? 1 : -1);
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawPolygon(transform.map(arrow));
            x += peerArrowSize + peerArrowTextGap;
            const QString text = QString::number(count);
            const int width = cell.fontMetrics.horizontalAdvance(text);
            painter->setPen(color);
            painter->drawText(QRect(x, cell.rect.top(), width, cell.rect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, text);
            x += width + peerDirectionGap;
        };
        drawDirection(item->torrentDownloadingPeers, true, QColor(22, 133, 60));
        drawDirection(item->torrentUploadingPeers, false, QColor(205, 45, 45));
        painter->restore();
        return;
    }
#endif

    if (index.column() != COLUMN_TRANSFER_STATS || !item) {
        QStyledItemDelegate::paint(painter, option, index);

        return;
    }

    // DC groups own the aggregate progress shown by this bar. DisplayRole may
    // forward a single child's segment label, which describes different bytes.
    const QString status = item->isTorrent() ? index.data(Qt::DisplayRole).toString()
                                             : item->data(COLUMN_TRANSFER_STATS).toString();
#if defined(USE_PROGRESS_BARS)
    QPalette pal = option.palette;
    if (item->download && download_bar_color.isValid())
        pal.setColor(QPalette::Highlight, download_bar_color);
    else if (!item->download && upload_bar_color.isValid())
        pal.setColor(QPalette::Highlight, upload_bar_color);
#ifdef USE_TORRENT
    if (item->isTorrent())
        pal.setColor(QPalette::Highlight, item->fail ? QColor(210, 65, 65)
                     : item->torrentPaused ? QColor(255, 193, 120)
                     : !item->download ? QColor(64, 145, 225) : QColor(55, 166, 80));
#endif

    const double percent = item->percent;

    QStyleOptionProgressBar progressBarOption;
    if (option.widget)
        progressBarOption.initFrom(option.widget);
    progressBarOption.state = QStyle::State_Enabled;
    progressBarOption.direction = QApplication::layoutDirection();
    progressBarOption.rect = option.rect;
    progressBarOption.fontMetrics = option.fontMetrics;
    progressBarOption.minimum = 0;
    progressBarOption.maximum = 100;
    progressBarOption.textAlignment = Qt::AlignCenter;
    progressBarOption.textVisible = false;
    progressBarOption.palette = pal;
    progressBarOption.progress = static_cast<int>(percent);

    painter->save();

    QRect r = option.rect.adjusted(1, 2, -1, -2);
    if (r.width() < 4 || r.height() < 4) {
        QStyledItemDelegate::paint(painter, option, index);
        painter->restore();
        return;
    }

    const bool selected = option.state & QStyle::State_Selected;

    QColor grooveColor = selected
        ? option.palette.highlight().color().darker(135)
        : option.palette.base().color().darker(125);

    QColor borderColor = selected
        ? option.palette.highlight().color().lighter(130)
        : option.palette.mid().color();

    QColor fillColor = pal.color(QPalette::Highlight);
    if (!fillColor.isValid())
        fillColor = option.palette.highlight().color();

    painter->setRenderHint(QPainter::Antialiasing, false);

    painter->setPen(borderColor);
    painter->setBrush(grooveColor);
    painter->drawRect(r);

    int fillWidth = 0;
    if (percent > 0.0) {
        fillWidth = qMax(1, qMin(r.width() - 2,
            static_cast<int>((r.width() - 2) * (percent / 100.0))));
    }

    if (fillWidth > 0) {
        QRect fillRect(r.left() + 1, r.top() + 1, fillWidth, r.height() - 2);
        painter->fillRect(fillRect, fillColor);
    }

    painter->setPen(selected ? option.palette.highlightedText().color()
                             : option.palette.text().color());
    painter->drawText(option.rect, Qt::AlignCenter, status);

    painter->restore();
#else
    QStyleOptionViewItem plainTextOption = option;
    plainTextOption.text = status;
    plainTextOption.displayAlignment = Qt::AlignCenter;

    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &plainTextOption, painter);
#endif
}

QSize TransferViewDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const{
    QSize sz = QStyledItemDelegate::sizeHint(option, index);
    const auto *item = static_cast<const TransferViewItem *>(index.internalPointer());
#ifdef USE_TORRENT
    if (index.column() == COLUMN_TRANSFER_USERS && item && item->isTorrent()) {
        QStyleOptionViewItem cell(option);
        initStyleOption(&cell, index);
        sz.setWidth(peerCountsWidth(*item, cell.fontMetrics));
        sz.setHeight(qMax(sz.height(), qMax(peerArrowSize, cell.fontMetrics.height()) + 8));
    }
#endif
    if (index.column() == COLUMN_TRANSFER_STATS && item && !item->isTorrent()) {
        const QString status = item->data(COLUMN_TRANSFER_STATS).toString();
        sz.setWidth(qMax(sz.width(), option.fontMetrics.horizontalAdvance(status) + 8));
    }
    sz.setHeight(qMax(sz.height(), option.fontMetrics.height() + 8));
    return sz;
}

void TransferViewDelegate::wsVarValueChanged(const QString &key, const QVariant &val){
    if (key == "transferview/download-bar-color")
        download_bar_color = qvariant_cast<QColor>(val);
    else if (key == "transferview/upload-bar-color")
        upload_bar_color = qvariant_cast<QColor>(val);
}
