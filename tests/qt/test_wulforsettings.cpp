/*
 * Tests for WulforSettings — Qt key-value settings store.
 * Requires dcpp TestContext (Util::getPath used in constructor).
 */

#include <catch2/catch_test_macros.hpp>
#include <QSignalSpy>
#include <QString>
#include <QVariant>

#include "WulforSettings.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include "TabButton.h"
#include "AppIconTheme.h"
#include <QLabel>
#include "../TestContext.h"

#include <memory>

// Lazy-init dcpp context + QtContext
static std::unique_ptr<dcpp::test::TestContext> g_tc;
static std::unique_ptr<QtContext> g_qtCtx;

static void cleanupQtContext() {
    g_qtCtx.reset();     // ~QtContext auto-deregisters
}

static void ensureSettings() {
    if (!g_tc)
        g_tc = std::make_unique<dcpp::test::TestContext>();
    if (!qtCtx()) {
        g_qtCtx = std::make_unique<QtContext>(*dcpp::getContext());  // auto-registers
        g_qtCtx->createSettings();
        std::atexit(cleanupQtContext);
    }
}

// ─── Construction ───────────────────────────────────────────────────────

TEST_CASE("WulforSettings: exists after context creation", "[qt][wulforsettings]") {
    ensureSettings();
    REQUIRE(qtCtx()->settings() != nullptr);
}

TEST_CASE("Multi-line tab icons retain their logical size on Retina screens", "[qt][tab-icons][icontheme]") {
    ensureSettings();
    TabButton button;
    const auto source = app_icon_theme::icon(app_icon_theme::resourcePath("reborn"),
        "torrent", button.palette());
    button.setWidgetIcon(source);
    auto *iconLabel = button.findChild<QLabel*>("tabWidgetIcon");
    REQUIRE(iconLabel);
    const auto rendered = iconLabel->pixmap();
    CHECK(rendered.deviceIndependentSize() == QSizeF(16, 16));
    CHECK(rendered.toImage() == source.pixmap(QSize(16, 16), button.devicePixelRatioF()).toImage());
    iconLabel->ensurePolished();
    CHECK(iconLabel->contentsRect().size() == QSize(16, 16));
}

TEST_CASE("WulforSettings: Reborn is the default toolbar theme", "[qt][wulforsettings][icontheme]") {
    ensureSettings();
    qtCtx()->settings()->load();
    REQUIRE(qtCtx()->settings()->getStr(WS_APP_ICONTHEME) == "reborn");
    REQUIRE(qtCtx()->settings()->getStr(WS_APP_USERTHEME) == "apex");
}

TEST_CASE("WulforSettings: only the old Apex toolbar selection migrates", "[qt][wulforsettings][icontheme]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();
    const QString previous = ws->getStr(WS_APP_ICONTHEME);
    for (const auto &id : {"apex", "contour", "faenza", "default"}) {
        ws->setStr(WS_APP_ICONTHEME, id);
        ws->load();
        CHECK(ws->getStr(WS_APP_ICONTHEME) == (QString(id) == "apex" ? "reborn" : id));
        CHECK(ws->getStr(WS_APP_USERTHEME) == "apex");
    }
    ws->setStr(WS_APP_ICONTHEME, previous);
}

// ─── String get/set ─────────────────────────────────────────────────────

TEST_CASE("WulforSettings: setStr and getStr", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    ws->setStr("test-str-key", "hello_world");
    REQUIRE(ws->getStr("test-str-key") == "hello_world");
}

TEST_CASE("WulforSettings: getStr default value", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    REQUIRE(ws->getStr("nonexistent-str-key", "default_val") == "default_val");
}

// ─── Int get/set ────────────────────────────────────────────────────────

TEST_CASE("WulforSettings: setInt and getInt", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    ws->setInt("test-int-key", 42);
    REQUIRE(ws->getInt("test-int-key") == 42);
}

