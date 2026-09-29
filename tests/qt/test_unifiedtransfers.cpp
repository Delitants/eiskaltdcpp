#include <catch2/catch_test_macros.hpp>
#include "TransferViewModel.h"
#include "WulforUtil.h"
#include "torrent/TorrentEngine.h"
#include <QCoreApplication>
#include <QPersistentModelIndex>
#include <QTemporaryDir>
#include <QImage>
#include <QWidget>
#include <QSignalSpy>
#include <memory>

using eiskalt::torrent::Job;
using eiskalt::torrent::TorrentEngine;

namespace {
Job downloading(QString id = "job-a") {
    Job job;
    job.id = id;
    job.name = "same.bin";
    job.savePath = "/tmp/unified-transfers";
    job.state = "Downloading";
    job.size = 1000;
    job.downloaded = 250;
    job.uploaded = 75;
    job.progress = 0.25;
    job.downloadRate = 100;
    job.uploadRate = 20;
    return job;
}

QVariantMap dcTransfer() {
    return {{"CID", "dc-peer"}, {"DOWN", true}, {"BGROUP", true},
            {"TARGET", "/tmp/unified-transfers/same.bin"}, {"FNAME", "same.bin"},
            {"USER", "DC peer"}, {"ESIZE", 1000}, {"SPEED", 10},
            {"STAT", "Downloading"}};
}

// These adapters let the pre-feature model compile so RED is a behavior failure,
// rather than a missing-member compiler error. They call only production APIs.
template<class Model>
void snapshot(Model &model, const QList<Job> &jobs) {
    if constexpr (requires { model.setTorrentJobs(jobs); })
        model.setTorrentJobs(jobs);
    else
        FAIL("TransferViewModel does not consume Torrent snapshots yet");
}

template<class Model>
void attach(Model &model, TorrentEngine *engine) {
    if constexpr (requires { model.setTorrentEngine(engine); })
        model.setTorrentEngine(engine);
    else
        FAIL("TransferViewModel does not subscribe to TorrentEngine yet");
}

template<class Model>
bool dcActions(Model &model, const QModelIndexList &selection) {
    if constexpr (requires { model.dcActionsAllowed(selection); })
        return model.dcActionsAllowed(selection);
    else {
        FAIL("TransferViewModel has no protocol action guard yet");
        return false;
    }
}

template<class Snapshot>
void setProxied(Snapshot &job, bool proxied) {
    if constexpr (requires { job.proxied = proxied; })
        job.proxied = proxied;
    else
        FAIL("Job snapshot does not expose proxy routing yet");
}

template<class Snapshot>
void setPeerCounts(Snapshot &job, int downloadingPeers, int uploadingPeers) {
    if constexpr (requires { job.downloadingPeers = downloadingPeers; job.uploadingPeers = uploadingPeers; }) {
        job.downloadingPeers = downloadingPeers;
        job.uploadingPeers = uploadingPeers;
    } else {
        FAIL("Job snapshot does not expose sampled payload peer counts yet");
    }
}

QModelIndex torrentRow(TransferViewModel &model, const QString &id) {
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row, 0);
        // Public identity stays stable across downloading and seeding.
        if (index.data(Qt::UserRole).toString() ==
            QString("torrent:%1").arg(id))
            return index;
    }
    return {};
}

TransferViewItem *item(const QModelIndex &index) {
    REQUIRE(index.isValid());
    return static_cast<TransferViewItem *>(index.internalPointer());
}

QImage paintStatus(QStyledItemDelegate &delegate, const QModelIndex &index) {
    QWidget widget;
    QStyleOptionViewItem option;
    option.initFrom(&widget);
    option.widget = &widget;
    option.rect = QRect(0, 0, 800, 36);
    option.features = QStyleOptionViewItem::HasDisplay;
    QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    delegate.paint(&painter, option, index);
    painter.end();
    return image;
}
}

TEST_CASE("DC aggregate progress painting does not borrow the only child's segment label", "[qt][unifiedtransfers][transfer-rendering]") {
    TransferViewModel model;
    auto dc = dcTransfer();
    dc["FPOS"] = 600;
    dc["DPOS"] = 100;
    dc["PERC"] = 10.0;
    dc["STAT"] = "Segment downloaded 100 bytes (10%)";
    model.addConnection(dc);
    model.updateTransfer(dc);
    model.updateParents();
    TransferViewItem *group = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &group));
    REQUIRE(group->percent == 70.0);
    REQUIRE(group->childCount() == 1);
    const auto status = model.createIndexForItem(group).siblingAtColumn(COLUMN_TRANSFER_STATS);
    TransferViewDelegate delegate;
    const QImage aggregate = paintStatus(delegate, status);
    group->child(0)->updateColumn(COLUMN_TRANSFER_STATS, "A different segment label (20%)");
    // Only the child label changed: the parent's aggregate bar and text must not.
    REQUIRE(paintStatus(delegate, status) == aggregate);
    group->updateColumn(COLUMN_TRANSFER_STATS, "Finished aggregate (100%)");
    REQUIRE_FALSE(paintStatus(delegate, status) == aggregate);
}

