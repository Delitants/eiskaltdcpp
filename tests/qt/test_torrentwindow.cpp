#ifdef USE_TORRENT
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "ArenaWidget.h"
#include "TorrentToolbar.h"
#include "TorrentWindow.h"
#include "TorrentCheckDelegate.h"
#include "FileExtensionIcons.h"
#include "MacInputStyle.h"
#if __has_include("TorrentActionMenu.h")
#include "TorrentActionMenu.h"
#endif
#include "torrent/TorrentEngine.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QProgressBar>
#include <QStandardItemModel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTest>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>
#include <QTreeWidget>
#include <functional>

using namespace eiskalt::torrent;

// Exercise the same snapshot renderer as refreshPeers without starting networking.
// The requires branches keep the pre-implementation RED build runnable.
struct TorrentWindowTestAccess {
    template<class Window> static void render(Window &window, const QList<Peer> &peers)
    {
        if constexpr (requires { window.renderPeers(QString{}, peers); })
            window.renderPeers(QStringLiteral("fixture"), peers);
        else
            FAIL("TorrentWindow has no snapshot peer renderer");
    }
};

namespace {
class ApplicationInputStyle {
public:
    explicit ApplicationInputStyle(int theme)
        : savedPalette(qApp->palette()), savedStyle(qApp->styleSheet())
    {
        if (theme < 0) return;
        QPalette palette = savedPalette;
        const bool dark = theme == 1;
        palette.setColor(QPalette::Window, dark ? QColor(40, 40, 40) : QColor(240, 240, 240));
        palette.setColor(QPalette::Base, dark ? QColor(30, 30, 30) : QColor(Qt::white));
        palette.setColor(QPalette::Text, dark ? QColor(242, 242, 242) : QColor(18, 18, 18));
        palette.setColor(QPalette::Highlight, QColor(55, 112, 220));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        if (qEnvironmentVariableIsSet("EISKALT_TORRENT_PRIORITY_SCREENSHOTS")) {
            REQUIRE(mac_input_style::macBundledStyleIcon(dark ? QStringLiteral("combo-arrow-down-light.svg")
                                                            : QStringLiteral("combo-arrow-down-dark.svg")) != "none");
        }
        qApp->setPalette(palette);
        qApp->setStyleSheet(mac_input_style::macInputContrastStyle(palette));
    }
    ~ApplicationInputStyle()
    {
        qApp->setStyleSheet(savedStyle);
        qApp->setPalette(savedPalette);
    }
private:
    QPalette savedPalette;
    QString savedStyle;
};

template<class Window> void setSecurity(Window &window, const Settings &settings)
{
    if constexpr (requires { window.setSecuritySettings(settings); })
        window.setSecuritySettings(settings);
    else
        FAIL("TorrentWindow has no security settings setter");
}

template<class Snapshot> void country(Snapshot &peer, const QString &code)
{
    if constexpr (requires { peer.countryCode = code; })
        peer.countryCode = code;
    else
        FAIL("Peer snapshot has no countryCode");
}

template<class T> T *control(TorrentWindow &tab, const char *name)
{
    auto *result = tab.findChild<T *>(QString::fromLatin1(name));
    REQUIRE(result);
    return result;
}

bool waitFor(const std::function<bool()> &ready, bool processEvents = true)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < 5000) {
        if (processEvents)
            QTest::qWait(10);
        else
            QThread::msleep(10);
    }
    return ready();
}

QString pausedFixture(QTemporaryDir &dir, TorrentEngine &engine)
{
    // Deferred start-paused metadata never creates a libtorrent network session.
    // No trackers, peers, bootstrap nodes, or payload are supplied by this test.
    Settings settings;
    settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = false;
    settings.bootstrapNodes.clear();
    engine.configure(settings, ProxyConfig{});
    QFile metadata(dir.filePath("offline.torrent"));
    REQUIRE(metadata.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArray("d4:infod6:lengthi1e4:name11:payload.bin12:piece lengthi16384e6:pieces20:") +
        QCryptographicHash::hash("x", QCryptographicHash::Sha1) + "ee";
    REQUIRE(metadata.write(bytes) == bytes.size());
    metadata.close();
    const auto id = engine.add(metadata.fileName(), dir.filePath("payload"), {}, true);
    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(waitFor([&] { return engine.files(id).size() == 1; }));
    return id;
}
}

TEST_CASE("Torrent management embeds as a reusable arena tab with the selected icon", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QWidget parent;
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"), &parent);
    REQUIRE_FALSE(tab.isWindow());
    REQUIRE(tab.parentWidget() == &parent);
    REQUIRE_FALSE(tab.testAttribute(Qt::WA_DeleteOnClose));
    auto *arena = dynamic_cast<ArenaWidget *>(&tab);
    REQUIRE(arena);
    REQUIRE(qobject_cast<ArenaWidget *>(&tab) == arena);
    REQUIRE(arena->getWidget() == &tab);
    REQUIRE(arena->getArenaTitle() == "Torrents");
    REQUIRE(arena->getArenaShortTitle() == arena->getArenaTitle());
    REQUIRE(arena->getMenu() == nullptr);
    REQUIRE((arena->state() & ArenaWidget::Singleton) == ArenaWidget::Singleton);
    // Appending the role must not renumber existing persisted arena identities.
    REQUIRE(ArenaWidget::LiveLog == 17);
    REQUIRE(arena->role() == static_cast<ArenaWidget::Role>(18));
    REQUIRE(arena->role() != ArenaWidget::NoRole);
    const auto *pixmap = &arena->getPixmap();
    REQUIRE_FALSE(pixmap->isNull());
    REQUIRE(&arena->getPixmap() == pixmap);
    REQUIRE(pixmap->toImage() == torrent_toolbar::icon().pixmap(QSize(16, 16), pixmap->devicePixelRatio()).toImage());
    REQUIRE(arena->getIcon().pixmap(QSize(64, 64), 2).toImage() ==
            torrent_toolbar::icon().pixmap(QSize(64, 64), 2).toImage());
    auto *toolbar = control<QToolBar>(tab, "torrentToolbar");
    REQUIRE(tab.layout());
    REQUIRE(tab.layout()->indexOf(toolbar) >= 0);
    REQUIRE_FALSE(toolbar->isMovable());
}

TEST_CASE("Torrent tab defaults to management columns and ignores standalone geometry", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("layout.ini");
    QWidget oldWindow;
    oldWindow.resize(333, 222);
    const auto geometry = oldWindow.saveGeometry();
    {
        QSettings saved(path, QSettings::IniFormat);
        saved.setValue("window/geometry", geometry);
    }
    QWidget parent;
    TorrentWindow tab(nullptr, path, &parent);
    REQUIRE(tab.size() != oldWindow.size());
    auto *jobs = control<QTreeWidget>(tab, "torrentJobs");
    REQUIRE(jobs->isColumnHidden(4));
    REQUIRE(jobs->isColumnHidden(5));
    REQUIRE_FALSE(jobs->isColumnHidden(8));
    REQUIRE(control<QLabel>(tab, "torrentNotice")->text().contains("Transfers"));
    tab.close();
    QSettings saved(path, QSettings::IniFormat);
    REQUIRE(saved.value("window/geometry").toByteArray() == geometry);
}

