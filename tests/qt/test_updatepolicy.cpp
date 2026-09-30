#include "updates/UpdatePolicy.h"
#include <catch2/catch_test_macros.hpp>
using namespace updates;

TEST_CASE("Update policy has exact daily boundaries and permits explicit manual checks", "[updates][update-policy]") {
    UpdatePreferences p;
    CHECK(p.checks); CHECK(p.notifications); CHECK_FALSE(p.autoInstall);
    const auto zero = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
    CHECK(UpdatePolicy::mayCheck(p, CheckKind::Automatic, zero, {}, false));
    CHECK_FALSE(UpdatePolicy::mayCheck(p, CheckKind::Automatic, zero.addSecs(86399), zero, false));
    CHECK(UpdatePolicy::mayCheck(p, CheckKind::Automatic, zero.addSecs(86400), zero, false));
    CHECK_FALSE(UpdatePolicy::mayCheck(p, CheckKind::Automatic, zero.addSecs(-1), zero, false));
    CHECK_FALSE(UpdatePolicy::mayCheck(p, CheckKind::Manual, zero, {}, true));
    p.checks = false;
    CHECK_FALSE(UpdatePolicy::mayCheck(p, CheckKind::Automatic, zero, {}, false));
    CHECK(UpdatePolicy::mayCheck(p, CheckKind::Manual, zero, zero, false));
    CHECK_FALSE(UpdatePolicy::mayCheck(p, CheckKind::Automatic, {}, {}, false));
}
TEST_CASE("Install policy requires normal quit and binds manual consent to one artifact", "[updates][update-policy]") {
    UpdatePreferences p;
    CHECK_FALSE(UpdatePolicy::mayInstall(p, true, "2:digest", {}));
    CHECK_FALSE(UpdatePolicy::mayInstall(p, false, "2:digest", "2:digest"));
    CHECK(UpdatePolicy::mayInstall(p, true, "2:digest", "2:digest"));
    CHECK_FALSE(UpdatePolicy::mayInstall(p, true, "3:digest", "2:digest"));
    CHECK_FALSE(UpdatePolicy::mayInstall(p, true, {}, {}));
    p.autoInstall = true;
    CHECK(UpdatePolicy::mayInstall(p, true, "2:digest", {}));
    p.checks = false;
    CHECK_FALSE(UpdatePolicy::mayInstall(p, true, "2:digest", {}));
    CHECK(UpdatePolicy::mayInstall(p, true, "2:digest", "2:digest"));
}
TEST_CASE("Update selection rejects incompatible identity channel and numeric builds", "[updates][update-policy]") {
    UpdateIdentity identity{1, Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)};
    UpdatePreferences p; p.channel = Channel::Preview;
    UpdateOffer offer{2, "3.0.0-pre.2", "notes", identity.bundleId, identity.cpu, QVersionNumber(26, 0), Channel::Preview, 100, QByteArray(32, 'a')};
    REQUIRE(UpdatePolicy::accepts(identity, p, offer));
    SECTION("same build") { offer.build = 1; }
    SECTION("older build") { offer.build = 0; }
    SECTION("wrong CPU") { offer.cpu = "x86_64"; }
    SECTION("wrong bundle") { offer.bundleId = "org.attacker"; }
    SECTION("newer OS") { offer.minimumOs = QVersionNumber(27); }
    SECTION("missing minimum OS") { offer.minimumOs = {}; }
    SECTION("missing digest") { offer.sha256.clear(); }
    SECTION("too large") { offer.size = 512LL * 1024 * 1024 + 1; }
    SECTION("empty version") { offer.version.clear(); }
    SECTION("stable channel") { p.channel = Channel::Stable; }
    SECTION("invalid channel") { offer.channel = static_cast<Channel>(99); }
    CHECK_FALSE(UpdatePolicy::accepts(identity, p, offer));
}