TEST_CASE("DC progress size hint fits the aggregate text actually painted", "[qt][unifiedtransfers][transfer-columns]") {
    TransferViewModel model;
    const auto dc = dcTransfer();
    model.addConnection(dc);
    model.updateTransfer(dc);
    TransferViewItem *group = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &group));
    const QString aggregate = "Downloaded 124.50 GiB (95.0%) from multiple available sources";
    group->updateColumn(COLUMN_TRANSFER_STATS, aggregate);
    group->child(0)->updateColumn(COLUMN_TRANSFER_STATS, "Short child label");
    const auto index = model.createIndexForItem(group).siblingAtColumn(COLUMN_TRANSFER_STATS);
    QWidget view;
    QStyleOptionViewItem option;
    option.initFrom(&view);
    option.font = view.font();
    option.fontMetrics = view.fontMetrics();
    TransferViewDelegate delegate;
    REQUIRE(delegate.sizeHint(option, index).width() >= option.fontMetrics.horizontalAdvance(aggregate) + 8);
}

TEST_CASE("Torrent progress painting retains formatted snapshot status and errors", "[qt][unifiedtransfers][transfer-rendering]") {
    TransferViewModel model;
    auto job = downloading();
    snapshot(model, {job});
    TransferViewDelegate delegate;
    const auto status = torrentRow(model, job.id).siblingAtColumn(COLUMN_TRANSFER_STATS);
    const QImage downloadingImage = paintStatus(delegate, status);
    job.state = "Blocked";
    job.error = "Disk unavailable";
    snapshot(model, {job});
    const auto blocked = torrentRow(model, job.id).siblingAtColumn(COLUMN_TRANSFER_STATS);
    REQUIRE(blocked.data().toString().contains("Disk unavailable"));
    REQUIRE_FALSE(paintStatus(delegate, blocked) == downloadingImage);
}

TEST_CASE("Unified transfers append protocol without shifting DC columns", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    REQUIRE(model.columnCount() == 11);
    REQUIRE(model.headerData(9, Qt::Horizontal).toString() == "Encryption");
    REQUIRE(model.headerData(10, Qt::Horizontal).toString() == "Protocol");
}

TEST_CASE("Torrent Users exposes independent payload peer counts and connected total", "[qt][unifiedtransfers][torrent-peer-counts]") {
    TransferViewModel model;
    auto job = downloading();
    job.peers = 3;
    // The same peer may contribute to both directions, independent of job rates.
    job.downloadRate = job.uploadRate = 0;
    setPeerCounts(job, 3, 2);
    snapshot(model, {job});
    const auto users = torrentRow(model, job.id);
    REQUIRE(users.data().toString() == "D: 3 | U: 2");
    REQUIRE(users.data(Qt::AccessibleTextRole).toString() == "Downloading peers: 3; uploading peers: 2");
    REQUIRE(users.data(Qt::ToolTipRole).toString().contains("Connected peers: 3"));
    REQUIRE_FALSE(users.data(Qt::DecorationRole).isValid());
    REQUIRE(model.rowCount() == 1);
}

TEST_CASE("Torrent Users clears exact zero and stopped counts without inferring activity", "[qt][unifiedtransfers][torrent-peer-counts]") {
    TransferViewModel model;
    auto job = downloading();
    job.peers = 17;
    setPeerCounts(job, 4, 5);
    snapshot(model, {job});
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    SECTION("Zero counts with nonzero job rates") { setPeerCounts(job, 0, 0); }
    SECTION("Paused stale counters") { job.paused = true; }
    SECTION("Blocked stale counters") { job.error = "Disk unavailable"; }
    SECTION("Negative counters") { setPeerCounts(job, -4, -5); }
    snapshot(model, {job});
    REQUIRE(selected.data().toString() == "D: 0 | U: 0");
    REQUIRE(selected.data(Qt::AccessibleTextRole).toString() == "Downloading peers: 0; uploading peers: 0");
    REQUIRE(selected.data(Qt::ToolTipRole).toString().contains("Connected peers: 17"));
    REQUIRE(selected == torrentRow(model, job.id));
}

TEST_CASE("Torrent Users keeps both directions for default idle and seeding snapshots", "[qt][unifiedtransfers][torrent-peer-counts]") {
    TransferViewModel model;
    auto job = downloading();
    QString expected = "D: 0 | U: 0";
    SECTION("Default counts") {}
    SECTION("Downloading only") { setPeerCounts(job, 7, 0); expected = "D: 7 | U: 0"; }
    SECTION("Seeding only") {
        job.complete = true;
        job.state = "Seeding";
        job.downloadRate = 0;
        setPeerCounts(job, 0, 6);
        expected = "D: 0 | U: 6";
    }
    snapshot(model, {job});
    REQUIRE(torrentRow(model, job.id).data().toString() == expected);
    REQUIRE(model.rowCount() == 1);
}

