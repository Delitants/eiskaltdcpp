#include <catch2/catch_test_macros.hpp>
#include "torrent/TorrentCreator.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <map>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace eiskalt::torrent;
namespace lt = libtorrent;

namespace {
void ensureApplication()
{
    if (QCoreApplication::instance()) return;
    static int argc = 1;
    static char name[] = "torrent-creator-tests";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
}

bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

struct Fixture {
    QTemporaryDir temporary;
    QString root;
    CreateTorrentOptions options;
    Fixture() {
        ensureApplication();
        REQUIRE(temporary.isValid());
        root = QDir(temporary.path()).canonicalPath();
        options.sourcePath = root + "/payload.bin";
        options.outputPath = root + "/result.torrent";
        writeFile(options.sourcePath, QByteArray(70001, 'x'));
    }
};

struct Result {
    int successes = 0, failures = 0, cancellations = 0;
    QString output, parent, error;
    explicit Result(TorrentCreator &creator) {
        QObject::connect(&creator, &TorrentCreator::finished, &creator, [this](QString path, QString source) {
            ++successes; output = path; parent = source;
        });
        QObject::connect(&creator, &TorrentCreator::failed, &creator, [this](QString message) {
            ++failures; error = message;
        });
        QObject::connect(&creator, &TorrentCreator::cancelled, &creator, [this] { ++cancellations; });
    }
    bool done() const { return successes + failures + cancellations > 0; }
};

void checkRejected(Fixture &f)
{
    TorrentCreator creator;
    Result result(creator);
    REQUIRE(creator.start(f.options));
    REQUIRE(waitFor([&] { return result.done(); }));
    INFO(result.error.toStdString());
    REQUIRE(result.failures == 1);
    REQUIRE_FALSE(result.error.isEmpty());
    REQUIRE(result.successes == 0);
    REQUIRE(result.cancellations == 0);
    REQUIRE_FALSE(creator.isRunning());
    REQUIRE(QDir(f.root).entryList({".torrent-create-*"}, QDir::Files | QDir::Hidden).isEmpty());
}
}

