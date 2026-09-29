#include <catch2/catch_test_macros.hpp>
#include "PendingOpenEvents.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QStringList>
#include <memory>

namespace {
const QString dcMagnet = QStringLiteral("magnet:?xt=urn:tree:tiger:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG");
const QString btMagnet = QStringLiteral("magnet:?xt=urn:btih:0123456789012345678901234567890123456789");

bool awaitEvents(const std::function<bool()> &ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 1500) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}
}

TEST_CASE("Native open sources preserve paths and fall back to encoded URLs", "[qt][open-events]")
{
    const QString path = "/tmp/space # 100%.torrent";
    REQUIRE(PendingOpenEvents::nativeSource(path, QUrl(btMagnet)) == path);
    REQUIRE(PendingOpenEvents::nativeSource({}, QUrl(btMagnet)) == btMagnet);
    REQUIRE(PendingOpenEvents::nativeSource({}, QUrl::fromLocalFile(path)) == "file:///tmp/space%20%23%20100%25.torrent");
    REQUIRE(PendingOpenEvents::nativeSource({}, QUrl()).isEmpty());
}

TEST_CASE("Early opens wait for both a handler and startup readiness without losing duplicates", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    const QString hub = "adcs://example.test:1511";
    REQUIRE(pending.enqueue(dcMagnet));
    REQUIRE(pending.enqueue(hub));
    REQUIRE(pending.enqueue(dcMagnet));
    QCoreApplication::processEvents();
    REQUIRE(delivered.isEmpty());
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    QCoreApplication::processEvents();
    REQUIRE(delivered.isEmpty());
    pending.setReady(true);
    REQUIRE(delivered.isEmpty());
    REQUIRE(awaitEvents([&] { return delivered.size() == 3; }));
    REQUIRE(delivered == QStringList{dcMagnet, hub, dcMagnet});
    pending.setReady(true);
    QCoreApplication::processEvents();
    REQUIRE(delivered.size() == 3);
}

TEST_CASE("Opens arriving during a handler are deferred rather than reentrant or discarded", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    int depth = 0, maximumDepth = 0;
    pending.setHandler([&](const QString &source) {
        ++depth;
        maximumDepth = qMax(maximumDepth, depth);
        delivered.append(source);
        if (delivered.size() == 1) {
            REQUIRE(pending.enqueue("adc://arrived-during-dialog.test"));
            QCoreApplication::processEvents();
            REQUIRE(delivered.size() == 1);
        }
        --depth;
    });
    pending.setReady(true);
    REQUIRE(pending.enqueue(dcMagnet));
    REQUIRE(pending.enqueue("dchub://already-pending.test"));
    REQUIRE(awaitEvents([&] { return delivered.size() == 3; }));
    REQUIRE(maximumDepth == 1);
    REQUIRE(delivered == QStringList{dcMagnet, "dchub://already-pending.test", "adc://arrived-during-dialog.test"});
}

TEST_CASE("A modal delivery gate retains events until it permits delivery", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    bool modalOpen = true;
    int gateCalls = 0;
    pending.setHandler([&](const QString &source) { delivered.append(source); }, [&] {
        ++gateCalls;
        return !modalOpen;
    });
    pending.setReady(true);
    REQUIRE(pending.enqueue(dcMagnet));
    REQUIRE(awaitEvents([&] { return gateCalls > 0; }));
    REQUIRE(delivered.isEmpty());
    modalOpen = false;
    REQUIRE(awaitEvents([&] { return delivered.size() == 1; }));
    REQUIRE(delivered.first() == dcMagnet);
}

TEST_CASE("Readiness can be revoked during startup or shutdown without losing queued events", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    pending.setHandler([&](const QString &source) {
        delivered.append(source);
        pending.setReady(false);
    });
    REQUIRE(pending.enqueue(dcMagnet));
    REQUIRE(pending.enqueue("nmdcs://second.test"));
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() == 1; }));
    QCoreApplication::processEvents();
    REQUIRE(delivered.size() == 1);
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() == 2; }));
    REQUIRE(delivered == QStringList{dcMagnet, "nmdcs://second.test"});
}

TEST_CASE("Malformed and unsupported native opens do not consume valid pending events", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    const QStringList invalid = {
        "", " ", "https://example.test/file.torrent", "javascript:alert(1)",
        "file:///tmp/file.torrent?query=1", "magnet:garbage", "magnet:?xt=urn:btih:short",
        "magnet:?xt=urn:tree:tiger:short", "magnet:?dn=name-only", "adc://", "adcs://host:70000",
        dcMagnet + "\n" + dcMagnet, dcMagnet + "\n", dcMagnet + "&dn=%00hidden", dcMagnet + "&dn=%0Asecond-line",
        QString("/tmp/bad") + QChar(0) + ".torrent", QString(65537, QLatin1Char('x'))
    };
    REQUIRE(pending.enqueue(dcMagnet));
    for (const auto &source : invalid) {
        INFO(source.left(100).toStdString());
        REQUIRE_FALSE(pending.enqueue(source));
    }
    REQUIRE(pending.enqueue("adc://valid-after-rejections.test"));
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() == 2; }));
    REQUIRE(delivered == QStringList{dcMagnet, "adc://valid-after-rejections.test"});
}

