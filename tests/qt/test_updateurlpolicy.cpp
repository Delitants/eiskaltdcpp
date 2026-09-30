#include "updates/UpdateUrlPolicy.h"
#include <catch2/catch_test_macros.hpp>

using updates::ResourceKind;

TEST_CASE("Updater URLs stay within the maintained feed and release namespaces", "[updates][update-transport][update-url]") {
    const QString feed = "https://raw.githubusercontent.com/Delitants/eiskaltdcpp/codex-2.5.2-release/updates/arm64.xml";
    const QString archive = "https://github.com/Delitants/eiskaltdcpp/releases/download/v3.0.0-pre.1/Eiskalt.zip";
    CHECK(updates::allowedUrl(QUrl(feed), ResourceKind::Feed));
    CHECK(updates::allowedUrl(QUrl(feed), ResourceKind::Feed, true));
    CHECK(updates::allowedUrl(QUrl(archive), ResourceKind::Archive));
    CHECK_FALSE(updates::allowedUrl(QUrl(feed), ResourceKind::Archive));
    CHECK_FALSE(updates::allowedUrl(QUrl(archive), ResourceKind::Feed));
    for(const QString host : {"release-assets.githubusercontent.com", "objects.githubusercontent.com"}) {
        const QUrl url("https://" + host + "/asset?id=signed%2Bquery");
        CHECK_FALSE(updates::allowedUrl(url, ResourceKind::Archive));
        CHECK(updates::allowedUrl(url, ResourceKind::Archive, true));
        CHECK_FALSE(updates::allowedUrl(url, ResourceKind::Feed, true));
    }
    for(const QString url : {
        "http://github.com/Delitants/eiskaltdcpp/releases/download/tag/app.zip",
        "https://user:pass@github.com/Delitants/eiskaltdcpp/releases/download/tag/app.zip",
        "https://github.com:444/Delitants/eiskaltdcpp/releases/download/tag/app.zip",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/tag/app.zip#fragment",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/tag/app.zip#",
        "https://github.com/Delitants/another/releases/download/tag/app.zip",
        "https://github.com/Delitants/eiskaltdcpp/releases/downloaded/tag/app.zip",
        "https://github.com.evil.invalid/Delitants/eiskaltdcpp/releases/download/tag/app.zip",
        "https://release-assets.githubusercontent.com.evil.invalid/asset",
        "https://127.0.0.1/Delitants/eiskaltdcpp/releases/download/tag/app.zip",
        "https://[::1]/asset", "file:///tmp/app.zip", "https://localhost/asset",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/../../other",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/%2e%2e/other",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/tag%2fother/app.zip",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/tag%5cother/app.zip",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/tag/app%0d%0a.zip",
        "https://github.com/Delitants/eiskaltdcpp/releases/download/tag/./app.zip"
    }) {
        CAPTURE(url.toStdString());
        CHECK_FALSE(updates::allowedUrl(QUrl(url), ResourceKind::Archive, true));
    }
}

TEST_CASE("Updater destination addresses exclude local and special-purpose networks", "[updates][update-transport][update-url]") {
    for(const QString address : {"1.1.1.1", "8.8.8.8", "140.82.114.3", "2606:4700:4700::1111", "2001:4860:4860::8888"}) {
        CAPTURE(address.toStdString());
        CHECK(updates::publicAddress(address));
    }
    for(const QString address : {
        "", "github.com", "127.1", "2130706433", " 1.1.1.1", "0.0.0.0", "0.1.2.3",
        "10.0.0.1", "100.64.0.1", "100.127.255.255", "127.0.0.1", "169.254.1.1",
        "172.16.0.1", "172.31.255.255", "192.0.0.1", "192.0.2.1", "192.88.99.1",
        "192.168.0.1", "198.18.0.1", "198.19.255.255", "198.51.100.1", "203.0.113.1",
        "224.0.0.1", "240.0.0.1", "255.255.255.255", "::", "::1", "fc00::1", "fdff::1",
        "fe80::1", "fe80::1%en0", "ff02::1", "2001:db8::1", "2002:7f00:1::1", "64:ff9b::7f00:1",
        "::ffff:127.0.0.1", "::ffff:8.8.8.8", "3fff::1"
    }) {
        CAPTURE(address.toStdString());
        CHECK_FALSE(updates::publicAddress(address));
    }
}