TEST_CASE("Torrent tab migrates legacy headers and retains subsequent layout choices", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("layout.ini");
    {
        QTreeWidget previous;
        previous.setColumnCount(9);
        previous.header()->setStretchLastSection(false);
        previous.setColumnWidth(0, 371);
        previous.setColumnWidth(1, 247);
        previous.header()->moveSection(8, 2);
        QSettings saved(path, QSettings::IniFormat);
        saved.setValue("jobs/header", previous.header()->saveState());
    }
    QWidget parent;
    QByteArray jobsState, filesState, splitterState;
    {
        TorrentWindow tab(nullptr, path, &parent);
        auto *jobs = control<QTreeWidget>(tab, "torrentJobs");
        auto *files = control<QTreeWidget>(tab, "torrentFiles");
        REQUIRE(jobs->columnWidth(0) == 371);
        REQUIRE(jobs->columnWidth(1) == 247);
        REQUIRE(jobs->header()->visualIndex(8) == 2);
        REQUIRE(jobs->isColumnHidden(4));
        REQUIRE(jobs->isColumnHidden(5));
        jobs->setColumnHidden(4, false);
        jobs->setColumnWidth(4, 153);
        files->setColumnWidth(0, 419);
        auto *splitter = tab.findChild<QSplitter *>();
        REQUIRE(splitter);
        splitter->setSizes({360, 120});
        jobsState = jobs->header()->saveState();
        filesState = files->header()->saveState();
        splitterState = splitter->saveState();
        tab.close();
    }
    QSettings saved(path, QSettings::IniFormat);
    REQUIRE_FALSE(saved.contains("window/geometry"));
    REQUIRE(saved.value("window/splitter").toByteArray() == splitterState);
    TorrentWindow reopened(nullptr, path, &parent);
    REQUIRE(control<QTreeWidget>(reopened, "torrentJobs")->header()->saveState() == jobsState);
    REQUIRE(control<QTreeWidget>(reopened, "torrentFiles")->header()->saveState() == filesState);
    REQUIRE(reopened.findChild<QSplitter *>()->saveState() == splitterState);
}

TEST_CASE("Torrent tab closing keeps real jobs and file-selection invalidation working", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    QWidget parent;
    parent.show();
    {
        TorrentWindow tab(&engine, dir.filePath("layout.ini"), &parent);
        QPointer<TorrentWindow> alive(&tab);
        auto *jobs = control<QTreeWidget>(tab, "torrentJobs");
        REQUIRE(jobs->topLevelItemCount() == 1);
        jobs->setCurrentItem(jobs->topLevelItem(0));
        auto *files = control<QTreeWidget>(tab, "torrentFiles");
        REQUIRE(files->topLevelItemCount() == 1);
        REQUIRE_FALSE(control<QAction>(tab, "torrentPause")->isEnabled());
        REQUIRE(control<QAction>(tab, "torrentResume")->isEnabled());
        for (const auto *name : {"torrentAddFile", "torrentAddMagnet", "torrentRemove", "torrentDelete", "torrentRecheck"})
            REQUIRE(control<QAction>(tab, name)->isEnabled());
        QSignalSpy settings(&tab, &TorrentWindow::settingsRequested);
        control<QAction>(tab, "torrentSettings")->trigger();
        REQUIRE(settings.count() == 1);
        tab.show();
        REQUIRE(tab.close());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        REQUIRE(alive);
        REQUIRE(tab.isHidden());
        REQUIRE(engine.jobs().size() == 1);
        REQUIRE(engine.jobs().first().id == id);
        REQUIRE(engine.jobs().first().paused);
        tab.setSharingStatus(id, "Pending hash");
        tab.show();
        REQUIRE_FALSE(tab.isWindow());
        REQUIRE(jobs->topLevelItem(0)->text(8) == "Pending hash");
        files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        auto *apply = control<QPushButton>(tab, "torrentApplyFiles");
        REQUIRE(apply->isEnabled());
        bool invalidatedBeforeSelection = false;
        QObject::connect(&tab, &TorrentWindow::shareInvalidationRequested, &tab, [&](const QString &jobId) {
            invalidatedBeforeSelection = jobId == id && engine.files(id).first().wanted;
        }, Qt::DirectConnection);
        QTimer::singleShot(0, &tab, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Yes)->click();
        });
        apply->click();
        REQUIRE(invalidatedBeforeSelection);
        REQUIRE(waitFor([&] { return !engine.files(id).first().wanted; }));
        REQUIRE_FALSE(apply->isEnabled());
        tab.hide();
        engine.setWantedFiles(id, {0});
        REQUIRE(waitFor([&] { return files->topLevelItem(0)->checkState(0) == Qt::Checked; }));
        REQUIRE(engine.jobs().first().downloaded == 0);
    }
    // Destroying the view must not take ownership of the external engine either.
    engine.setWantedFiles(id, {});
    REQUIRE(waitFor([&] { return !engine.files(id).first().wanted; }));
    REQUIRE(engine.jobs().size() == 1);
    REQUIRE(engine.jobs().first().paused);
    REQUIRE_FALSE(QFile::exists(dir.filePath("payload/payload.bin")));
}

TEST_CASE("Torrent tab retains safe unavailable actions and public error display", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QWidget parent;
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"), &parent);
    for (const auto *name : {"torrentAddFile", "torrentAddMagnet", "torrentPause", "torrentResume", "torrentRemove", "torrentDelete", "torrentRecheck"})
        REQUIRE_FALSE(control<QAction>(tab, name)->isEnabled());
    REQUIRE(control<QAction>(tab, "torrentSettings")->isEnabled());
    tab.showError("Test error <plain text>");
    auto *notice = control<QLabel>(tab, "torrentNotice");
    REQUIRE(notice->text() == "Test error <plain text>");
    REQUIRE(notice->textFormat() == Qt::PlainText);
}

TEST_CASE("Torrent checklist reconciles applied edits when refresh skips an intermediate snapshot", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    QWidget parent;
    TorrentWindow tab(&engine, dir.filePath("layout.ini"), &parent);
    tab.selectJob(id);
    auto *files = control<QTreeWidget>(tab, "torrentFiles");
    REQUIRE(files->topLevelItemCount() == 1);
    REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Checked);
    files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    tab.selectJob(id);
    REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Unchecked);
    REQUIRE(control<QPushButton>(tab, "torrentApplyFiles")->isEnabled());
    QTimer::singleShot(0, &tab, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Yes)->click();
    });
    control<QPushButton>(tab, "torrentApplyFiles")->click();
    // Keep queued GUI refreshes pending while the real worker changes twice.
    // The next render sees the original snapshot, but the checkbox was edited.
    REQUIRE(waitFor([&] { return !engine.files(id).first().wanted; }, false));
    engine.setWantedFiles(id, {0});
    REQUIRE(waitFor([&] { return engine.files(id).first().wanted; }, false));
    REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Unchecked);
    tab.selectJob(id);
    REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Checked);
    REQUIRE_FALSE(control<QPushButton>(tab, "torrentApplyFiles")->isEnabled());
    REQUIRE(engine.jobs().first().paused);
}