TEST_CASE("Torrent peer-only updates repaint sparsely and preserve the shared row", "[qt][unifiedtransfers][torrent-peer-counts][torrent-refresh]") {
    TransferViewModel model;
    auto job = downloading();
    job.peers = 8;
    setPeerCounts(job, 3, 2);
    snapshot(model, {job, downloading("untouched")});
    model.sort(COLUMN_TRANSFER_FNAME, Qt::AscendingOrder);
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    const auto *original = item(selected);
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removes(&model, &QAbstractItemModel::rowsRemoved);
    QString expected = "D: 3 | U: 2";
    SECTION("Download count only") { setPeerCounts(job, 4, 2); expected = "D: 4 | U: 2"; }
    SECTION("Upload count only") { setPeerCounts(job, 3, 4); expected = "D: 3 | U: 4"; }
    SECTION("Same sum different directions") { setPeerCounts(job, 2, 3); expected = "D: 2 | U: 3"; }
    SECTION("Connected tooltip only") { job.peers = 9; }
    snapshot(model, {job, downloading("untouched")});
    REQUIRE(changes.size() == 1);
    REQUIRE(changes.first().at(0).value<QModelIndex>().row() == selected.row());
    REQUIRE(changes.first().at(1).value<QModelIndex>().row() == selected.row());
    REQUIRE(selected.data().toString() == expected);
    REQUIRE(selected.data(Qt::ToolTipRole).toString().contains(QString("Connected peers: %1").arg(job.peers)));
    for (int i = 0; i < 20; ++i)
        snapshot(model, {job, downloading("untouched")});
    REQUIRE(changes.size() == 1);
    REQUIRE(layouts.isEmpty());
    REQUIRE(inserts.isEmpty());
    REQUIRE(removes.isEmpty());
    REQUIRE(item(selected) == original);
}

TEST_CASE("Torrent Users sorts numeric download then upload counts without losing selection", "[qt][unifiedtransfers][torrent-peer-counts]") {
    TransferViewModel model;
    auto low = downloading("low"), high = downloading("high"), tie = downloading("tie");
    setPeerCounts(low, 2, 3);
    setPeerCounts(high, 10, 0);
    setPeerCounts(tie, 2, 12);
    snapshot(model, {high, tie, low});
    const QPersistentModelIndex selected(torrentRow(model, low.id));
    model.sort(COLUMN_TRANSFER_USERS, Qt::AscendingOrder);
    REQUIRE(selected.row() == 0);
    REQUIRE(torrentRow(model, tie.id).row() == 1);
    REQUIRE(torrentRow(model, high.id).row() == 2);
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    setPeerCounts(low, 2, 4);
    snapshot(model, {high, tie, low});
    REQUIRE(layouts.isEmpty());
    setPeerCounts(low, 11, 4);
    snapshot(model, {high, tie, low});
    REQUIRE(layouts.size() == 1);
    REQUIRE(selected.row() == 2);
    REQUIRE(selected == torrentRow(model, low.id));
    model.sort(COLUMN_TRANSFER_USERS, Qt::DescendingOrder);
    REQUIRE(selected.row() == 0);
}

TEST_CASE("Mixed Users sorting preserves DC names alongside numeric Torrent counts", "[qt][unifiedtransfers][torrent-peer-counts]") {
    TransferViewModel model;
    auto a = dcTransfer(), z = dcTransfer();
    a["USER"] = "Alice";
    z["USER"] = "Zoe";
    z["CID"] = "dc-z";
    z["TARGET"] = "/tmp/unified-transfers/another.bin";
    model.addConnection(z);
    model.addConnection(a);
    auto low = downloading("low"), high = downloading("high");
    setPeerCounts(low, 2, 0);
    setPeerCounts(high, 10, 0);
    snapshot(model, {high, low});
    model.sort(COLUMN_TRANSFER_USERS, Qt::AscendingOrder);
    REQUIRE(model.index(0, 0).data().toString() == "Alice");
    REQUIRE(model.index(1, 0).data().toString() == "Zoe");
    REQUIRE(torrentRow(model, low.id).row() == 2);
    REQUIRE(torrentRow(model, high.id).row() == 3);
    model.sort(COLUMN_TRANSFER_USERS, Qt::DescendingOrder);
    REQUIRE(torrentRow(model, high.id).row() == 0);
    REQUIRE(torrentRow(model, low.id).row() == 1);
    REQUIRE(model.index(2, 0).data().toString() == "Zoe");
    REQUIRE(model.index(3, 0).data().toString() == "Alice");
}

TEST_CASE("Torrent Users paints both colored directions and counts while DC remains native", "[qt][unifiedtransfers][torrent-peer-counts][transfer-rendering]") {
    TransferViewModel model;
    model.addConnection(dcTransfer());
    model.updateTransfer(dcTransfer());
    TransferViewItem *dc = nullptr;
    // A single DC child is hidden; the visible group forwards its Users text.
    REQUIRE(model.findParent(dcTransfer()["TARGET"].toString(), &dc));
    TransferViewDelegate delegate;
    const QPersistentModelIndex dcUsers(model.createIndexForItem(dc).siblingAtColumn(COLUMN_TRANSFER_USERS));
    REQUIRE(dcUsers.isValid());
    REQUIRE(dcUsers.data().toString() == "DC peer");
    const QImage dcBefore = paintStatus(delegate, dcUsers);
    QStyledItemDelegate nativeDelegate;
    REQUIRE(paintStatus(nativeDelegate, dcUsers) == dcBefore);
    auto job = downloading();
    setPeerCounts(job, 12, 34);
    snapshot(model, {job});
    const auto users = torrentRow(model, job.id);
    const QImage both = paintStatus(delegate, users);
    int green = 0, red = 0;
    for (int y = 0; y < both.height(); ++y) {
        for (int x = 0; x < both.width(); ++x) {
            const QColor color = both.pixelColor(x, y);
            green += color.green() > color.red() + 40 && color.green() > color.blue() + 30;
            red += color.red() > color.green() + 40 && color.red() > color.blue() + 40;
        }
    }
    REQUIRE(green > 10);
    REQUIRE(red > 10);
    SECTION("Download number is painted") { setPeerCounts(job, 56, 34); }
    SECTION("Upload number is painted") { setPeerCounts(job, 12, 78); }
    snapshot(model, {job});
    REQUIRE_FALSE(paintStatus(delegate, users) == both);
    REQUIRE(paintStatus(delegate, dcUsers) == dcBefore);
}

