#include "AppIconTheme.h"

#include <catch2/catch_test_macros.hpp>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QSvgRenderer>
#include <QTemporaryDir>

namespace {
QPalette background(const QColor &color)
{
    QPalette result;
    result.setColor(QPalette::Window, color);
    return result;
}
}

TEST_CASE("Icon themes: four approved styles with Reborn fourth", "[qt][icontheme]")
{
    CHECK(app_icon_theme::modernIds() == QStringList{"prism", "contour", "slate", "reborn"});
    CHECK(app_icon_theme::canonicalId("") == "reborn");
    CHECK(app_icon_theme::canonicalId(" Apex ") == "reborn");
    CHECK(app_icon_theme::canonicalId("default") == "default");
    CHECK(app_icon_theme::canonicalId("faenza") == "faenza");
}

TEST_CASE("Icon themes: all bundled SVGs render at toolbar size", "[qt][icontheme]")
{
    int expectedCount = -1;
    for (const auto &theme : app_icon_theme::modernIds()) {
        const auto root = app_icon_theme::resourcePath(theme);
        QDir dir(root);
        const auto names = dir.entryList({"*.svg"}, QDir::Files);
        INFO(theme.toStdString());
        REQUIRE(names.size() >= 75);
        if (expectedCount < 0)
            expectedCount = names.size();
        CHECK(names.size() == expectedCount);
        for (const auto &name : names) {
            const auto stem = QFileInfo(name).completeBaseName();
            INFO(name.toStdString());
            for (const auto &color : {QColor(Qt::white), QColor("#25282d")}) {
                const auto palette = background(color);
                const auto path = app_icon_theme::assetPath(root, stem, palette);
                CHECK(path.contains("/dark/") == app_icon_theme::isDark(palette));
                QSvgRenderer renderer(path);
                REQUIRE(renderer.isValid());
                for (const qreal dpr : {1.0, 2.0}) {
                    auto rendered = app_icon_theme::pixmap(root, stem, palette, 28, dpr);
                    REQUIRE_FALSE(rendered.isNull());
                    CHECK(rendered.size() == QSize(28 * dpr, 28 * dpr));
                    CHECK(rendered.devicePixelRatio() == dpr);
                    const auto image = rendered.toImage();
                    bool hasInk = false;
                    bool hasTransparency = false;
                    for (int y = 0; y < image.height(); ++y) {
                        for (int x = 0; x < image.width(); ++x) {
                            const auto alpha = image.pixelColor(x, y).alpha();
                            hasInk |= alpha > 0;
                            hasTransparency |= alpha == 0;
                        }
                    }
                    CHECK(hasInk);
                    CHECK(hasTransparency);
                }
                CHECK_FALSE(app_icon_theme::icon(root, stem, palette).pixmap(28, 28).isNull());
            }
        }
    }
}

TEST_CASE("Icon themes: missing files fail safely and brand icons are unchanged", "[qt][icontheme]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto palette = background(Qt::white);
    CHECK(app_icon_theme::pixmap(directory.path(), "missing", palette, 28, 1).isNull());
    CHECK(app_icon_theme::pixmap(directory.path(), "missing", palette, -1, 1).isNull());
    CHECK(app_icon_theme::pixmap(directory.path(), "missing", palette, 28, 0).isNull());
    QPixmap fallback(16, 16);
    fallback.fill(Qt::red);
    CHECK_FALSE(app_icon_theme::icon(directory.path(), "missing", palette, QIcon(fallback)).isNull());
    for (const auto &theme : app_icon_theme::modernIds()) {
        for (const auto &name : {"icon_appl_big", "icon_msg_big", "qt-logo"}) {
            const auto path = app_icon_theme::assetPath(app_icon_theme::resourcePath(theme), name, palette);
            CHECK(path == QString(":/icon-themes/apex-brand/") + name + ".png");
            CHECK_FALSE(app_icon_theme::pixmap(app_icon_theme::resourcePath(theme), name, palette, 128, 2).isNull());
        }
    }
#ifdef USE_TORRENT
    QFile torrent(":/torrent/torrent.png");
    REQUIRE(torrent.open(QIODevice::ReadOnly));
    CHECK(QCryptographicHash::hash(torrent.readAll(), QCryptographicHash::Sha256).toHex() ==
          "e83ed3f29d332ef6e9fe71b114b8b85325cc695cae3a9080271a4bd7022c1eee");
#endif
}

TEST_CASE("Icon themes: external notifications receive a real cached image file", "[qt][icontheme]")
{
    const QString resource(":/icon-themes/apex-brand/icon_appl_big.png");
    const auto local = app_icon_theme::localFilePath(resource);
    REQUIRE_FALSE(local.isEmpty());
    CHECK_FALSE(local.startsWith(QLatin1Char(':')));
    CHECK(app_icon_theme::localFilePath(resource) == local);
    CHECK(app_icon_theme::localFilePath(local) == local);
    CHECK(app_icon_theme::localFilePath(":/missing.png").isEmpty());
    QFile original(resource);
    QFile exported(local);
    REQUIRE(original.open(QIODevice::ReadOnly));
    REQUIRE(exported.open(QIODevice::ReadOnly));
    CHECK(original.readAll() == exported.readAll());
}

TEST_CASE("Icon themes: normal and highlighted states render vectors at the requested resolution", "[qt][icontheme]")
{
    for (const auto &theme : app_icon_theme::modernIds()) {
        INFO(theme.toStdString());
        for (const auto &color : {QColor(Qt::white), QColor("#25282d")}) {
            const auto palette = background(color);
            const auto root = app_icon_theme::resourcePath(theme);
            const auto normal = app_icon_theme::assetPath(root, "transfer", palette);
            const auto active = app_icon_theme::assetPath(root, "transfer-highlight", palette);
            const auto icon = app_icon_theme::highlightedIcon(normal, active);
            for (const int side : {16, 22, 28, 32, 64}) {
                for (const qreal dpr : {1.0, 1.5, 2.0}) {
                    const QSize logical(side, side);
                    for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected}) {
                        for (const auto state : {QIcon::Off, QIcon::On}) {
                            const auto rendered = icon.pixmap(logical, dpr, mode, state);
                            CHECK(rendered.size() == logical * dpr);
                            CHECK(rendered.devicePixelRatio() == dpr);
                            const auto expected = QIcon(mode == QIcon::Normal && state == QIcon::Off
                                ? normal : active).pixmap(logical, dpr);
                            CHECK(rendered.toImage() == expected.toImage());
                        }
                    }
                    CHECK_FALSE(icon.pixmap(logical, dpr, QIcon::Disabled).isNull());
                }
            }
        }
    }
}