TEST_CASE("Torrent details select and focus the exact requested job identity", "[qt][torrent-tab]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto firstId = pausedFixture(dir, engine);
    const auto secondId = engine.add("magnet:?xt=urn:btih:0123456789012345678901234567890123456789",
                                     dir.filePath("second"), {}, true);
    REQUIRE_FALSE(secondId.isEmpty());
    QWidget parent;
    parent.show();
    TorrentWindow tab(&engine, dir.filePath("layout.ini"), &parent);
    tab.show();
    auto *jobs = control<QTreeWidget>(tab, "torrentJobs");
    REQUIRE(waitFor([&] { return jobs->topLevelItemCount() == 2; }));
    // Invoke the public slot so the pre-conversion implementation fails at runtime.
    REQUIRE(QMetaObject::invokeMethod(&tab, "selectJob", Qt::DirectConnection, Q_ARG(QString, secondId)));
    REQUIRE(jobs->selectedItems().size() == 1);
    REQUIRE(jobs->currentItem()->data(0, Qt::UserRole).toString() == secondId);
    REQUIRE(tab.focusWidget() == jobs);
    REQUIRE(QMetaObject::invokeMethod(&tab, "selectJob", Qt::DirectConnection, Q_ARG(QString, firstId)));
    REQUIRE(jobs->selectedItems().size() == 1);
    REQUIRE(jobs->currentItem()->data(0, Qt::UserRole).toString() == firstId);
    REQUIRE(control<QTreeWidget>(tab, "torrentFiles")->topLevelItemCount() == 1);
    REQUIRE(QMetaObject::invokeMethod(&tab, "selectJob", Qt::DirectConnection, Q_ARG(QString, QString("missing-job"))));
    REQUIRE(jobs->currentItem()->data(0, Qt::UserRole).toString() == firstId);
    REQUIRE(engine.jobs().size() == 2);
}

TEST_CASE("Torrent peer details retain tabs widths and pending file edits", "[qt][torrent-tab][torrent-peers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    QWidget parent;
    const auto path = dir.filePath("layout.ini");
    QByteArray peerHeader, splitterState;
    {
        TorrentWindow tab(&engine, path, &parent);
        tab.selectJob(id);
        auto *details = control<QTabWidget>(tab, "torrentDetails");
        auto *files = control<QTreeWidget>(tab, "torrentFiles");
        auto *peers = control<QTreeWidget>(tab, "torrentPeers");
        REQUIRE(details->count() == 3);
        REQUIRE(details->tabText(0) == "Files");
        REQUIRE(details->tabText(1) == "Peers");
        REQUIRE(details->tabText(2) == "Security");
        REQUIRE(details->widget(0)->isAncestorOf(files));
        REQUIRE(details->widget(1)->isAncestorOf(peers));
        REQUIRE(peers->columnCount() == 7);
        REQUIRE(peers->topLevelItemCount() == 0);
        REQUIRE(control<QLabel>(tab, "torrentPeerNotice")->textFormat() == Qt::PlainText);
        files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        details->setCurrentIndex(1);
        peers->setColumnWidth(0, 267);
        peers->setColumnWidth(2, 231);
        peerHeader = peers->header()->saveState();
        tab.selectJob(id);
        REQUIRE(details->currentIndex() == 1);
        REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Unchecked);
        REQUIRE(control<QPushButton>(tab, "torrentApplyFiles")->isEnabled());
        auto *splitter = tab.findChild<QSplitter *>();
        REQUIRE(splitter);
        REQUIRE(splitter->widget(1) == details);
        splitter->setSizes({300, 160});
        splitterState = splitter->saveState();
        tab.close();
    }
    TorrentWindow reopened(&engine, path, &parent);
    REQUIRE(control<QTabWidget>(reopened, "torrentDetails")->currentIndex() == 1);
    REQUIRE(control<QTreeWidget>(reopened, "torrentPeers")->header()->saveState() == peerHeader);
    REQUIRE(reopened.findChild<QSplitter *>()->saveState() == splitterState);
    REQUIRE(control<QTreeWidget>(reopened, "torrentJobs")->selectedItems().size() == 1);
    REQUIRE(control<QTreeWidget>(reopened, "torrentJobs")->currentItem()->data(0, Qt::UserRole).toString() == id);
}

TEST_CASE("Torrent peer details follow selected jobs without starting paused jobs", "[qt][torrent-tab][torrent-peers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    QWidget parent;
    TorrentWindow tab(&engine, dir.filePath("layout.ini"), &parent);
    auto *peers = control<QTreeWidget>(tab, "torrentPeers");
    auto *notice = control<QLabel>(tab, "torrentPeerNotice");
    REQUIRE(notice->text().contains("Select"));
    tab.selectJob(id);
    REQUIRE_FALSE(notice->text().contains("Select"));
    REQUIRE(peers->topLevelItemCount() == 0);
    QTest::qWait(1200);
    REQUIRE(engine.jobs().first().paused);
    REQUIRE(engine.effectiveListeners().isEmpty());
    REQUIRE_FALSE(QFile::exists(dir.filePath("payload/payload.bin")));
    control<QTreeWidget>(tab, "torrentJobs")->clearSelection();
    REQUIRE(notice->text().contains("Select"));
    tab.selectJob(id);
    engine.remove(id);
    REQUIRE(waitFor([&] { return control<QTreeWidget>(tab, "torrentJobs")->topLevelItemCount() == 0; }));
    REQUIRE(peers->topLevelItemCount() == 0);
    REQUIRE(notice->text().contains("Select"));
}

TEST_CASE("Torrent layout writes debounce header and splitter changes and flush on close", "[qt][torrent-layout]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("layout.ini");
    TorrentWindow tab(nullptr, path);
    tab.resize(950, 650);
    tab.show();
    QTest::qWait(700);
    auto read = [&](const char *key) {
        QSettings saved(path, QSettings::IniFormat);
        return saved.value(QString::fromLatin1(key)).toByteArray();
    };
    auto *peers = control<QTreeWidget>(tab, "torrentPeers");
    auto *splitter = tab.findChild<QSplitter *>();
    REQUIRE(splitter);
    REQUIRE(splitter->handleWidth() >= 7);
    const auto oldHeader = read("peers/header");
    const auto oldSplitter = read("window/splitter");
    for (int width = 280; width < 290; ++width) {
        peers->setColumnWidth(0, width);
        QTest::qWait(15);
        REQUIRE(read("peers/header") == oldHeader);
    }
    splitter->setSizes({310, 170});
    REQUIRE(QMetaObject::invokeMethod(splitter, "splitterMoved", Qt::DirectConnection,
                                     Q_ARG(int, 310), Q_ARG(int, 1)));
    REQUIRE(read("window/splitter") == oldSplitter);
    REQUIRE(waitFor([&] { return read("peers/header") == peers->header()->saveState(); }));
    REQUIRE(read("window/splitter") == splitter->saveState());
    peers->setColumnWidth(0, 333);
    tab.close();
    REQUIRE(read("peers/header") == peers->header()->saveState());
    QByteArray finalHeader;
    {
        TorrentWindow second(nullptr, path);
        auto *secondPeers = control<QTreeWidget>(second, "torrentPeers");
        secondPeers->setColumnWidth(0, 377);
        finalHeader = secondPeers->header()->saveState();
    }
    REQUIRE(read("peers/header") == finalHeader);
}