TEST_CASE("Torrent Users arrows remain visible at zero and painting respects the cell", "[qt][unifiedtransfers][torrent-peer-counts][transfer-rendering]") {
    TransferViewModel model;
    auto job = downloading();
    snapshot(model, {job});
    const auto users = torrentRow(model, job.id);
    TransferViewDelegate delegate;
    QWidget widget;
    QStyleOptionViewItem option;
    option.initFrom(&widget);
    option.widget = &widget;
    option.font = widget.font();
    option.fontMetrics = widget.fontMetrics();
    const QSize zeroSize = delegate.sizeHint(option, users);
    const QImage zero = paintStatus(delegate, users);
    // Sample the arrow wings, away from either count: down widens below its
    // stem, up widens above it. Reversing either glyph fails these assertions.
    const auto green = [](QColor c) { return c.green() > c.red() + 40 && c.green() > c.blue() + 30; };
    const auto red = [](QColor c) { return c.red() > c.green() + 40 && c.red() > c.blue() + 40; };
    REQUIRE(green(zero.pixelColor(6, 19)));
    REQUIRE_FALSE(green(zero.pixelColor(6, 14)));
    const int upLeft = 4 + 12 + 3 + option.fontMetrics.horizontalAdvance("0") + 10;
    REQUIRE(red(zero.pixelColor(upLeft + 2, 16)));
    REQUIRE_FALSE(red(zero.pixelColor(upLeft + 2, 21)));
    setPeerCounts(job, 12345, 67890);
    snapshot(model, {job});
    REQUIRE(delegate.sizeHint(option, users).width() > zeroSize.width());
    option.rect = QRect(10, 10, 40, 36);
    option.state |= QStyle::State_Selected;
    QImage canvas(100, 60, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::magenta);
    QPainter painter(&canvas);
    delegate.paint(&painter, option, users);
    painter.end();
    for (int y = 0; y < canvas.height(); ++y)
        for (int x = 0; x < canvas.width(); ++x)
            if (!option.rect.contains(x, y))
                REQUIRE(canvas.pixelColor(x, y) == QColor(Qt::magenta));
}

TEST_CASE("Equal filenames never merge Torrent jobs with DC groups", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    const auto dc = dcTransfer();
    model.addConnection(dc);
    model.updateTransfer(dc);
    TransferViewItem *dcParent = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &dcParent));
    snapshot(model, {downloading(), downloading("job-b")});
    REQUIRE(model.rowCount() == 3);
    REQUIRE(dcParent->childCount() == 1);
    REQUIRE(dcParent->data(10).toString() == "DC++");
    REQUIRE(item(torrentRow(model, "job-a"))->data(10).toString() == "Torrent");
    REQUIRE(torrentRow(model, "job-b").isValid());
    TransferViewItem *found = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &found));
    REQUIRE(found == dcParent);
    REQUIRE_FALSE(model.findTransfer("job-a", true, &found));
    model.finishParent({{"TARGET", dc["TARGET"]}});
    REQUIRE(item(torrentRow(model, "job-a"))->percent == 25.0);
    snapshot(model, {});
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.findTransfer("dc-peer", true, &found));
}

TEST_CASE("One Torrent row retains identity progress size and both traffic directions", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    auto job = downloading();
    snapshot(model, {job});
    REQUIRE(model.rowCount() == 1);
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    auto *transfer = item(selected);
    REQUIRE(selected.data(Qt::UserRole).toString() == "torrent:job-a");
    REQUIRE(transfer->data(COLUMN_TRANSFER_SPEED).toLongLong() == 120);
    const auto speed = QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_SPEED).data().toString();
    INFO(speed.toStdString());
    // The Qt harness supplies a locale-based byte formatter, unlike the DC runtime.
    REQUIRE(speed == "D: " + WulforUtil::formatBytes(100) + "/s | U: " + WulforUtil::formatBytes(20) + "/s");
    REQUIRE(transfer->dpos == 250);
    REQUIRE(transfer->percent == 25.0);
    REQUIRE(transfer->data(COLUMN_TRANSFER_SIZE).toLongLong() == 1000);
    const auto status = QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_STATS).data().toString();
    REQUIRE(status.contains("Downloaded " + WulforUtil::formatBytes(250)));
    REQUIRE(status.contains("Uploaded " + WulforUtil::formatBytes(75)));
    REQUIRE(QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_TLEFT).data().toString() == "00:00:08");
    QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removes(&model, &QAbstractItemModel::rowsRemoved);
    job.complete = true;
    job.state = "Seeding";
    job.progress = 1;
    job.downloaded = 1000;
    job.downloadRate = 0;
    job.uploadRate = 0;
    snapshot(model, {job});
    REQUIRE(model.rowCount() == 1);
    REQUIRE(selected == torrentRow(model, job.id));
    REQUIRE(item(selected) == transfer);
    REQUIRE(inserts.isEmpty());
    REQUIRE(removes.isEmpty());
    REQUIRE(transfer->percent == 100.0);
    REQUIRE(transfer->data(COLUMN_TRANSFER_SIZE).toLongLong() == 1000);
    REQUIRE(transfer->data(COLUMN_TRANSFER_SPEED).toLongLong() == 0);
    REQUIRE(QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_STATS).data().toString().contains("Seeding"));
    REQUIRE(QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_TLEFT).data().toString().isEmpty());
}

