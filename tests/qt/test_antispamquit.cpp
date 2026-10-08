#include "dcpp/stdinc.h"
#include "dcpp/DCContext.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/Util.h"
#include "Antispam.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <memory>

namespace {
const QStringList filenames{"antispam", "blacklist", "graylist", "whitelist"};
QByteArray readBytes(const QString& path) {
    QFile file(path); REQUIRE(file.open(QIODevice::ReadOnly)); return file.readAll();
}
void writeMarker(const QString& path) {
    QFile file(path); REQUIRE(file.exists());
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray marker("already-saved-receipt\n");
    REQUIRE(file.write(marker) == marker.size());
}
struct MinimalAntiSpam {
    QTemporaryDir home;
    dcpp::DCContext* previousCore = dcpp::getContext();
    QtContext* previousGui = qtCtx();
    dcpp::Util::PathsMap previousPaths;
    std::unique_ptr<dcpp::DCContext> core;
    std::unique_ptr<QtContext> gui;
    MinimalAntiSpam() {
        REQUIRE(home.isValid());
        for(int i = 0; i < dcpp::Util::PATH_LAST; ++i) {
            const auto key = static_cast<dcpp::Util::Paths>(i);
            previousPaths[key] = dcpp::Util::getPath(key);
        }
        auto paths = previousPaths;
        const auto root = (home.path() + '/').toStdString();
        paths[dcpp::Util::PATH_USER_CONFIG] = root;
        paths[dcpp::Util::PATH_USER_LOCAL] = root;
        paths[dcpp::Util::PATH_DOWNLOADS] = root + "downloads/";
        dcpp::Util::uninitialize(); dcpp::Util::initialize(paths);
        core = std::make_unique<dcpp::DCContext>(); core->startupMinimal();
        dcpp::setContext(core.get());
        gui = std::make_unique<QtContext>(*core);
        gui->createSettings(); gui->createAntiSpam();
        gui->antiSpam()->setPhrase("initialize"); gui->antiSpam()->setPhrase("frozen-question");
        gui->antiSpam()->addToBlack({"blocked-user"});
        gui->antiSpam()->addToGray({"gray-user"});
        gui->antiSpam()->addToWhite({"allowed-user"});
    }
    ~MinimalAntiSpam() {
        gui.reset(); core->shutdown(); core.reset();
        dcpp::setContext(previousCore); QtContextAware::setCurrent(previousGui);
        dcpp::Util::uninitialize(); dcpp::Util::initialize(previousPaths);
    }
};
}

TEST_CASE("AntiSpam retry preserves successful file commits", "[qt][antispam-file-recovery]") {
    const auto blocked = QString::fromLatin1(GENERATE("antispam", "blacklist", "graylist", "whitelist"));
    MinimalAntiSpam f;
    auto& spam = *f.gui->antiSpam();
    const auto blockedPath = f.home.filePath(blocked);
    REQUIRE(QDir().mkpath(blockedPath));
    REQUIRE_FALSE(spam.saveForQuitChecked());
    for(const auto& name : filenames)
        if(name != blocked) writeMarker(f.home.filePath(name));
    REQUIRE(QDir(blockedPath).removeRecursively());
    REQUIRE(spam.saveForQuitChecked());
    for(const auto& name : filenames) {
        INFO("blocked=" << blocked.toStdString() << " saved=" << name.toStdString());
        if(name != blocked)
            CHECK(readBytes(f.home.filePath(name)) == QByteArray("already-saved-receipt\n"));
    }
}

TEST_CASE("Frozen AntiSpam serialization and paths survive retries", "[qt][antispam-file-serialization]") {
    MinimalAntiSpam f;
    auto& spam = *f.gui->antiSpam();
    spam.setKeys({"hello", QString::fromUtf8("caf\xc3\xa9")}); spam.setAttempts(7);
    spam.addToBlack({QString::fromUtf8("caf\xc3\xa9")}); spam.clearGray();
    const auto blocked = f.home.filePath("antispam");
    REQUIRE(QDir().mkpath(blocked)); REQUIRE_FALSE(spam.saveForQuitChecked());
    REQUIRE(QDir(blocked).removeRecursively());
    dcpp::Util::PathsMap paths;
    for(int i = 0; i < dcpp::Util::PATH_LAST; ++i) {
        const auto key = static_cast<dcpp::Util::Paths>(i); paths[key] = dcpp::Util::getPath(key);
    }
    auto restore = qScopeGuard([&] { dcpp::Util::uninitialize(); dcpp::Util::initialize(paths); });
    QTemporaryDir alternate; REQUIRE(alternate.isValid());
    auto changed = paths; changed[dcpp::Util::PATH_USER_CONFIG] = (alternate.path() + '/').toStdString();
    dcpp::Util::uninitialize(); dcpp::Util::initialize(changed);
    spam.setPhrase("late-question"); spam.clearBlack(); spam.setAttempts(99);
    REQUIRE(spam.saveForQuitChecked());
    REQUIRE(QFile::exists(blocked));
    CHECK(readBytes(blocked) == QByteArray("|ANTISPAM_PHRASE|frozen-question\n|ANTISPAM_KEY|HELLO|CAF\xc3\x89|\n|ATTEMPTS|7\n"));
    CHECK(readBytes(f.home.filePath("blacklist")) == QByteArray("blocked-user\ncaf\xc3\xa9\n"));
    CHECK(readBytes(f.home.filePath("graylist")).isEmpty());
    CHECK(readBytes(f.home.filePath("whitelist")) == QByteArray("allowed-user\n"));
    for(const auto& name : filenames) CHECK_FALSE(QFile::exists(alternate.filePath(name)));
}