TEST_CASE("Torrent peer columns sort numeric snapshots with stable row identities", "[qt][torrent-peers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("layout.ini");
    {
        TorrentWindow tab(nullptr, path);
        auto *peers = control<QTreeWidget>(tab, "torrentPeers");
        REQUIRE(peers->isSortingEnabled());
        Peer low, high;
        low.id = "low"; low.ip = "10.0.0.2"; low.port = 9;
        low.client = "Alpha"; low.state = "Active"; low.progress = 0.09;
        low.downloadRate = 900; low.uploadRate = 900;
        country(low, "DE");
        high.id = "high"; high.ip = "10.0.0.10"; high.port = 10;
        high.client = "Zulu"; high.state = "Waiting"; high.progress = 0.8;
        high.downloadRate = 1200; high.uploadRate = 1200;
        country(high, "US");
        TorrentWindowTestAccess::render(tab, {high, low});
        REQUIRE(peers->topLevelItemCount() == 2);
        for (int column = 0; column < peers->columnCount(); ++column) {
            peers->sortItems(column, Qt::AscendingOrder);
            REQUIRE(peers->topLevelItem(0)->data(0, Qt::UserRole).toString() == "low");
            peers->sortItems(column, Qt::DescendingOrder);
            REQUIRE(peers->topLevelItem(0)->data(0, Qt::UserRole).toString() == "high");
        }
        peers->sortItems(0, Qt::AscendingOrder);
        auto *row = peers->topLevelItem(0);
        peers->setCurrentItem(row);
        high.ip = low.ip;
        TorrentWindowTestAccess::render(tab, {high, low});
        REQUIRE(peers->topLevelItem(0) == row); // Numeric port 9 precedes 10.
        REQUIRE(peers->currentItem() == row);
        low.ip = "2001:db8::2";
        high.ip = "2001:db8::10";
        TorrentWindowTestAccess::render(tab, {high, low});
        REQUIRE(peers->topLevelItem(0) == row);
        low.downloadRate = high.downloadRate;
        peers->sortItems(4, Qt::AscendingOrder);
        TorrentWindowTestAccess::render(tab, {low, high});
        const auto first = peers->topLevelItem(0)->data(0, Qt::UserRole).toString();
        TorrentWindowTestAccess::render(tab, {high, low});
        REQUIRE(peers->topLevelItem(0)->data(0, Qt::UserRole).toString() == first);
        peers->sortItems(5, Qt::DescendingOrder);
        tab.close();
    }
    TorrentWindow reopened(nullptr, path);
    auto *peers = control<QTreeWidget>(reopened, "torrentPeers");
    REQUIRE(peers->isSortingEnabled());
    REQUIRE(peers->sortColumn() == 5);
    REQUIRE(peers->header()->sortIndicatorOrder() == Qt::DescendingOrder);
}

TEST_CASE("Torrent peer country flag comes from snapshot and clears for unknown country", "[qt][torrent-peers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"));
    Peer peer;
    peer.id = "flag";
    peer.ip = "192.0.2.1"; // Documentation address, not a GeoIP lookup fixture.
    country(peer, " us ");
    TorrentWindowTestAccess::render(tab, {peer});
    auto *row = control<QTreeWidget>(tab, "torrentPeers")->topLevelItem(0);
    REQUIRE(row);
    REQUIRE(row->text(1).contains("United States"));
    REQUIRE_FALSE(row->icon(1).isNull());
    REQUIRE_FALSE(row->icon(1).pixmap(24, 16).isNull());
    const auto cached = row->icon(1).cacheKey();
    TorrentWindowTestAccess::render(tab, {peer});
    REQUIRE(row->icon(1).cacheKey() == cached);
    country(peer, "");
    TorrentWindowTestAccess::render(tab, {peer});
    REQUIRE(row->text(1).isEmpty());
    REQUIRE(row->icon(1).isNull());
}

TEST_CASE("Torrent peer refresh and layout saves wait until splitter or header drag ends", "[qt][torrent-layout]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto path = dir.filePath("layout.ini");
    TorrentWindow tab(nullptr, path);
    tab.resize(950, 650);
    control<QTabWidget>(tab, "torrentDetails")->setCurrentIndex(1);
    tab.show();
    QTest::qWait(700);
    auto *peers = control<QTreeWidget>(tab, "torrentPeers");
    auto *splitter = tab.findChild<QSplitter *>();
    REQUIRE(splitter);
    for (auto *target : {static_cast<QWidget *>(splitter->handle(1)), peers->header()->viewport()}) {
        Peer peer;
        peer.id = "drag"; peer.ip = "192.0.2.2"; peer.client = "Before";
        TorrentWindowTestAccess::render(tab, {peer});
        auto *row = peers->topLevelItem(0);
        REQUIRE(row);
        const auto initial = [&] { QSettings saved(path, QSettings::IniFormat);
                                  return saved.value("peers/header").toByteArray(); }();
        QTest::mousePress(target, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        peers->setColumnWidth(0, peers->columnWidth(0) + 11);
        peer.client = "After";
        TorrentWindowTestAccess::render(tab, {peer});
        QTest::qWait(700);
        REQUIRE(row->text(2) == "Before");
        REQUIRE(QSettings(path, QSettings::IniFormat).value("peers/header").toByteArray() == initial);
        QTest::mouseRelease(target, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        REQUIRE(waitFor([&] { return row->text(2) == "After"; }));
        REQUIRE(waitFor([&] { return QSettings(path, QSettings::IniFormat).value("peers/header").toByteArray()
                                    == peers->header()->saveState(); }));
    }
}

TEST_CASE("Torrent security content scrolls without imposing its height on compact peer details", "[qt][torrent-security][torrent-layout]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"));
    auto *details = control<QTabWidget>(tab, "torrentDetails");
    details->setCurrentIndex(2);
    tab.resize(720, 500);
    tab.show();
    QTest::qWait(50);
    REQUIRE(tab.width() <= 720);
    REQUIRE(tab.height() <= 500);
    auto *splitter = tab.findChild<QSplitter *>();
    REQUIRE(splitter);
    details->setCurrentIndex(1);
    splitter->setSizes({splitter->height() - 180, 180});
    QTest::qWait(50);
    REQUIRE(details->height() <= 240);
    auto *peers = control<QTreeWidget>(tab, "torrentPeers");
    REQUIRE(peers->viewport()->height() >= 40);
    REQUIRE(tab.height() <= 500);
    details->setCurrentIndex(2);
    QTest::qWait(50);
    auto *scroll = details->widget(2)->findChild<QScrollArea *>();
    if (!scroll) scroll = qobject_cast<QScrollArea *>(details->widget(2));
    REQUIRE(scroll);
    REQUIRE(scroll->widgetResizable());
    REQUIRE(scroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    REQUIRE(scroll->verticalScrollBar()->maximum() > 0);
    auto *countries = control<QListWidget>(tab, "torrentSecurityCountries");
    REQUIRE(countries->height() >= 100);
    for (auto *label : countries->parentWidget()->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly))
        REQUIRE_FALSE(label->geometry().intersects(countries->geometry()));
    auto *apply = control<QPushButton>(tab, "torrentApplySecurity");
    scroll->ensureWidgetVisible(apply);
    QTest::qWait(20);
    REQUIRE(scroll->viewport()->rect().contains(apply->mapTo(scroll->viewport(), apply->rect().center())));
    REQUIRE(tab.width() <= 720);
    REQUIRE(tab.height() <= 500);
}