TEST_CASE("Zero rates and stopped Torrent snapshots do not retain stale traffic", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    auto job = downloading();
    snapshot(model, {job});
    SECTION("Idle") {
        job.downloadRate = 0;
        job.uploadRate = 0;
    }
    SECTION("Paused with stale rates") {
        job.paused = true;
        job.state = "Paused";
    }
    SECTION("Error with stale rates") {
        job.error = "Disk unavailable";
        job.state = "Blocked";
    }
    snapshot(model, {job});
    REQUIRE(model.rowCount() == 1);
    {
        const auto index = torrentRow(model, job.id);
        REQUIRE(item(index)->data(COLUMN_TRANSFER_SPEED).toLongLong() == 0);
        const auto speed = index.siblingAtColumn(COLUMN_TRANSFER_SPEED).data().toString();
        INFO(speed.toStdString());
        REQUIRE(speed == "D: " + WulforUtil::formatBytes(0) + "/s | U: " + WulforUtil::formatBytes(0) + "/s");
        const auto status = index.siblingAtColumn(COLUMN_TRANSFER_STATS).data().toString();
        if (job.paused) REQUIRE(status.contains("Paused"));
        if (!job.error.isEmpty()) REQUIRE(status.contains("Disk unavailable"));
        REQUIRE(index.siblingAtColumn(COLUMN_TRANSFER_TLEFT).data().toString().isEmpty());
    }
}

TEST_CASE("Checking Torrent state survives progress and DC parent refresh", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    auto job = downloading();
    job.state = "Checking";
    job.downloadRate = job.uploadRate = 0;
    snapshot(model, {job});
    model.updateParents();
    REQUIRE(torrentRow(model, job.id).siblingAtColumn(COLUMN_TRANSFER_STATS).data().toString().contains("Checking"));
}

TEST_CASE("Paused Torrent snapshots preserve authoritative engine state", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    auto job = downloading();
    job.paused = true;
    QString expected;
    SECTION("Queued") { expected = "Queued"; job.state = expected; }
    SECTION("Seeding limit") { expected = "Seeding limit"; job.state = expected; job.complete = true; }
    SECTION("Checking") { expected = "Checking"; job.state = expected; }
    SECTION("Empty state falls back to Paused") { expected = "Paused"; job.state.clear(); }
    snapshot(model, {job});
    model.updateParents();
    const auto index = torrentRow(model, job.id);
    REQUIRE(index.isValid());
    const auto status = index.siblingAtColumn(COLUMN_TRANSFER_STATS).data().toString();
    REQUIRE(status.startsWith(expected + " | "));
    REQUIRE(item(index)->torrentPaused);
    REQUIRE(item(index)->data(COLUMN_TRANSFER_SPEED).toLongLong() == 0);
    REQUIRE_FALSE(index.siblingAtColumn(COLUMN_TRANSFER_FLAGS).data().toString().contains("Paused"));
}

TEST_CASE("Torrent flags describe real snapshot fields and refresh without relayout", "[qt][unifiedtransfers][torrent-refresh]") {
    TransferViewModel model;
    auto job = downloading();
    snapshot(model, {job});
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    REQUIRE(selected.isValid());
    const auto flags = [&] { return QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_FLAGS); };
    REQUIRE(flags().data().toString().isEmpty());
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    job.v1 = true;
    SECTION("Private") { job.privateTorrent = true; }
    SECTION("Paused") { job.paused = true; }
    SECTION("Seeding") { job.complete = true; }
    SECTION("Complete but paused") { job.complete = job.paused = true; }
    SECTION("Complete but failed") { job.complete = true; job.error = "Disk unavailable"; }
    SECTION("Proxied") { setProxied(job, true); }
    SECTION("All flags") {
        job.privateTorrent = job.paused = job.complete = true;
        setProxied(job, true);
    }
    snapshot(model, {job});
    REQUIRE(changes.size() == 1);
    const auto display = flags().data().toString();
    const auto tooltip = flags().data(Qt::ToolTipRole).toString();
    REQUIRE(display.contains("[P]") == job.privateTorrent);
    REQUIRE(display.contains("[v1]"));
    REQUIRE_FALSE(display.contains("Paused"));
    REQUIRE_FALSE(display.contains("Seeding"));
    REQUIRE_FALSE(display.contains("Complete"));
    REQUIRE_FALSE(display.contains("Proxied"));
    REQUIRE(tooltip.contains("Private") == job.privateTorrent);
    REQUIRE(tooltip.contains("BEP 3"));
    REQUIRE_FALSE(tooltip.contains("Paused"));
    REQUIRE_FALSE(tooltip.contains("Seeding"));
    REQUIRE_FALSE(tooltip.contains("Complete"));
    REQUIRE_FALSE(tooltip.contains("Proxy"));
    REQUIRE_FALSE(tooltip.contains("Encrypted"));
    REQUIRE(item(selected)->torrentId == job.id);
    REQUIRE(item(selected)->torrentPaused == job.paused);
    snapshot(model, {job});
    REQUIRE(changes.size() == 1);
    snapshot(model, {downloading()});
    REQUIRE(flags().data().toString().isEmpty());
    REQUIRE_FALSE(flags().data(Qt::ToolTipRole).toString().contains("Proxy"));
    REQUIRE(changes.size() == 2);
    REQUIRE(layouts.isEmpty());
    REQUIRE(selected == torrentRow(model, job.id));
}