#ifndef Q_OS_UNIX
TEST_CASE("Creator fails closed on platforms without secure source access", "[torrent-creator]")
{
    Fixture f;
    checkRejected(f);
    REQUIRE_FALSE(QFile::exists(f.options.outputPath));
}
#else
TEST_CASE("Creator produces hybrid hashes matching libtorrent without changing sources", "[torrent-creator]")
{
    Fixture f;
    SECTION("single file") {}
    SECTION("read-only source") { REQUIRE(QFile::setPermissions(f.options.sourcePath, QFileDevice::ReadOwner)); }
    SECTION("folder with padding, nested and empty files") {
        f.options.sourcePath = f.root + "/bundle";
        writeFile(f.options.sourcePath + "/a.bin", QByteArray(65537, 'a'));
        writeFile(f.options.sourcePath + "/nested/b.bin", QByteArray(5 * 1024 * 1024 + 13, 'b'));
        writeFile(f.options.sourcePath + "/empty", {});
        writeFile(f.options.sourcePath + "/.hidden", "hidden");
    }
    SECTION("folder containing one file keeps its root") {
        f.options.sourcePath = f.root + "/bundle";
        writeFile(f.options.sourcePath + "/single.bin", "one");
    }
    SECTION("private trackers") {
        f.options.privateTorrent = true;
        f.options.trackers = {"https://tracker.invalid/announce", "udp://tracker.invalid:6969/announce"};
    }
    const auto files = lt::list_files(f.options.sourcePath.toStdString(), lt::create_torrent::no_attributes);
    struct Original { QByteArray bytes; QFileDevice::Permissions permissions; QDateTime modified; };
    std::map<QString, Original> before;
    for (const auto &file : files) {
        const auto name = QString::fromStdString(file.filename);
        const QFileInfo info(f.root + '/' + name);
        before.emplace(name, Original{readFile(info.filePath()), info.permissions(), info.lastModified()});
    }
    TorrentCreator creator;
    Result result(creator);
    qint64 lastCompleted = -1, lastTotal = -1;
    bool ownerThread = true;
    QObject::connect(&creator, &TorrentCreator::progress, &creator, [&](qint64 completed, qint64 total) {
        ownerThread = ownerThread && QThread::currentThread() == creator.thread();
        lastCompleted = completed; lastTotal = total;
    });
    REQUIRE(creator.start(f.options));
    REQUIRE(waitFor([&] { return result.done(); }));
    INFO(result.error.toStdString());
    REQUIRE(result.successes == 1);
    REQUIRE(result.failures == 0);
    REQUIRE(result.output == f.options.outputPath);
    REQUIRE(result.parent == f.root);
    REQUIRE_FALSE(creator.isRunning());
    REQUIRE(ownerThread);
    REQUIRE(lastCompleted == lastTotal);
    REQUIRE(lastTotal > 0);
    const auto loaded = lt::load_torrent_file(result.output.toStdString());
    REQUIRE(loaded.ti);
    const auto &actual = *loaded.ti;
    REQUIRE(actual.info_hashes().has_v1());
    REQUIRE(actual.info_hashes().has_v2());
    REQUIRE(actual.priv() == f.options.privateTorrent);
    REQUIRE(loaded.trackers.size() == static_cast<std::size_t>(f.options.trackers.size()));
    for (qsizetype i = 0; i < f.options.trackers.size(); ++i)
        REQUIRE(loaded.trackers[i] == f.options.trackers[i].toStdString());
    // Independent oracle uses libtorrent's disk hasher, not the creator's reader.
    lt::create_torrent expected(files, actual.piece_length());
    expected.set_priv(f.options.privateTorrent);
    lt::set_piece_hashes(expected, f.root.toStdString());
    const auto encoded = expected.generate_buf();
    const auto oracle = lt::load_torrent_buffer(lt::span<char const>(encoded.data(), encoded.size()));
    REQUIRE(oracle.ti);
    REQUIRE(actual.info_hashes() == oracle.ti->info_hashes());
    for (const auto &[path, original] : before) {
        const QFileInfo info(f.root + '/' + path);
        REQUIRE(readFile(info.filePath()) == original.bytes);
        REQUIRE(info.permissions() == original.permissions);
        REQUIRE(info.lastModified() == original.modified);
    }
    REQUIRE(QDir(f.root).entryList({"*.part*", ".torrent-create-*"}, QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Creator refuses invalid sources, unsafe paths and unbounded options", "[torrent-creator]")
{
    Fixture f;
    SECTION("missing") { f.options.sourcePath = f.root + "/missing"; }
    SECTION("relative") { f.options.sourcePath = "payload.bin"; }
    SECTION("traversal") { f.options.sourcePath = f.root + "/../" + QFileInfo(f.root).fileName() + "/payload.bin"; }
    SECTION("empty file") { writeFile(f.options.sourcePath, {}); }
    SECTION("empty directory") { f.options.sourcePath = f.root + "/empty"; REQUIRE(QDir().mkdir(f.options.sourcePath)); }
    SECTION("bad tracker scheme") { f.options.trackers = {"file:///etc/passwd"}; }
    SECTION("bad tracker host") { f.options.trackers = {"https:///announce"}; }
    SECTION("too many trackers") { for (int i = 0; i < 129; ++i) f.options.trackers << "https://tracker.invalid/announce"; }
    SECTION("missing output directory") { f.options.outputPath = f.root + "/missing/a.torrent"; }
    SECTION("unsafe filename") { f.options.sourcePath = f.root + "/bad\\name"; writeFile(f.options.sourcePath, "x"); }
    SECTION("deep tree") {
        f.options.sourcePath = f.root + "/deep";
        QString path = f.options.sourcePath;
        for (int i = 0; i < 130; ++i) path += "/d";
        writeFile(path + "/data", "x");
    }
    checkRejected(f);
    REQUIRE_FALSE(QFile::exists(f.options.outputPath));
}

TEST_CASE("Creator refuses source overlap and never overwrites output", "[torrent-creator]")
{
    Fixture f;
    const auto original = readFile(f.options.sourcePath);
    SECTION("source itself") { f.options.outputPath = f.options.sourcePath; }
    SECTION("inside selected directory") {
        f.options.sourcePath = f.root;
        f.options.outputPath = f.root + "/new.torrent";
    }
    SECTION("existing output") { writeFile(f.options.outputPath, "keep me"); }
    const bool existed = QFile::exists(f.options.outputPath);
    const auto oldOutput = existed ? readFile(f.options.outputPath) : QByteArray();
    checkRejected(f);
    if (existed) REQUIRE(readFile(f.options.outputPath) == oldOutput);
    else REQUIRE_FALSE(QFile::exists(f.options.outputPath));
    REQUIRE(readFile(f.root + "/payload.bin") == original);
}

TEST_CASE("Creator cancels before publication and can be reused", "[torrent-creator]")
{
    Fixture f;
    TorrentCreator creator;
    Result result(creator);
    int phase = 0;
    SECTION("before source scan") { phase = 0; }
    SECTION("before payload read") { phase = 1; }
    SECTION("after hashing before publication") { phase = 2; }
    auto connection = QObject::connect(&creator, &TorrentCreator::progress, &creator,
        [&](qint64 completed, qint64 total) {
            if ((phase == 0 && total == 0) || (phase == 1 && total > 0 && completed == 0) ||
                (phase == 2 && total > 0 && completed == total)) creator.cancel();
        }, Qt::DirectConnection);
    REQUIRE(creator.start(f.options));
    REQUIRE_FALSE(creator.start(f.options));
    REQUIRE(waitFor([&] { return result.done(); }));
    REQUIRE(result.cancellations == 1);
    REQUIRE(result.successes == 0);
    REQUIRE(result.failures == 0);
    REQUIRE_FALSE(QFile::exists(f.options.outputPath));
    REQUIRE(QDir(f.root).entryList({".torrent-create-*"}, QDir::Files | QDir::Hidden).isEmpty());
    REQUIRE(readFile(f.options.sourcePath) == QByteArray(70001, 'x'));
    QObject::disconnect(connection);
    result.cancellations = 0;
    REQUIRE(creator.start(f.options));
    REQUIRE(waitFor([&] { return result.done(); }));
    REQUIRE(result.successes == 1);
}

TEST_CASE("Creator detects modified source snapshots and output publication races", "[torrent-creator]")
{
    Fixture f;
    TorrentCreator creator;
    Result result(creator);
    std::function<void()> mutation;
    bool outputRace = false;
    bool afterRead = false;
    SECTION("size changed") { mutation = [&] { QFile file(f.options.sourcePath); if (file.open(QIODevice::Append)) file.write("changed"); }; }
    SECTION("same size rewritten") { mutation = [&] { QFile file(f.options.sourcePath); if (file.open(QIODevice::WriteOnly)) file.write(QByteArray(70001, 'y')); }; }
    SECTION("previously read bytes change during hashing") {
        afterRead = true;
        writeFile(f.options.sourcePath, QByteArray(8 * 1024 * 1024, 'x'));
        mutation = [&] { QFile file(f.options.sourcePath); if (file.open(QIODevice::ReadWrite)) file.write("y"); };
    }
    SECTION("tree entry added") {
        f.options.sourcePath = f.root + "/bundle";
        writeFile(f.options.sourcePath + "/a", "x");
        mutation = [&] { QFile file(f.options.sourcePath + "/new"); if (file.open(QIODevice::WriteOnly)) file.write("new"); };
    }
    SECTION("output appears while hashing") {
        outputRace = true;
        mutation = [&] { QFile file(f.options.outputPath); if (file.open(QIODevice::WriteOnly)) file.write("raced"); };
    }
    std::atomic<bool> mutated{false};
    QObject::connect(&creator, &TorrentCreator::progress, &creator, [&](qint64 completed, qint64 total) {
        if (total > 0 && (!afterRead || completed > 0) && !mutated.exchange(true)) mutation();
    }, Qt::DirectConnection);
    REQUIRE(creator.start(f.options));
    REQUIRE(waitFor([&] { return result.done(); }));
    REQUIRE(mutated);
    REQUIRE(result.failures == 1);
    REQUIRE(result.successes == 0);
    if (outputRace) REQUIRE(readFile(f.options.outputPath) == "raced");
    else REQUIRE_FALSE(QFile::exists(f.options.outputPath));
    REQUIRE(QDir(f.root).entryList({".torrent-create-*"}, QDir::Files | QDir::Hidden).isEmpty());
}

TEST_CASE("Creator destruction joins hashing and removes unpublished metadata", "[torrent-creator]")
{
    Fixture f;
    auto creator = std::make_unique<TorrentCreator>();
    std::atomic<bool> hashing{false};
    std::atomic<bool> released{false};
    QObject::connect(creator.get(), &TorrentCreator::progress, creator.get(), [&](qint64, qint64 total) {
        if (total > 0 && !hashing.exchange(true)) {
            QThread::msleep(50);
            released = true;
        }
    }, Qt::DirectConnection);
    REQUIRE(creator->start(f.options));
    REQUIRE(waitFor([&] { return hashing.load(); }));
    creator.reset();
    REQUIRE(released);
    REQUIRE_FALSE(QFile::exists(f.options.outputPath));
    QCoreApplication::processEvents();
}

TEST_CASE("Creator rejects engine-incompatible metadata before reading payload", "[torrent-creator]")
{
    Fixture f;
    SECTION("hybrid pad files exceed engine file count") {
        f.options.sourcePath = f.root + "/many";
        for (int i = 0; i < 5001; ++i)
            writeFile(f.options.sourcePath + '/' + QString::number(i), "x");
    }
    SECTION("metadata exceeds engine buffer limit") {
        QFile file(f.options.sourcePath);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.resize(qint64(512) * 1024 * 1024 * 1024));
    }
    SECTION("metadata exceeds engine piece count") {
        QFile file(f.options.sourcePath);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.resize(qint64(600) * 1024 * 1024 * 1024));
    }
    SECTION("v2 tree exceeds engine decode depth") {
        f.options.sourcePath = f.root + "/deep";
        QString path = f.options.sourcePath;
        for (int i = 0; i < 110; ++i) path += "/d";
        writeFile(path + "/data", "x");
    }
    TorrentCreator creator;
    Result result(creator);
    std::atomic<bool> hashing{false};
    QObject::connect(&creator, &TorrentCreator::progress, &creator, [&](qint64, qint64 total) {
        if (total > 0) { hashing = true; creator.cancel(); }
    }, Qt::DirectConnection);
    REQUIRE(creator.start(f.options));
    REQUIRE(waitFor([&] { return result.done(); }));
    INFO(result.error.toStdString());
    REQUIRE_FALSE(hashing);
    REQUIRE(result.failures == 1);
    REQUIRE(result.successes == 0);
    REQUIRE_FALSE(QFile::exists(f.options.outputPath));
}

#ifdef Q_OS_UNIX
TEST_CASE("Creator rejects symlinks, hardlink output aliases and special files", "[torrent-creator]")
{
    Fixture f;
    const auto original = readFile(f.options.sourcePath);
    SECTION("source symlink") {
        REQUIRE(::symlink(QFile::encodeName(f.options.sourcePath).constData(), QFile::encodeName(f.root + "/link").constData()) == 0);
        f.options.sourcePath = f.root + "/link";
    }
    SECTION("symlink in selected tree") {
        f.options.sourcePath = f.root + "/bundle";
        REQUIRE(QDir().mkdir(f.options.sourcePath));
        REQUIRE(::symlink("../payload.bin", QFile::encodeName(f.options.sourcePath + "/link").constData()) == 0);
    }
    SECTION("symlink ancestor") {
        REQUIRE(::symlink(QFile::encodeName(f.root).constData(), QFile::encodeName(f.root + "/alias").constData()) == 0);
        f.options.sourcePath = f.root + "/alias/payload.bin";
    }
    SECTION("dangling output symlink") {
        REQUIRE(::symlink("missing", QFile::encodeName(f.options.outputPath).constData()) == 0);
    }
    SECTION("output parent symlink") {
        REQUIRE(::symlink(QFile::encodeName(f.root).constData(), QFile::encodeName(f.root + "/alias").constData()) == 0);
        f.options.outputPath = f.root + "/alias/output.torrent";
    }
    SECTION("hardlink to source") {
        REQUIRE(::link(QFile::encodeName(f.options.sourcePath).constData(), QFile::encodeName(f.options.outputPath).constData()) == 0);
    }
    SECTION("fifo") {
        f.options.sourcePath = f.root + "/pipe";
        REQUIRE(::mkfifo(QFile::encodeName(f.options.sourcePath).constData(), 0600) == 0);
    }
    checkRejected(f);
    REQUIRE(readFile(f.root + "/payload.bin") == original);
}
#endif
#endif