TEST_CASE("Torrent priority resting cells follow application item styling", "[qt][torrent-priority-style]")
{
    const auto theme = GENERATE(0, 1);
    const auto direction = GENERATE(Qt::LeftToRight, Qt::RightToLeft);
    const auto selected = GENERATE(false, true);
    CAPTURE(theme, direction, selected);
    ApplicationInputStyle appearance(theme);
    QTemporaryDir dir;
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"));
    auto *files = control<QTreeWidget>(tab, "torrentFiles");
    files->setEnabled(true);
    files->setLayoutDirection(direction);
    auto *row = new QTreeWidgetItem(files, {"payload.bin", "1 B", "Normal"});
    row->setData(2, Qt::UserRole + 1, 4);
    files->ensurePolished();
    QStyleOptionViewItem option;
    option.initFrom(files);
    option.widget = files;
    option.rect = QRect(0, 0, 160, 40);
    option.direction = direction;
    option.state = QStyle::State_Enabled | QStyle::State_Active;
    if (selected) option.state |= QStyle::State_Selected;
    const auto index = files->model()->index(0, 2);
    QImage actual(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
    actual.fill(files->palette().base().color());
    QImage background = actual;
    QPainter expectedPainter(&background);
    files->style()->drawControl(QStyle::CE_ItemViewItem, &option, &expectedPainter, files);
    expectedPainter.end();
    QPainter painter(&actual);
    auto *delegate = files->itemDelegateForIndex(index);
    REQUIRE(delegate);
    delegate->paint(&painter, option, index);
    painter.end();
    // Resting cells are ordinary items, not an unstyled native combo frame.
    for (const QPoint point : {QPoint(2, 10), QPoint(2, 20), QPoint(157, 10), QPoint(157, 20)})
        CHECK(actual.pixelColor(point) == background.pixelColor(point));
    const QRect arrow = QStyle::visualRect(direction, option.rect, QRect(138, 12, 16, 16));
    int arrowPixels = 0;
    for (int y = arrow.top(); y <= arrow.bottom(); ++y)
        for (int x = arrow.left(); x <= arrow.right(); ++x)
            arrowPixels += actual.pixelColor(x, y) != background.pixelColor(x, y);
    CHECK(arrowPixels > 8);
    const QRect textRect = QStyle::visualRect(direction, option.rect, QRect(6, 0, 130, 40));
    const QColor foreground = files->palette().color(selected ? QPalette::HighlightedText : QPalette::Text);
    int textPixels = 0;
    for (int y = textRect.top(); y <= textRect.bottom(); ++y)
        for (int x = textRect.left(); x <= textRect.right(); ++x)
            textPixels += actual.pixelColor(x, y) == foreground;
    CHECK(textPixels > 8);
}

TEST_CASE("Torrent priority sizing stays bounded for large file lists", "[qt][torrent-priority-style]")
{
    const auto theme = GENERATE(0, 1);
    CAPTURE(theme);
    ApplicationInputStyle appearance(theme);
    QTemporaryDir dir;
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"));
    auto *files = control<QTreeWidget>(tab, "torrentFiles");
    files->setEnabled(true);
    {
        const QSignalBlocker blocked(files);
        for (int i = 0; i < 10000; ++i)
            new QTreeWidgetItem(files, {QString::number(i), "1 B", "Normal"});
    }
    files->ensurePolished();
    files->doItemsLayout();
    REQUIRE(files->topLevelItemCount() == 10000);
    CHECK(files->findChildren<QComboBox *>().size() <= 1);
    CHECK_FALSE(files->findChild<QComboBox *>("torrentFilePriorityEditor"));
    QComboBox reference(files->viewport());
    reference.addItem("Normal");
    reference.ensurePolished();
    for (const int row : {0, 5000, 9999}) {
        const auto index = files->model()->index(row, 2);
        CHECK_FALSE(files->indexWidget(index));
        CHECK(files->visualRect(index).height() >= reference.sizeHint().height());
    }
}

TEST_CASE("Torrent priority defaults fit translated editors and retain user widths", "[qt][torrent-priority-style]")
{
    const auto theme = GENERATE(0, 1);
    const auto translated = GENERATE(false, true);
    CAPTURE(theme, translated);
    ApplicationInputStyle appearance(theme);
    class PriorityTranslation final : public QTranslator {
    public:
        bool isEmpty() const override { return false; }
        QString translate(const char *context, const char *source, const char *, int) const override
        {
            if (qstrcmp(context, "TorrentWindow") == 0 && qstrcmp(source, "High") == 0)
                return QStringLiteral("Sehr hohe Dateiprioritaet");
            return {};
        }
        ~PriorityTranslation() override { qApp->removeTranslator(this); }
    } translation;
    if (translated) REQUIRE(qApp->installTranslator(&translation));
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    const auto layout = dir.filePath("layout.ini");
    int userWidth = 0;
    {
        TorrentWindow tab(&engine, layout);
        tab.selectJob(id);
        tab.resize(1400, 760);
        tab.show();
        QTest::qWait(50);
        auto *files = control<QTreeWidget>(tab, "torrentFiles");
        const auto cell = files->visualRect(files->model()->index(0, 2));
        QTest::mouseClick(files->viewport(), Qt::LeftButton, Qt::NoModifier, cell.center());
        QPointer<QComboBox> combo;
        REQUIRE(waitFor([&] {
            combo = files->findChild<QComboBox *>("torrentFilePriorityEditor");
            return combo && combo->view()->isVisible();
        }));
        if (translated) REQUIRE(combo->itemText(2) == "Sehr hohe Dateiprioritaet");
        CHECK(cell.width() >= combo->sizeHint().width() + 4);
        CHECK(combo->width() >= combo->sizeHint().width());
        CHECK(cell.contains(combo->geometry()));
        const auto choice = combo->model()->index(2, 0);
        QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          combo->view()->visualRect(choice).center());
        REQUIRE(waitFor([&] { return combo.isNull(); }));
        // A deliberate user width, even narrower than the editor, must remain authoritative.
        userWidth = 90;
        files->setColumnWidth(2, userWidth);
        control<QPushButton>(tab, "torrentApplyFiles")->click();
        REQUIRE(waitFor([&] { return engine.files(id).first().priority == 7; }));
        tab.selectJob(id);
        CHECK(files->columnWidth(2) == userWidth);
        tab.close();
    }
    TorrentWindow reopened(&engine, layout);
    CHECK(control<QTreeWidget>(reopened, "torrentFiles")->columnWidth(2) == userWidth);
}

