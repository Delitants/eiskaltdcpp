#include <catch2/catch_test_macros.hpp>
#include "TorrentSharing.h"
#include <QTextEdit>
#include <QUrl>
#include <QUrlQuery>

TEST_CASE("DC torrent magnets use only published selected completed files", "[torrent-sharing]")
{
    eiskalt::torrent::Job job;
    job.id = "job";
    job.revision = 7;
    job.complete = true;
    job.files = {{0, "folder/a & b.mkv", 42, true}, {1, "folder/skip.mkv", 5, false}};
    int lookups = 0;
    auto lookup = [&](const auto &) { ++lookups; return QString(39, 'A'); };
    const auto files = torrent_sharing::dcFiles(job, lookup);
    REQUIRE(files.size() == 1);
    REQUIRE(lookups == 1);
    CHECK(files[0].revision == 7);
    const QUrlQuery query{QUrl(files[0].magnet)};
    CHECK(query.queryItemValue("xt") == "urn:tree:tiger:" + QString(39, 'A'));
    CHECK(query.queryItemValue("dn", QUrl::FullyDecoded) == "a & b.mkv");
    CHECK(query.queryItemValue("xl") == "42");
    job.privateTorrent = true;
    REQUIRE(torrent_sharing::dcFiles(job, lookup).isEmpty());
    CHECK(lookups == 1);
    job.privateTorrent = false;
    job.complete = false;
    REQUIRE(torrent_sharing::dcFiles(job, lookup).isEmpty());
    job.complete = true;
    job.error = "invalid";
    REQUIRE(torrent_sharing::dcFiles(job, lookup).isEmpty());
    job.error.clear();
    REQUIRE(torrent_sharing::dcFiles(job, [](const auto &) { return QString(); }).isEmpty());
}

TEST_CASE("Magnet sharing preserves draft and selection and is one undoable insertion", "[torrent-sharing]")
{
    QTextEdit editor;
    editor.setPlainText("Draft <not HTML>");
    editor.selectAll();
    REQUIRE(torrent_sharing::appendDraft(&editor, {"magnet:?xt=first", "magnet:?xt=second"}));
    CHECK(editor.toPlainText() == "Draft <not HTML>\nmagnet:?xt=first\nmagnet:?xt=second");
    editor.undo();
    CHECK(editor.toPlainText() == "Draft <not HTML>");
    REQUIRE_FALSE(torrent_sharing::appendDraft(nullptr, {"magnet:?xt=first"}));
    REQUIRE_FALSE(torrent_sharing::appendDraft(&editor, {}));
}