TEST_CASE("Torrent direction rate changes repaint even when the numeric sum is unchanged", "[qt][unifiedtransfers][torrent-refresh]") {
    TransferViewModel model;
    auto job = downloading();
    // Keep the rounded ETA unchanged as well, so only the rate split can repaint.
    job.downloaded = 999;
    job.progress = 0.999;
    snapshot(model, {job});
    model.sort(COLUMN_TRANSFER_SPEED, Qt::AscendingOrder);
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    REQUIRE(selected.isValid());
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    job.downloadRate = 80;
    job.uploadRate = 40;
    snapshot(model, {job});
    REQUIRE(changes.size() == 1);
    REQUIRE(item(selected)->data(COLUMN_TRANSFER_SPEED).toLongLong() == 120);
    const auto speed = QModelIndex(selected).siblingAtColumn(COLUMN_TRANSFER_SPEED).data().toString();
    INFO(speed.toStdString());
    REQUIRE(speed == "D: " + WulforUtil::formatBytes(80) + "/s | U: " + WulforUtil::formatBytes(40) + "/s");
    REQUIRE(layouts.isEmpty());
}

TEST_CASE("Numeric sorting preserves Torrent row selection identity", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    auto slow = downloading("slow");
    auto fast = downloading("fast");
    slow.downloadRate = 9;
    slow.uploadRate = 150;
    fast.downloadRate = 100;
    snapshot(model, {fast, slow});
    const QPersistentModelIndex selected(torrentRow(model, "slow"));
    model.sort(COLUMN_TRANSFER_SPEED, Qt::AscendingOrder);
    REQUIRE(torrentRow(model, "fast").row() < torrentRow(model, "slow").row());
    REQUIRE(selected == torrentRow(model, "slow"));
    slow.uploadRate = 0;
    snapshot(model, {fast, slow});
    REQUIRE(torrentRow(model, "slow").row() < torrentRow(model, "fast").row());
    REQUIRE(selected == torrentRow(model, "slow"));
    model.sort(10, Qt::DescendingOrder);
    REQUIRE(selected == torrentRow(model, "slow"));
    snapshot(model, {fast});
    REQUIRE_FALSE(selected.isValid());
}