#include "AntispamQuitPersistence.h"

namespace {
antispamquit::Snapshot snapshot(const QTemporaryDir& directory) {
    return {QUuid("{00000000-0000-0000-0000-000000000001}"), {{{directory.filePath("antispam"), "settings\n"},
        {directory.filePath("blacklist"), "black\n"}, {directory.filePath("graylist"), "gray\n"},
        {directory.filePath("whitelist"), "white\n"}}}};
}
}

TEST_CASE("Checkpoint skips committed files", "[qt][antispam-file-checkpoint]") {
    QTemporaryDir directory; REQUIRE(directory.isValid());
    antispamquit::Checkpoint checkpoint(snapshot(directory));
    const antispamquit::Mask empty{}; CHECK(checkpoint.committed() == empty);
    const auto blocked = directory.filePath("antispam"); REQUIRE(QDir().mkpath(blocked));
    auto first = checkpoint.begin(); REQUIRE(first.has_value());
    const antispamquit::Mask partial{false, true, true, true};
    const auto receipt = antispamquit::execute(*first); CHECK(receipt.committed == partial);
    REQUIRE(checkpoint.complete(first->token, receipt) == antispamquit::Status::RetryableFailure);
    for(const auto& name : QStringList{"blacklist", "graylist", "whitelist"}) writeMarker(directory.filePath(name));
    REQUIRE(QDir(blocked).removeRecursively());
    auto second = checkpoint.begin(); REQUIRE(second.has_value()); CHECK(second->committed == partial);
    REQUIRE(checkpoint.complete(second->token, antispamquit::execute(*second)) == antispamquit::Status::Ready);
    for(const auto& name : QStringList{"blacklist", "graylist", "whitelist"})
        CHECK(readBytes(directory.filePath(name)) == QByteArray("already-saved-receipt\n"));
    CHECK(readBytes(blocked) == QByteArray("settings\n"));
    CHECK_FALSE(checkpoint.begin().has_value());
}

TEST_CASE("Checkpoint accumulates partial receipts", "[qt][antispam-file-checkpoint]") {
    QTemporaryDir directory; REQUIRE(directory.isValid());
    antispamquit::Checkpoint checkpoint(snapshot(directory));
    REQUIRE(QDir().mkpath(directory.filePath("blacklist"))); REQUIRE(QDir().mkpath(directory.filePath("graylist")));
    auto first = checkpoint.begin(); REQUIRE(first.has_value());
    REQUIRE(checkpoint.complete(first->token, antispamquit::execute(*first)) == antispamquit::Status::RetryableFailure);
    const antispamquit::Mask one{true, false, false, true}; CHECK(checkpoint.committed() == one);
    writeMarker(directory.filePath("antispam")); writeMarker(directory.filePath("whitelist"));
    REQUIRE(QDir(directory.filePath("blacklist")).removeRecursively());
    auto second = checkpoint.begin(); REQUIRE(second.has_value());
    REQUIRE(checkpoint.complete(second->token, antispamquit::execute(*second)) == antispamquit::Status::RetryableFailure);
    const antispamquit::Mask two{true, true, false, true}; CHECK(checkpoint.committed() == two);
    writeMarker(directory.filePath("blacklist")); REQUIRE(QDir(directory.filePath("graylist")).removeRecursively());
    auto third = checkpoint.begin(); REQUIRE(third.has_value());
    REQUIRE(checkpoint.complete(third->token, antispamquit::execute(*third)) == antispamquit::Status::Ready);
    for(const auto& name : QStringList{"antispam", "blacklist", "whitelist"})
        CHECK(readBytes(directory.filePath(name)) == QByteArray("already-saved-receipt\n"));
    CHECK(readBytes(directory.filePath("graylist")) == QByteArray("gray\n"));
}

