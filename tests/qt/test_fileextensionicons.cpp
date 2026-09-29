#include "FileExtensionIcons.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDir>
#include <QImage>

TEST_CASE("Extension icon cache normalizes remote names without opening their paths", "[qt][file-extension-icons]")
{
    FileExtensionIcons icons;
    QPixmap fallbackPixmap(32, 32);
    fallbackPixmap.fill(Qt::magenta);
    const QIcon fallback(fallbackPixmap);
    CHECK(icons.iconForFile("no-extension", fallback).cacheKey() == fallback.cacheKey());
    CHECK(icons.iconForFile("file.bad:stream", fallback).cacheKey() == fallback.cacheKey());
    CHECK(icons.iconForFile("file." + QString(200, 'a'), fallback).cacheKey() == fallback.cacheKey());
    const auto first = icons.iconForFile("/nonexistent/remote/movie.mp4", fallback);
    CHECK_FALSE(first.isNull());
    CHECK(icons.iconForFile("server\\share\\another.MP4", fallback).cacheKey() == first.cacheKey());
    CHECK_FALSE(first.pixmap(QSize(32,32)).isNull());
}

TEST_CASE("Native extension icons match registered file types", "[qt][file-extension-icons][native-icons]")
{
    if (QApplication::platformName() != "cocoa") {
        SUCCEED("Native association assertion is macOS-only");
        return;
    }
    FileExtensionIcons icons;
    const auto movie = icons.iconForFile("movie.mp4", QIcon());
    const auto document = icons.iconForFile("document.pdf", QIcon());
    REQUIRE_FALSE(movie.isNull());
    REQUIRE_FALSE(document.isNull());
    CHECK(movie.pixmap(32,32).toImage() != document.pixmap(32,32).toImage());
    CHECK(movie.pixmap(32,32).width() >= 32);
}