TEST_CASE("WulforSettings: getInt default value", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    REQUIRE(ws->getInt("nonexistent-int-key", -99) == -99);
}

// ─── Bool get/set ───────────────────────────────────────────────────────

TEST_CASE("WulforSettings: setBool and getBool", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    ws->setBool("test-bool-key", true);
    REQUIRE(ws->getBool("test-bool-key") == true);

    ws->setBool("test-bool-key", false);
    REQUIRE(ws->getBool("test-bool-key") == false);
}

TEST_CASE("WulforSettings: getBool default value", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    REQUIRE(ws->getBool("nonexistent-bool-key", true) == true);
    REQUIRE(ws->getBool("nonexistent-bool-key-2", false) == false);
}

// ─── QVariant get/set ───────────────────────────────────────────────────

TEST_CASE("WulforSettings: setVar and getVar", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    QVariant v(QStringList({"a", "b", "c"}));
    ws->setVar("test-var-key", v);

    QVariant result = ws->getVar("test-var-key");
    REQUIRE(result.toStringList() == QStringList({"a", "b", "c"}));
}

TEST_CASE("WulforSettings: getVar default value", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    QVariant def(123);
    REQUIRE(ws->getVar("nonexistent-var-key", def).toInt() == 123);
}

// ─── hasKey ─────────────────────────────────────────────────────────────

TEST_CASE("WulforSettings: hasKey checks intmap/strmap (old config)", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    // hasKey only checks intmap/strmap (old XML config), not QSettings
    // setStr goes to QSettings, so hasKey won't find it
    ws->setStr("has-key-test", "value");
    // This is expected behavior — hasKey checks the legacy maps only
    REQUIRE_FALSE(ws->hasKey("has-key-test"));
}

// ─── Signals ────────────────────────────────────────────────────────────

TEST_CASE("WulforSettings: strValueChanged signal on setStr", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    QSignalSpy spy(ws, &WulforSettings::strValueChanged);
    ws->setStr("signal-test-str", "new_value");

    REQUIRE(spy.count() >= 1);
    auto args = spy.last();
    REQUIRE(args.at(0).toString() == "signal-test-str");
    REQUIRE(args.at(1).toString() == "new_value");
}

TEST_CASE("WulforSettings: intValueChanged signal on setInt", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    QSignalSpy spy(ws, &WulforSettings::intValueChanged);
    ws->setInt("signal-test-int", 77);

    REQUIRE(spy.count() >= 1);
    auto args = spy.last();
    REQUIRE(args.at(0).toString() == "signal-test-int");
    REQUIRE(args.at(1).toInt() == 77);
}

TEST_CASE("WulforSettings: varValueChanged signal on setVar", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    QSignalSpy spy(ws, &WulforSettings::varValueChanged);
    ws->setVar("signal-test-var", QVariant("test_val"));

    REQUIRE(spy.count() >= 1);
    auto args = spy.last();
    REQUIRE(args.at(0).toString() == "signal-test-var");
}

// ─── parseCmd ───────────────────────────────────────────────────────────

TEST_CASE("WulforSettings: parseCmd sets a key-value pair", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    // parseCmd("key value", res) → sets key=value in QSettings
    QString result;
    ws->parseCmd("test-parse-key test-parse-value", result);

    // Verify the setting was stored
    REQUIRE(ws->getStr("test-parse-key") == "test-parse-value");
    // Result is a confirmation message
    REQUIRE(!result.isEmpty());
}

TEST_CASE("WulforSettings: parseCmd with one arg returns current value", "[qt][wulforsettings]") {
    ensureSettings();
    auto *ws = qtCtx()->settings();

    ws->setStr("my-setting", "my-value");
    QString result;
    ws->parseCmd("my-setting", result);
    // Returns "GUI setting my-setting: my-value"
    REQUIRE(!result.isEmpty());
    REQUIRE(result.contains("my-value"));
}