TEST_CASE("Native Torrent opens respect the optional build and decode local file URLs", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    pending.setReady(true);
#ifdef USE_TORRENT
    REQUIRE(pending.enqueue(btMagnet));
    REQUIRE(pending.enqueue("file:///tmp/space%20%23%20100%25.torrent"));
    REQUIRE(pending.enqueue("/tmp/raw %25.torrent"));
    REQUIRE(pending.enqueue("relative %25.torrent"));
    REQUIRE(awaitEvents([&] { return delivered.size() == 4; }));
    REQUIRE(delivered == QStringList{btMagnet, "/tmp/space # 100%.torrent", "/tmp/raw %25.torrent",
                                    QFileInfo("relative %25.torrent").absoluteFilePath()});
#else
    REQUIRE_FALSE(pending.enqueue(btMagnet));
    REQUIRE_FALSE(pending.enqueue("file:///tmp/file.torrent"));
    REQUIRE_FALSE(pending.enqueue("/tmp/file.torrent"));
    REQUIRE_FALSE(pending.enqueue("relative.torrent"));
    QCoreApplication::processEvents();
    REQUIRE(delivered.isEmpty());
#endif
}

TEST_CASE("Destroying the open queue cancels scheduled delivery", "[qt][open-events]")
{
    int delivered = 0;
    {
        auto pending = std::make_unique<PendingOpenEvents>();
        pending->setHandler([&](const QString &) { ++delivered; });
        pending->setReady(true);
        REQUIRE(pending->enqueue(dcMagnet));
    }
    QCoreApplication::processEvents();
    REQUIRE(delivered == 0);
}

TEST_CASE("Explicit activation waits for startup while empty open input is rejected", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    REQUIRE_FALSE(pending.enqueue(""));
    pending.enqueueActivation();
    REQUIRE(pending.enqueue(dcMagnet));
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    QCoreApplication::processEvents();
    REQUIRE(delivered.isEmpty());
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() == 2; }));
    REQUIRE(delivered == QStringList{QString(), dcMagnet});
}

TEST_CASE("A burst buffered before consumer installation drains once in order", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList expected, delivered;
    pending.setReady(true);
    for (int i = 0; i < 128; ++i) {
        const QString source = QStringLiteral("adc://hub-%1.test").arg(i);
        expected.append(source);
        REQUIRE(pending.enqueue(source));
        REQUIRE_FALSE(pending.enqueue("magnet:?xt=broken"));
    }
    QCoreApplication::processEvents();
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    REQUIRE(delivered.isEmpty());
    REQUIRE(awaitEvents([&] { return delivered.size() == expected.size(); }));
    REQUIRE(delivered == expected);
    QCoreApplication::processEvents();
    REQUIRE(delivered == expected);
}

TEST_CASE("Native opens retain DC keyword searches and legacy Tiger magnets", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    const QString hash = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG";
    const QStringList expected = {
        "magnet:?kt=some%20file&dn=some%20file",
        "magnet:?xt=urn:tree:tiger/:" + hash,
        "magnet:?xt=urn:tree:tiger/1024:" + hash,
        "magnet:?xt=urn:bitprint:ABCDEFGHIJKLMNOPQRSTUVWXYZ234567." + hash
    };
    for (const auto &source : expected)
        REQUIRE(pending.enqueue(source));
    REQUIRE_FALSE(pending.enqueue("magnet:?kt="));
    REQUIRE_FALSE(pending.enqueue("magnet:?xt=urn:btih:short&kt=not-a-DC-search"));
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() == expected.size(); }));
    REQUIRE(delivered == expected);
}

TEST_CASE("Pending opens reject overflow without eviction and reuse drained capacity", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList expected, delivered;
    for (int i = 0; i < 127; ++i) {
        const QString source = QStringLiteral("adc://queued-%1.test").arg(i);
        REQUIRE(pending.enqueue(source));
        expected.append(source);
    }
    pending.enqueueActivation();
    expected.append(QString());
    REQUIRE_FALSE(pending.enqueue("adc://overflow.test"));
    pending.enqueueActivation();
    pending.setHandler([&](const QString &source) {
        delivered.append(source);
        if (delivered.size() == 1)
            pending.setReady(false);
    });
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() == 1; }));
    REQUIRE(pending.enqueue("adc://after-one-drain.test"));
    expected.append("adc://after-one-drain.test");
    REQUIRE_FALSE(pending.enqueue("adc://still-full.test"));
    pending.enqueueActivation();
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() >= expected.size(); }));
    QCoreApplication::processEvents();
    REQUIRE(delivered == expected);
}

TEST_CASE("Activation-only requests cannot bypass the pending limit", "[qt][open-events]")
{
    PendingOpenEvents pending;
    QStringList delivered;
    for (int i = 0; i < 160; ++i)
        pending.enqueueActivation();
    REQUIRE_FALSE(pending.enqueue(dcMagnet));
    pending.setHandler([&](const QString &source) { delivered.append(source); });
    pending.setReady(true);
    REQUIRE(awaitEvents([&] { return delivered.size() >= 128; }));
    QCoreApplication::processEvents();
    REQUIRE(delivered.size() == 128);
    for (const auto &source : delivered)
        REQUIRE(source.isEmpty());
    REQUIRE(pending.enqueue(dcMagnet));
    REQUIRE(awaitEvents([&] { return delivered.size() == 129; }));
    REQUIRE(delivered.last() == dcMagnet);
}