TEST_CASE("Unchanged Torrent snapshots do not invalidate the transfer view", "[qt][unifiedtransfers][torrent-refresh]") {
    TransferViewModel model;
    const auto job = downloading();
    snapshot(model, {job});
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    QSignalSpy dataChanges(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    for (int i = 0; i < 20; ++i)
        snapshot(model, {job});
    CHECK(dataChanges.isEmpty());
    CHECK(layouts.isEmpty());
    CHECK(selected == torrentRow(model, job.id));
}

TEST_CASE("Torrent progress updates only its job row without relayout", "[qt][unifiedtransfers][torrent-refresh]") {
    TransferViewModel model;
    auto job = downloading();
    snapshot(model, {job});
    model.sort(COLUMN_TRANSFER_FNAME, Qt::AscendingOrder);
    const QPersistentModelIndex selected(torrentRow(model, job.id));
    QSignalSpy dataChanges(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    job.downloaded += 10;
    job.progress += 0.01;
    snapshot(model, {job});
    REQUIRE(dataChanges.size() == 1);
    CHECK(dataChanges.first().at(0).value<QModelIndex>().row() == selected.row());
    CHECK(layouts.isEmpty());
    CHECK(item(selected)->dpos == job.downloaded);
}

TEST_CASE("Torrent rate updates relayout only when sorted order changes", "[qt][unifiedtransfers][torrent-refresh]") {
    TransferViewModel model;
    auto slow = downloading("slow"), fast = downloading("fast");
    slow.downloadRate = 100;
    fast.downloadRate = 200;
    snapshot(model, {slow, fast});
    model.sort(COLUMN_TRANSFER_SPEED, Qt::AscendingOrder);
    const QPersistentModelIndex selected(torrentRow(model, slow.id));
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    slow.downloadRate = 110;
    snapshot(model, {slow, fast});
    CHECK(layouts.isEmpty());
    slow.downloadRate = 300;
    snapshot(model, {slow, fast});
    CHECK(layouts.size() == 1);
    CHECK(selected == torrentRow(model, slow.id));
    CHECK(selected.row() > torrentRow(model, fast.id).row());
}

TEST_CASE("DC action guard rejects Torrent and mixed selections", "[qt][unifiedtransfers]") {
    TransferViewModel model;
    model.addConnection(dcTransfer());
    const QPersistentModelIndex dc(model.index(0, 0));
    snapshot(model, {downloading()});
    const auto torrent = torrentRow(model, "job-a");
    REQUIRE(dcActions(model, {dc}));
    REQUIRE_FALSE(dcActions(model, {torrent}));
    REQUIRE_FALSE(dcActions(model, {dc, torrent}));
    REQUIRE_FALSE(dcActions(model, {}));
}

TEST_CASE("Torrent snapshot attachment clears only Torrent rows and ignores old queued updates", "[qt][unifiedtransfers]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    // Unconfigured engines do not start a network session.
    auto oldEngine = std::make_unique<TorrentEngine>(dir.filePath("old"));
    TorrentEngine newEngine(dir.filePath("new"));
    TransferViewModel model;
    model.addConnection(dcTransfer());
    attach(model, oldEngine.get());
    snapshot(model, {downloading("old")});
    oldEngine->changed();
    attach(model, &newEngine);
    snapshot(model, {downloading("new")});
    oldEngine.reset();
    QCoreApplication::processEvents();
    REQUIRE(torrentRow(model, "new").isValid());
    newEngine.changed();
    QCoreApplication::processEvents();
    REQUIRE(model.rowCount() == 1);
    snapshot(model, {downloading()});
    attach(model, nullptr);
    REQUIRE(model.rowCount() == 1);
    auto dying = std::make_unique<TorrentEngine>(dir.filePath("dying"));
    attach(model, dying.get());
    snapshot(model, {downloading()});
    dying.reset();
    QCoreApplication::processEvents();
    REQUIRE(model.rowCount() == 1);
    TransferViewItem *dc = nullptr;
    REQUIRE(model.findTransfer("dc-peer", true, &dc));
}

TEST_CASE("Torrent progress colors distinguish activity states", "[qt][torrent-controls]") {
    TransferViewModel model;
    auto job = downloading();
    job.progress = 1;
    QColor expected(55, 166, 80);
    SECTION("Downloading") {}
    SECTION("Paused") { job.paused = true; expected = QColor(255, 193, 120); }
    SECTION("Seeding") { job.complete = true; expected = QColor(64, 145, 225); }
    snapshot(model, {job});
    TransferViewDelegate delegate;
    const auto painted = paintStatus(delegate, torrentRow(model, job.id).siblingAtColumn(COLUMN_TRANSFER_STATS));
    REQUIRE(painted.pixelColor(10, 8) == expected);
}

TEST_CASE("Stopped torrents leave shared transfers and resume without disturbing other rows", "[qt][unifiedtransfers][torrent-ui-fixes]") {
    TransferViewModel model;
    auto job = downloading();
    auto other = downloading("other");
    model.addConnection(dcTransfer());
    snapshot(model, {job, other});
    const QPersistentModelIndex stoppedRow(torrentRow(model, job.id));
    const QPersistentModelIndex otherRow(torrentRow(model, other.id));
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    job.stopped = true;
    // The explicit stopped bit, not a translated status or paused flag, is authoritative.
    snapshot(model, {job, other});
    REQUIRE_FALSE(torrentRow(model, job.id).isValid());
    REQUIRE_FALSE(stoppedRow.isValid());
    REQUIRE(model.rowCount() == 2);
    REQUIRE(otherRow == torrentRow(model, other.id));
    REQUIRE(removed.size() == 1);
    snapshot(model, {job, other});
    REQUIRE(removed.size() == 1);
    job.stopped = false;
    job.paused = true;
    snapshot(model, {job, other});
    REQUIRE(torrentRow(model, job.id).isValid());
    REQUIRE(model.rowCount() == 3);
    REQUIRE(otherRow == torrentRow(model, other.id));
}

TEST_CASE("Torrent snapshot changes notify contiguous rows once and keep selection", "[qt][unifiedtransfers][transfer-batch]") {
    TransferViewModel model;
    QList<Job> jobs;
    for (int i = 0; i < 100; ++i) {
        auto job = downloading(QString("job-%1").arg(i, 3, 10, QLatin1Char('0')));
        job.name = job.id;
        jobs.append(job);
    }
    snapshot(model, jobs);
    model.sort(COLUMN_TRANSFER_FNAME, Qt::AscendingOrder);
    QPersistentModelIndex selected(torrentRow(model, "job-050"));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layout(&model, &QAbstractItemModel::layoutChanged);
    for (auto &job : jobs) job.uploaded += 10;
    snapshot(model, jobs);
    CHECK(changed.count() == 1);
    REQUIRE_FALSE(changed.isEmpty());
    CHECK(qvariant_cast<QModelIndex>(changed.first()[0]).row() == 0);
    CHECK(qvariant_cast<QModelIndex>(changed.last()[1]).row() == 99);
    CHECK(layout.isEmpty());
    CHECK(selected.data(Qt::UserRole).toString() == "torrent:job-050");
    changed.clear();
    snapshot(model, jobs);
    CHECK(changed.isEmpty());
    CHECK(layout.isEmpty());
    jobs[50].downloadRate = 10000;
    model.sort(COLUMN_TRANSFER_SPEED, Qt::DescendingOrder);
    snapshot(model, jobs);
    CHECK(selected.data(Qt::UserRole).toString() == "torrent:job-050");
    CHECK(selected.row() == 0);
}

TEST_CASE("Idle DC aggregation neither repaints layout nor counts segments twice", "[qt][unifiedtransfers][transfer-batch]") {
    TransferViewModel model;
    auto dc = dcTransfer();
    dc["DPOS"] = 100;
    dc["FPOS"] = 200;
    dc["PERC"] = 10.0;
    model.addConnection(dc);
    model.updateTransfer(dc);
    model.updateParents();
    TransferViewItem *group = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &group));
    REQUIRE(group->dpos == 300);
    QPersistentModelIndex selected(model.createIndexForItem(group));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy layout(&model, &QAbstractItemModel::layoutChanged);
    for (int i = 0; i < 20; ++i) model.updateParents();
    CHECK(group->dpos == 300);
    CHECK(group->percent == 30.0);
    CHECK(layout.isEmpty());
    CHECK(changed.isEmpty());
    CHECK(selected.isValid());
    dc["DPOS"] = 150;
    model.updateTransfer(dc);
    model.updateParents();
    CHECK(group->dpos == 350);
    CHECK(group->percent == 35.0);
    CHECK_FALSE(changed.isEmpty());
    CHECK(layout.isEmpty());
}