TEST_CASE("Torrent file checkbox and priority edits apply and survive refresh", "[qt][torrent-checkbox][torrent-controls][torrent-priority-style]")
{
    const auto theme = GENERATE(-1, 0, 1);
    CAPTURE(theme);
    ApplicationInputStyle appearance(theme);
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    TorrentWindow tab(&engine, dir.filePath("layout.ini"));
    tab.selectJob(id);
    tab.resize(1000, 760);
    tab.show();
    QTest::qWait(50);
    auto *files = control<QTreeWidget>(tab, "torrentFiles");
    auto *item = files->topLevelItem(0);
    REQUIRE(files->columnCount() == 3);
    QStyleOptionViewItem option;
    REQUIRE(files->itemDelegateForColumn(0)->createEditor(files->viewport(), option,
                 files->model()->index(0, 0)) == nullptr);
    REQUIRE(item->text(2) == "Normal");
    auto rect = files->visualItemRect(item);
    QTest::mouseClick(files->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(60, rect.center().y()));
    REQUIRE(item->checkState(0) == Qt::Unchecked);
    QTest::keyClick(files, Qt::Key_Space);
    REQUIRE(item->checkState(0) == Qt::Checked);
    const auto capture = [&](QWidget *widget, const QString &state) {
        const auto directory = qEnvironmentVariable("EISKALT_TORRENT_PRIORITY_SCREENSHOTS");
        if (directory.isEmpty() || theme < 0) return;
        REQUIRE(QDir().mkpath(directory));
        const QString name = QStringLiteral("priority-%1-%2-%3x.png")
            .arg(theme == 1 ? "dark" : "light", state, QString::number(widget->devicePixelRatioF()));
        REQUIRE(widget->grab().save(QDir(directory).filePath(name)));
    };
    capture(&tab, QStringLiteral("resting"));
    const auto priorityRect = files->visualRect(files->model()->index(0, 2));
    QTest::mouseClick(files->viewport(), Qt::LeftButton, Qt::NoModifier, priorityRect.center());
    QPointer<QComboBox> combo;
    REQUIRE(waitFor([&] {
        combo = files->findChild<QComboBox *>("torrentFilePriorityEditor");
        return combo && combo->view()->isVisible();
    }));
    REQUIRE(combo->currentData().toInt() == 4);
    REQUIRE(combo->count() == 3);
    REQUIRE(combo->itemData(0).toInt() == 1);
    REQUIRE(combo->itemData(2).toInt() == 7);
    CHECK(combo->styleSheet().isEmpty());
    CHECK(priorityRect.width() >= combo->sizeHint().width() + 4);
    CHECK(combo->width() >= combo->sizeHint().width());
    CHECK(priorityRect.height() >= combo->sizeHint().height());
    CHECK(priorityRect.contains(combo->geometry()));
    CHECK(combo->height() >= combo->sizeHint().height());
    const int restingRowHeight = priorityRect.height();
    capture(&tab, QStringLiteral("editor"));
    capture(combo->view()->window(), QStringLiteral("popup"));
    QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      combo->view()->visualRect(combo->model()->index(2, 0)).center());
    REQUIRE(waitFor([&] { return item->text(2) == "High"; }));
    // Reopen the already-selected cell using its arrow, not editItem or keyboard activation.
    for (const int priority : {1, 4, 7}) {
        QPointer<QComboBox> previous(combo);
        REQUIRE(waitFor([&] { return previous.isNull(); }));
        const auto cell = files->visualRect(files->model()->index(0, 2));
        QTest::mouseClick(files->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(cell.right() - 10, cell.center().y()));
        REQUIRE(waitFor([&] {
            combo = files->findChild<QComboBox *>("torrentFilePriorityEditor");
            return combo && combo->view()->isVisible();
        }));
        QComboBox reference(files->viewport());
        reference.ensurePolished();
        CHECK(combo->palette().color(QPalette::Base) == reference.palette().color(QPalette::Base));
        CHECK(combo->palette().color(QPalette::Text) == reference.palette().color(QPalette::Text));
        CHECK(cell.height() == restingRowHeight);
        CHECK(cell.width() >= combo->sizeHint().width() + 4);
        CHECK(combo->width() >= combo->sizeHint().width());
        CHECK(cell.contains(combo->geometry()));
        CHECK(combo->height() >= combo->sizeHint().height());
        const auto choice = combo->model()->index(combo->findData(priority), 0);
        QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          combo->view()->visualRect(choice).center());
        REQUIRE(waitFor([&] { return item->data(2, Qt::UserRole + 1).toInt() == priority; }));
    }
    tab.selectJob(id);
    REQUIRE(item->text(2) == "High");
    control<QPushButton>(tab, "torrentApplyFiles")->click();
    REQUIRE(waitFor([&] { return engine.files(id).first().priority == 7; }));
    auto *stop = control<QAction>(tab, "torrentStop");
    REQUIRE(stop->isEnabled());
    stop->trigger();
    REQUIRE(engine.jobs().first().stopped);
    REQUIRE(engine.jobs().first().paused);
    REQUIRE(waitFor([&] { return !stop->isEnabled(); }));
    REQUIRE(control<QAction>(tab, "torrentResume")->isEnabled());
    REQUIRE(files->topLevelItem(0)->text(2) == "High");
}

TEST_CASE("Stopped jobs stay grey in management with state-aware sharing actions", "[qt][torrent-controls][torrent-ui-fixes]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    TorrentWindow tab(&engine, dir.filePath("layout.ini"));
    auto *jobs = control<QTreeWidget>(tab, "torrentJobs");
    tab.selectJob(id);
    auto *pause = control<QAction>(tab, "torrentPause");
    auto *resume = control<QAction>(tab, "torrentResume");
    auto *stop = control<QAction>(tab, "torrentStop");
    CHECK(pause->isCheckable());
    CHECK(pause->isChecked());
    CHECK_FALSE(resume->isChecked());
    auto *progress = qobject_cast<QProgressBar *>(jobs->itemWidget(jobs->topLevelItem(0), 2));
    REQUIRE(progress);
    CHECK(progress->palette().color(QPalette::Highlight) == QColor(255, 193, 120));
    engine.stop(id);
    REQUIRE(waitFor([&] { return !stop->isEnabled(); }));
    REQUIRE(jobs->topLevelItemCount() == 1);
    CHECK(stop->isChecked());
    CHECK_FALSE(pause->isChecked());
    CHECK_FALSE(pause->isEnabled());
    CHECK(resume->isEnabled());
    CHECK(progress->palette().color(QPalette::Highlight) == QColor(150, 150, 150));
    CHECK(jobs->topLevelItem(0)->foreground(0).color() == QColor(150, 150, 150));
    QSignalSpy sharing(&tab, SIGNAL(magnetShareRequested(QStringList,bool)));
    REQUIRE(sharing.isValid());
    for (const bool dc : {true, false}) {
        const auto text = dc ? "Paste DC++ magnet(s) in chat" : "Paste Torrent Magnet(s) in chat";
        QAction *share = nullptr;
        for (auto *action : jobs->actions()) if (action->text() == text) share = action;
        REQUIRE(share);
        REQUIRE(share->isEnabled());
        share->trigger();
        REQUIRE(sharing.last().at(0).toStringList() == QStringList{id});
        REQUIRE(sharing.last().at(1).toBool() == dc);
        jobs->clearSelection();
        REQUIRE_FALSE(share->isEnabled());
        tab.selectJob(id);
    }
}

TEST_CASE("Torrent file rows use extension icons", "[qt][torrent-ui-fixes]")
{
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    TorrentWindow tab(&engine, dir.filePath("layout.ini"));
    tab.selectJob(id);
    auto *files = control<QTreeWidget>(tab, "torrentFiles");
    FileExtensionIcons cache;
    const auto expected = cache.iconForFile("payload.bin", files->style()->standardIcon(QStyle::SP_FileIcon));
    REQUIRE_FALSE(files->topLevelItem(0)->icon(0).isNull());
    REQUIRE(files->topLevelItem(0)->icon(0).pixmap(16, 16).toImage() == expected.pixmap(16, 16).toImage());
}

