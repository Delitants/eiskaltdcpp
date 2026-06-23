#include <catch2/catch_test_macros.hpp>

#include "DownloadToSettings.h"

namespace {

QString encodeLiteral(const char* value)
{
    return QString::fromLatin1(QByteArray(value).toBase64());
}

} // namespace

TEST_CASE("Download-to settings encode and decode empty lists", "[qt][downloadto]")
{
    REQUIRE(decodeDownloadTo({}, {}).isEmpty());

    const auto encoded = encodeDownloadTo({});
    REQUIRE(encoded.first.isEmpty());
    REQUIRE(encoded.second.isEmpty());
}

TEST_CASE("Download-to settings preserve the legacy base64 newline format", "[qt][downloadto]")
{
    const QList<DownloadToEntry> entries = { { "/tmp/downloads", "Temporary" } };
    const auto encoded = encodeDownloadTo(entries);

    REQUIRE(encoded.first == encodeLiteral("/tmp/downloads\n"));
    REQUIRE(encoded.second == encodeLiteral("Temporary\n"));
    REQUIRE(decodeDownloadTo(encoded.first, encoded.second) == entries);
}

TEST_CASE("Download-to settings round trip Unicode paths and aliases", "[qt][downloadto]")
{
    const QList<DownloadToEntry> entries = {
        { QString::fromUtf8("/Завантаження/音楽"), QString::fromUtf8("Музика 🎵") }
    };

    const auto encoded = encodeDownloadTo(entries);
    REQUIRE(decodeDownloadTo(encoded.first, encoded.second) == entries);
}

TEST_CASE("Download-to settings reject mismatched path and alias counts", "[qt][downloadto]")
{
    const QString paths = QString::fromLatin1(QByteArray("/one\n/two\n").toBase64());
    const QString aliases = QString::fromLatin1(QByteArray("One\n").toBase64());

    REQUIRE(decodeDownloadTo(paths, aliases).isEmpty());
}

TEST_CASE("Download-to deletion re-encodes only remaining rows", "[qt][downloadto]")
{
    QList<DownloadToEntry> entries = {
        { "/one", "One" },
        { "/two", "Two" },
        { "/three", "Three" }
    };
    entries.removeAt(1);

    const auto encoded = encodeDownloadTo(entries);

    REQUIRE(encoded.first == encodeLiteral("/one\n/three\n"));
    REQUIRE(encoded.second == encodeLiteral("One\nThree\n"));
    REQUIRE(decodeDownloadTo(encoded.first, encoded.second) == entries);
}