TEST_CASE("Sparse Torrent changes keep separate notification ranges across removals", "[qt][unifiedtransfers][transfer-batch]") {
    TransferViewModel model;
    QList<Job> jobs;
    for (int i = 0; i < 6; ++i) {
        auto job = downloading(QString::number(i));
        job.name = job.id;
        jobs.append(job);
    }
    snapshot(model, jobs);
    model.sort(COLUMN_TRANSFER_FNAME, Qt::AscendingOrder);
    QPersistentModelIndex selected(torrentRow(model, "4"));
    jobs[0].stopped = true;
    jobs[1].uploaded++;
    jobs[2].uploaded++;
    jobs[4].uploaded++;
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    snapshot(model, jobs);
    REQUIRE(changed.count() == 2);
    CHECK(qvariant_cast<QModelIndex>(changed[0][0]).row() == 0);
    CHECK(qvariant_cast<QModelIndex>(changed[0][1]).row() == 1);
    CHECK(qvariant_cast<QModelIndex>(changed[1][0]).row() == 3);
    CHECK(qvariant_cast<QModelIndex>(changed[1][1]).row() == 3);
    CHECK(selected.data(Qt::UserRole).toString() == "torrent:4");
    CHECK(selected.row() == 3);
}

TEST_CASE("DC segment changes notify a valid child index without disturbing selection", "[qt][unifiedtransfers][transfer-batch]") {
    TransferViewModel model;
    auto dc = dcTransfer();
    dc["FPOS"] = 0;
    model.addConnection(dc);
    model.updateTransfer(dc);
    TransferViewItem* peer = nullptr;
    REQUIRE(model.findTransfer("dc-peer", true, &peer));
    QPersistentModelIndex selected(model.createIndexForItem(peer));
    REQUIRE(selected.isValid());
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    dc["DPOS"] = 150;
    model.updateTransfer(dc);
    REQUIRE(changed.count() == 2); // Hidden child and its visible group.
    const auto index = qvariant_cast<QModelIndex>(changed[0][0]);
    CHECK(index.isValid());
    CHECK(index.parent().isValid());
    CHECK(index.internalPointer() == peer);
    CHECK(selected.internalPointer() == peer);
    changed.clear();
    model.updateTransfer(dc);
    CHECK(changed.isEmpty());
}

TEST_CASE("Retained DC bytes survive failure retry and another peer's updates", "[qt][unifiedtransfers][transfer-batch-review]") {
    TransferViewModel model;
    auto dc = dcTransfer();
    dc["FPOS"] = 200;
    dc["DPOS"] = 100;
    model.addConnection(dc);
    model.updateTransfer(dc);
    auto other = dc;
    other["CID"] = "second-peer";
    other["DPOS"] = 50;
    model.addConnection(other);
    model.updateTransfer(other);
    model.updateParents();
    TransferViewItem *group = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &group));
    CHECK(group->dpos == 350);
    dc["FAIL"] = true;
    dc["SPEED"] = 0;
    model.updateTransfer(dc);
    model.updateTransferPos(dc, 300);
    model.updateParents();
    CHECK(group->dpos == 350);
    model.updateTransfer({{"CID", "dc-peer"}, {"DOWN", true}, {"STAT", "Waiting to retry"}, {"SPEED", 0}});
    model.updateParents();
    CHECK(group->dpos == 350);
    other["FPOS"] = 300;
    other["DPOS"] = 75;
    model.updateTransfer(other);
    model.updateParents();
    CHECK(group->dpos == 375);
    dc["FAIL"] = false;
    dc["FPOS"] = 300;
    dc["DPOS"] = 25;
    dc["SPEED"] = 10;
    model.updateTransfer(dc);
    model.updateParents();
    CHECK(group->dpos == 400);
}

TEST_CASE("Single-peer forwarded cells notify the visible group row", "[qt][unifiedtransfers][transfer-batch-review]") {
    TransferViewModel model;
    auto dc = dcTransfer();
    dc["FPOS"] = dc["DPOS"] = 0;
    model.addConnection(dc);
    model.updateTransfer(dc);
    model.updateParents();
    TransferViewItem *group = nullptr;
    REQUIRE(model.findParent(dc["TARGET"].toString(), &group));
    const auto visible = model.createIndexForItem(group);
    REQUIRE_FALSE(model.hasChildren(visible.siblingAtColumn(0)));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    dc["IP"] = "192.0.2.1";
    dc["FLAGS"] = "[S]";
    model.updateTransfer(dc);
    model.updateParents();
    bool visibleNotified = false;
    for (const auto &signal : changed) {
        const auto start = qvariant_cast<QModelIndex>(signal[0]);
        if (!start.parent().isValid() && start.row() == visible.row())
            visibleNotified = true;
    }
    CHECK(visibleNotified);
    CHECK(visible.siblingAtColumn(COLUMN_TRANSFER_IP).data().toString().contains("192.0.2.1"));
}
