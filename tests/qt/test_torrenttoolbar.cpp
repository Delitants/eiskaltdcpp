#include <catch2/catch_test_macros.hpp>
#include "TorrentToolbar.h"
#include "AppIconTheme.h"
#include <QApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>

namespace {
QString encode(const QStringList &actions)
{
    return QString::fromLatin1(actions.join(QLatin1Char(';')).toUtf8().toBase64());
}
}

TEST_CASE("Torrent toolbar upgrade preserves order and adds beside Download Queue", "[qt][torrent-toolbar]")
{
    const QStringList previous {"toolsSearch", "separator3", "toolsDownloadQueue", "toolsFinishedDownloads"};
    const QStringList expected {"toolsSearch", "separator3", "toolsDownloadQueue", "toolsTorrents", "toolsFinishedDownloads"};
    REQUIRE(torrent_toolbar::restoreActions(encode(previous), false) == expected);
}

TEST_CASE("Torrent toolbar upgrade appends when Download Queue is hidden", "[qt][torrent-toolbar]")
{
    const QStringList previous {"toolsSearch", "toolsOptions"};
    const QStringList expected {"toolsSearch", "toolsOptions", "toolsTorrents"};
    REQUIRE(torrent_toolbar::restoreActions(encode(previous), false) == expected);
}

TEST_CASE("Torrent toolbar upgrade retains an existing Torrent position without duplication", "[qt][torrent-toolbar]")
{
    const QStringList previous {"toolsTorrents", "toolsSearch", "toolsDownloadQueue"};
    REQUIRE(torrent_toolbar::restoreActions(encode(previous), false) == previous);
    REQUIRE(torrent_toolbar::restoreActions(encode(previous), true) == previous);
}

TEST_CASE("Fresh toolbar settings continue to use the full default action list", "[qt][torrent-toolbar]")
{
    REQUIRE(torrent_toolbar::restoreActions(QString(), false).isEmpty());
    REQUIRE(torrent_toolbar::restoreActions(QString(), true).isEmpty());
}

TEST_CASE("Hiding Torrents after the one-time upgrade survives settings reload", "[qt][torrent-toolbar]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QStringList customized {"toolsOptions", "toolsDownloadQueue"};
    {
        QSettings settings(dir.filePath("toolbar.ini"), QSettings::IniFormat);
        settings.setValue(torrent_toolbar::introducedKey(), true);
        settings.setValue("actions", encode(customized));
        settings.sync();
        REQUIRE(settings.status() == QSettings::NoError);
    }
    QSettings restored(dir.filePath("toolbar.ini"), QSettings::IniFormat);
    REQUIRE(torrent_toolbar::restoreActions(restored.value("actions").toString(),
            restored.value(torrent_toolbar::introducedKey()).toBool()) == customized);
}

TEST_CASE("Torrent icon is embedded with transparency and renders at toolbar sizes", "[qt][torrent-toolbar]")
{
    const QImage source(QStringLiteral(":/torrent/torrent.png"));
    REQUIRE_FALSE(source.isNull());
    REQUIRE(source.hasAlphaChannel());
    REQUIRE(source.pixelColor(0, 0).alpha() == 0);
    const QIcon icon = torrent_toolbar::icon();
    REQUIRE_FALSE(icon.isNull());
    for (const int size : {16, 24, 28, 56}) {
        const QPixmap pixmap = icon.pixmap(size, size);
        REQUIRE_FALSE(pixmap.isNull());
        REQUIRE(pixmap.deviceIndependentSize() == QSizeF(size, size));
        REQUIRE(pixmap.hasAlphaChannel());
    }
}

TEST_CASE("Torrent toolbar follows each active theme instead of standalone artwork", "[qt][torrent-toolbar][icontheme]")
{
    const auto previous = app_icon_theme::activePath();
    struct RestoreTheme {
        QString path;
        ~RestoreTheme() { app_icon_theme::setActivePath(path); }
    } restore{previous};
    for (const auto &theme : app_icon_theme::modernIds()) {
        const auto root = app_icon_theme::resourcePath(theme);
        app_icon_theme::setActivePath(root);
        INFO(theme.toStdString());
        const auto expected = app_icon_theme::icon(root, "torrent", qApp->palette());
        REQUIRE_FALSE(expected.isNull());
        for (const qreal dpr : {1.0, 2.0}) {
            const auto actual = torrent_toolbar::icon().pixmap(QSize(28, 28), dpr);
            CHECK(actual.size() == QSize(28 * dpr, 28 * dpr));
            CHECK(actual.toImage() == expected.pixmap(QSize(28, 28), dpr).toImage());
        }
    }
    app_icon_theme::setActivePath(":/missing-legacy-theme");
    CHECK(torrent_toolbar::icon().pixmap(28, 28).toImage() ==
          QIcon(":/torrent/torrent.png").pixmap(28, 28).toImage());
}