TEST_CASE("Torrent checkbox decoration and text follow layout direction without overlap", "[qt][torrent-ui-fixes]")
{
    const auto direction = GENERATE(Qt::LeftToRight, Qt::RightToLeft);
    QWidget widget;
    QStandardItemModel model(1, 1);
    model.setData(model.index(0, 0), "payload.txt");
    model.setData(model.index(0, 0), Qt::Checked, Qt::CheckStateRole);
    QPixmap icon(16, 16);
    icon.fill(Qt::magenta);
    model.setData(model.index(0, 0), QIcon(icon), Qt::DecorationRole);
    QStyleOptionViewItem option;
    option.initFrom(&widget);
    option.widget = &widget;
    option.direction = direction;
    option.rect = QRect(10, 10, 240, 28);
    option.decorationSize = QSize(16, 16);
    QImage image(260, 48, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    TorrentCheckDelegate delegate;
    delegate.paint(&painter, option, model.index(0, 0));
    painter.end();
    const auto iconRect = QStyle::visualRect(direction, option.rect, QRect(37, 16, 16, 16));
    REQUIRE(image.pixelColor(iconRect.center()) == QColor(Qt::magenta));
    const auto boxRect = QStyle::visualRect(direction, option.rect, QRect(15, 17, 14, 14));
    REQUIRE(image.pixelColor(boxRect.center()) != QColor(Qt::magenta));
}

TEST_CASE("Shared Torrent action helper exposes mutually exclusive current state", "[qt][torrent-ui-fixes]")
{
#if __has_include("TorrentActionMenu.h")
    QWidget owner;
    QMenu menu(&owner);
    const auto actions = torrent_action_menu::create(&menu, &owner);
    menu.addActions({actions.pause, actions.resume, actions.stop});
    for (const bool available : {false, true}) {
        for (int state = 0; state < 4; ++state) {
            Job job;
            job.paused = state == 1 || state == 2;
            job.stopped = state == 2;
            const QList<Job> selected = state == 3 ? QList<Job>{} : QList<Job>{job};
            torrent_action_menu::update(actions, selected, available);
            CHECK(actions.pause->isChecked() == (state == 1));
            CHECK(actions.resume->isChecked() == (state == 0));
            CHECK(actions.stop->isChecked() == (state == 2));
            CHECK(actions.pause->isEnabled() == (available && state == 0));
            CHECK(actions.resume->isEnabled() == (available && (state == 1 || state == 2)));
            CHECK(actions.stop->isEnabled() == (state == 0 || state == 1));
        }
    }
    Job active, paused, stopped;
    paused.paused = true;
    stopped.stopped = true;
    torrent_action_menu::update(actions, {active, paused, stopped}, true);
    CHECK_FALSE(actions.pause->isChecked());
    CHECK_FALSE(actions.resume->isChecked());
    CHECK_FALSE(actions.stop->isChecked());
    CHECK(actions.pause->isEnabled());
    CHECK(actions.resume->isEnabled());
    CHECK(actions.stop->isEnabled());
    CHECK(menu.palette() == owner.palette());
#else
    FAIL("Shared state-aware Torrent actions are missing");
#endif
}

TEST_CASE("Torrent country indicators paint at every row and row clicks toggle once", "[qt][torrent-checkbox]")
{
    QTemporaryDir dir;
    TorrentWindow tab(nullptr, dir.filePath("layout.ini"));
    tab.resize(1000, 760);
    control<QTabWidget>(tab, "torrentDetails")->setCurrentIndex(2);
    tab.show();
    QTest::qWait(100);
    auto *list = control<QListWidget>(tab, "torrentSecurityCountries");
    auto *row = list->item(3);
    list->scrollToItem(row, QAbstractItemView::PositionAtCenter);
    QTest::qWait(30);
    const auto rect = list->visualItemRect(row);
    const QRect indicator(rect.left(), rect.top(), 28, rect.height());
    const auto before = list->viewport()->grab(indicator).toImage();
    row->setCheckState(Qt::Checked);
    QTest::qWait(30);
    CHECK(before != list->viewport()->grab(indicator).toImage());
    row->setCheckState(Qt::Unchecked);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
    REQUIRE(row->checkState() == Qt::Checked);
    QTest::keyClick(list, Qt::Key_Space);
    REQUIRE(row->checkState() == Qt::Unchecked);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rect.left() + 10, rect.center().y()));
    REQUIRE(row->checkState() == Qt::Checked);
    auto *search = control<QLineEdit>(tab, "torrentSecuritySearch");
    search->setText("China");
    search->clear();
    REQUIRE(row->checkState() == Qt::Checked);
    REQUIRE(control<QPushButton>(tab, "torrentApplySecurity")->isEnabled());
}

TEST_CASE("Torrent security edits are global explicit requests and never auto apply", "[qt][torrent-security]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    TorrentWindow tab(&engine, dir.filePath("layout.ini"));
    tab.selectJob(id);
    auto *details = control<QTabWidget>(tab, "torrentDetails");
    REQUIRE(details->count() == 3);
    details->setCurrentIndex(2);
    auto *encryption = control<QComboBox>(tab, "torrentSecurityEncryption");
    auto *countries = control<QListWidget>(tab, "torrentSecurityCountries");
    auto *search = control<QLineEdit>(tab, "torrentSecuritySearch");
    auto *unknown = control<QCheckBox>(tab, "torrentSecurityUnknown");
    auto *apply = control<QPushButton>(tab, "torrentApplySecurity");
    REQUIRE(encryption->count() == 3);
    REQUIRE_FALSE(unknown->isChecked());
    REQUIRE_FALSE(apply->isEnabled());
    QSignalSpy requested(&tab, SIGNAL(securitySettingsRequested(eiskalt::torrent::EncryptionMode,QStringList,bool)));
    REQUIRE(requested.isValid());
    setSecurity(tab, engine.settings());
    auto *files = control<QTreeWidget>(tab, "torrentFiles");
    files->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
    encryption->setCurrentIndex(encryption->findData(static_cast<int>(EncryptionMode::Required)));
    unknown->setChecked(true);
    QListWidgetItem *us = nullptr, *de = nullptr;
    for (int i = 0; i < countries->count(); ++i) {
        auto *item = countries->item(i);
        if (item->data(Qt::UserRole).toString() == "US") us = item;
        if (item->data(Qt::UserRole).toString() == "DE") de = item;
    }
    REQUIRE(us);
    REQUIRE(de);
    us->setCheckState(Qt::Checked);
    de->setCheckState(Qt::Checked);
    search->setText("United States");
    REQUIRE_FALSE(us->isHidden());
    REQUIRE(de->isHidden());
    tab.selectJob(id);
    setSecurity(tab, engine.settings()); // Unchanged security on unrelated reload.
    REQUIRE(details->currentIndex() == 2);
    REQUIRE(unknown->isChecked());
    REQUIRE(encryption->currentData().toInt() == static_cast<int>(EncryptionMode::Required));
    REQUIRE(files->topLevelItem(0)->checkState(0) == Qt::Unchecked);
    REQUIRE(control<QPushButton>(tab, "torrentApplyFiles")->isEnabled());
    REQUIRE(apply->isEnabled());
    REQUIRE(requested.isEmpty());
    apply->click();
    REQUIRE(requested.size() == 1);
    REQUIRE(requested.first().at(0).value<EncryptionMode>() == EncryptionMode::Required);
    REQUIRE(requested.first().at(1).toStringList() == QStringList{"DE", "US"});
    REQUIRE(requested.first().at(2).toBool());
    REQUIRE(engine.settings().encryptionMode == EncryptionMode::Optional);
    REQUIRE(engine.effectiveListeners().isEmpty());
    REQUIRE(engine.jobs().first().paused);
    tab.close();
    QSettings saved(dir.filePath("layout.ini"), QSettings::IniFormat);
    for (const auto &key : saved.allKeys()) {
        REQUIRE_FALSE(key.contains("security", Qt::CaseInsensitive));
        REQUIRE_FALSE(key.contains("blocked", Qt::CaseInsensitive));
    }
}