TEST_CASE("Checkpoint rejects duplicate admission", "[qt][antispam-file-checkpoint]") {
    QTemporaryDir directory; REQUIRE(directory.isValid());
    antispamquit::Checkpoint checkpoint(snapshot(directory));
    auto attempt = checkpoint.begin(); REQUIRE(attempt.has_value());
    CHECK(checkpoint.status() == antispamquit::Status::Pending);
    CHECK_FALSE(checkpoint.begin().has_value());
    REQUIRE(checkpoint.complete(attempt->token, antispamquit::execute(*attempt)) == antispamquit::Status::Ready);
    CHECK_FALSE(checkpoint.begin().has_value());
}

TEST_CASE("Checkpoint rejects foreign and regressing receipts", "[qt][antispam-file-checkpoint]") {
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    QTemporaryDir directory; REQUIRE(directory.isValid());
    antispamquit::Checkpoint checkpoint(snapshot(directory));
    REQUIRE(QDir().mkpath(directory.filePath("antispam")));
    auto first = checkpoint.begin(); REQUIRE(first.has_value());
    const auto partial = antispamquit::execute(*first);
    REQUIRE(checkpoint.complete(first->token, partial) == antispamquit::Status::RetryableFailure);
    const antispamquit::Mask previous{false, true, true, true}; REQUIRE(checkpoint.committed() == previous);
    auto attempt = checkpoint.begin(); REQUIRE(attempt.has_value());
    std::optional<antispamquit::Receipt> forged = antispamquit::Receipt{attempt->token, previous};
    auto caller = attempt->token;
    switch(mode) {
    case 0: forged.reset(); break;
    case 1: forged->token.snapshot = QUuid(); break;
    case 2: forged->token.attempt = QUuid(); break;
    case 3: forged->committed[3] = false; break;
    case 4: caller = first->token; break;
    case 5: forged = partial; break;
    case 6: forged->token.snapshot = QUuid("{00000000-0000-0000-0000-000000000099}"); break;
    case 7: forged->token.attempt = QUuid("{00000000-0000-0000-0000-000000000099}"); break;
    }
    CHECK(checkpoint.complete(caller, forged) == antispamquit::Status::TerminalFailure);
    CHECK(checkpoint.committed() == previous); CHECK_FALSE(checkpoint.begin().has_value());
}

TEST_CASE("Checkpoint retains state after unstarted cancellation", "[qt][antispam-file-checkpoint]") {
    QTemporaryDir directory; REQUIRE(directory.isValid());
    antispamquit::Checkpoint checkpoint(snapshot(directory));
    REQUIRE(QDir().mkpath(directory.filePath("antispam")));
    auto first = checkpoint.begin(); REQUIRE(first.has_value());
    REQUIRE(checkpoint.complete(first->token, antispamquit::execute(*first)) == antispamquit::Status::RetryableFailure);
    const antispamquit::Mask previous{false, true, true, true};
    auto canceled = checkpoint.begin(); REQUIRE(canceled.has_value());
    REQUIRE(checkpoint.cancelUnstarted(canceled->token)); CHECK(checkpoint.committed() == previous);
    auto next = checkpoint.begin(); REQUIRE(next.has_value()); CHECK(next->committed == previous);
    CHECK(next->token.attempt != canceled->token.attempt);
    CHECK_FALSE(checkpoint.cancelUnstarted(canceled->token));
    CHECK(checkpoint.status() == antispamquit::Status::TerminalFailure);
    CHECK(checkpoint.committed() == previous);
}

TEST_CASE("Null checkpoint identity cannot write files", "[qt][antispam-file-checkpoint]") {
    QTemporaryDir directory; REQUIRE(directory.isValid());
    auto empty = snapshot(directory); empty.id = QUuid();
    antispamquit::Checkpoint checkpoint(std::move(empty));
    CHECK(checkpoint.status() == antispamquit::Status::TerminalFailure);
    CHECK_FALSE(checkpoint.begin().has_value());
    for(const auto& name : filenames) CHECK_FALSE(QFile::exists(directory.filePath(name)));
}

TEST_CASE("Checkpoint duplicate completion is terminal", "[qt][antispam-file-checkpoint]") {
    QTemporaryDir directory; REQUIRE(directory.isValid());
    antispamquit::Checkpoint checkpoint(snapshot(directory));
    auto attempt = checkpoint.begin(); REQUIRE(attempt.has_value());
    const auto receipt = antispamquit::execute(*attempt);
    REQUIRE(checkpoint.complete(attempt->token, receipt) == antispamquit::Status::Ready);
    CHECK(checkpoint.complete(attempt->token, receipt) == antispamquit::Status::TerminalFailure);
    const antispamquit::Mask full{true, true, true, true}; CHECK(checkpoint.committed() == full);
    CHECK_FALSE(checkpoint.begin().has_value());
}