TEST_CASE("Torrent observation rejects missing jobs and clears synchronously at lifecycle boundaries", "[qt][torrent-peers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    const auto other = engine.add("magnet:?xt=urn:btih:0123456789012345678901234567890123456789",
                                  dir.filePath("other"), {}, true);
    REQUIRE_FALSE(other.isEmpty());
    engine.selectObservedJob(id);
    REQUIRE(engine.observedJob() == id);
    REQUIRE(engine.peers(id).isEmpty());
    engine.selectObservedJob(other);
    REQUIRE(engine.observedJob() == other);
    REQUIRE(engine.peers(id).isEmpty());
    engine.selectObservedJob("missing-job");
    REQUIRE(engine.observedJob().isEmpty());
    engine.selectObservedJob(id);
    engine.configure(engine.settings(), ProxyConfig{});
    REQUIRE(engine.peers(id).isEmpty());
    engine.remove(id);
    REQUIRE(engine.observedJob().isEmpty());
    REQUIRE(engine.peers(id).isEmpty());
    engine.selectObservedJob(other);
    engine.shutdown();
    REQUIRE(engine.observedJob().isEmpty());
    REQUIRE(engine.peers(other).isEmpty());
    engine.selectObservedJob(other);
    REQUIRE(engine.observedJob().isEmpty());
}

TEST_CASE("Torrent job proxy decoration follows the effective engine route", "[qt][torrent-peers]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    TorrentEngine engine(dir.filePath("state"));
    const auto id = pausedFixture(dir, engine);
    REQUIRE_FALSE(engine.jobs().first().proxied);
    auto settings = engine.settings();
    settings.proxyMode = ProxyMode::FollowApplication;
    // The engine receives SOCKS5 for a prepared encrypted TCP adapter too.
    ProxyConfig adapter{ProxyType::Socks5, "127.0.0.1", {}, {}, {}, 1080, true, false};
    QSignalSpy configured(&engine, &TorrentEngine::configurationApplied);
    auto applied = engine.configure(settings, adapter);
    REQUIRE(waitFor([&] {
        return !configured.isEmpty() && configured.last().first().toULongLong() == applied;
    }));
    REQUIRE(engine.jobs().first().id == id);
    REQUIRE(engine.jobs().first().proxied);
    settings.proxyMode = ProxyMode::Direct;
    applied = engine.configure(settings, adapter);
    REQUIRE(waitFor([&] {
        return !configured.isEmpty() && configured.last().first().toULongLong() == applied;
    }));
    REQUIRE_FALSE(engine.jobs().first().proxied);
    REQUIRE(engine.effectiveListeners().isEmpty());
}
#endif

#ifdef USE_TORRENT
namespace {
template<class Snapshot> bool windowExcluded(const Snapshot &job) {
    if constexpr (requires { job.dcShareExcluded; }) return job.dcShareExcluded;
    else { FAIL("Job has no DC sharing exclusion"); return false; }
}
}
TEST_CASE("Torrent DC sharing controls apply full selection without unsafe single-row actions", "[qt][torrent][dc-exclusion]") {
    const auto theme = GENERATE(0, 1);
    CAPTURE(theme);
    ApplicationInputStyle appearance(theme);
    QTemporaryDir dir;
    TorrentEngine engine(dir.filePath("state"));
    const auto first = pausedFixture(dir, engine);
    const auto second = engine.add("magnet:?xt=urn:btih:0123456789012345678901234567890123456789", dir.filePath("second"), {}, true);
    REQUIRE_FALSE(second.isEmpty());
    TorrentWindow window(&engine, dir.filePath("layout.ini"));
    window.resize(1000, 700);
    window.show();
    QTest::qWait(50);
    auto *jobs = control<QTreeWidget>(window, "torrentJobs");
    REQUIRE(waitFor([&] { return jobs->topLevelItemCount() == 2; }));
    REQUIRE(jobs->selectionMode() == QAbstractItemView::ExtendedSelection);
    auto *exclude = control<QAction>(window, "torrentDcShareExclude");
    auto *follow = control<QAction>(window, "torrentDcShareFollowGlobal");
    auto *menu = qobject_cast<QMenu *>(exclude->parent());
    REQUIRE(menu);
    const auto captureMenu = [&](const QString &state, bool allExcluded) {
        menu->popup(window.mapToGlobal(QPoint(400, 200)));
        REQUIRE(waitFor([&] { return menu->isVisible(); }));
        REQUIRE(follow->isEnabled());
        REQUIRE(exclude->isEnabled());
        REQUIRE_FALSE(follow->isChecked());
        REQUIRE(exclude->isChecked() == allExcluded);
        for (auto *action : {follow, exclude}) {
            REQUIRE(action->isVisible());
            REQUIRE_FALSE(menu->actionGeometry(action).isEmpty());
            REQUIRE(menu->rect().contains(menu->actionGeometry(action).center()));
        }
        const auto directory = qEnvironmentVariable("EISKALT_DC_SHARING_SCREENSHOTS");
        if (!directory.isEmpty()) {
            REQUIRE(QDir().mkpath(directory));
            for (QWidget *widget : {static_cast<QWidget *>(&window), static_cast<QWidget *>(menu)}) {
                const auto name = QStringLiteral("dc-sharing-%1-%2-%3-%4x.png")
                    .arg(theme == 1 ? "dark" : "light", state, widget == menu ? "menu" : "window",
                         QString::number(widget->devicePixelRatioF()));
                REQUIRE(widget->grab().save(QDir(directory).filePath(name)));
            }
        }
        menu->hide();
    };
    jobs->clearSelection();
    REQUIRE_FALSE(exclude->isEnabled());
    REQUIRE_FALSE(follow->isEnabled());
    window.selectJob(first);
    REQUIRE(follow->isChecked());
    exclude->trigger();
    REQUIRE(exclude->isChecked());
    for (int i = 0; i < jobs->topLevelItemCount(); ++i) jobs->topLevelItem(i)->setSelected(true);
    REQUIRE(jobs->selectedItems().size() == 2);
    REQUIRE_FALSE(exclude->isChecked());
    REQUIRE_FALSE(follow->isChecked());
    captureMenu(QStringLiteral("mixed"), false);
    for (const char *name : {"torrentPause", "torrentResume", "torrentStop", "torrentRemove", "torrentDelete", "torrentRecheck"})
        REQUIRE_FALSE(control<QAction>(window, name)->isEnabled());
    REQUIRE_FALSE(control<QTreeWidget>(window, "torrentFiles")->isEnabled());
    REQUIRE(engine.observedJob().isEmpty());
    QSignalSpy share(&window, &TorrentWindow::magnetShareRequested);
    for (const char *name : {"torrentShareDc", "torrentShareTorrent"}) {
        auto *action = control<QAction>(window, name);
        REQUIRE(action->isEnabled());
        action->trigger();
        const auto ids = share.last().first().toStringList();
        REQUIRE(ids.size() == 2);
        REQUIRE(ids.contains(first));
        REQUIRE(ids.contains(second));
    }
    exclude->trigger();
    REQUIRE(exclude->isChecked());
    for (const auto &job : engine.jobs()) REQUIRE(windowExcluded(job));
    QTest::qWait(300);
    REQUIRE(jobs->selectedItems().size() == 2);
    captureMenu(QStringLiteral("all-excluded"), true);
    follow->trigger();
    for (const auto &job : engine.jobs()) REQUIRE_FALSE(windowExcluded(job));
    REQUIRE(follow->isChecked());
}
#endif
