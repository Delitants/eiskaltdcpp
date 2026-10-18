// Register in the existing full-GUI integration target, never QT_CONTEXT_MINIMAL.
#include "dcpp/stdinc.h"
#include "ApplicationQuitTestFixture.h"
#include "PublicHubs.h"
#include "dcpp/FavoriteManager.h"
#include <QComboBox>
#include <QMetaObject>

TEST_CASE("Public Hubs selects the manager's preferred first source with matching indices",
          "[qt][mainwindow][hublist-priority]")
{
    IsolatedMainWindow fixture;
    auto *manager = fixture.core->getFavoriteManager();
    REQUIRE(manager);
    // This tests selector/index policy, not downloading. Block hub-list HTTP
    // before constructing PublicHubs, including all constructor refresh calls.
    manager->stopForShutdown();
    auto *settings = fixture.core->getSettingsManager();
    const std::string raw = "https://ordinary.invalid/custom.xml;"
                            "https://second.invalid/list.xml;https://hublist.eu/hublist.xml.bz2";
    settings->set(dcpp::SettingsManager::HUBLIST_SERVERS, raw);
    const dcpp::StringList expected = {"https://hublist.eu/hublist.xml.bz2",
        "https://ordinary.invalid/custom.xml", "https://second.invalid/list.xml"};
    PublicHubs view(*fixture.core);
    auto *selector = view.findChild<QComboBox*>(QStringLiteral("comboBox_HUBS"));
    REQUIRE(selector);
    REQUIRE(selector->count() == int(expected.size()));
    CHECK(selector->currentIndex() == 0);
    CHECK(selector->currentText().toStdString() == expected.front());
    CHECK(manager->getHubLists() == expected);
    for (int index = 0; index < selector->count(); ++index) {
        INFO("selector index=" << index);
        CHECK(selector->itemText(index).toStdString() == expected.at(index));
        CHECK(selector->itemText(index).toStdString() == manager->getHubLists().at(index));
        selector->setCurrentIndex(index);
        REQUIRE(QMetaObject::invokeMethod(&view, "slotHubChanged", Qt::DirectConnection, Q_ARG(int, index)));
        CHECK(manager->getSelectedHubList() == index);
        CHECK_FALSE(manager->isDownloading());
        CHECK(settings->get(dcpp::SettingsManager::HUBLIST_SERVERS) == raw);
    }
}

TEST_CASE("Public Hubs keeps a custom first source when hublist.eu is absent",
          "[qt][mainwindow][hublist-priority]")
{
    IsolatedMainWindow fixture;
    auto *manager = fixture.core->getFavoriteManager();
    REQUIRE(manager);
    manager->stopForShutdown();
    auto *settings = fixture.core->getSettingsManager();
    const std::string raw = "https://private.invalid/custom.xml;https://hublist.eu.attacker.invalid/list.xml";
    settings->set(dcpp::SettingsManager::HUBLIST_SERVERS, raw);
    PublicHubs view(*fixture.core);
    auto *selector = view.findChild<QComboBox*>(QStringLiteral("comboBox_HUBS"));
    REQUIRE(selector);
    REQUIRE(selector->count() == 2);
    CHECK(selector->currentIndex() == 0);
    CHECK(selector->currentText() == QStringLiteral("https://private.invalid/custom.xml"));
    CHECK(manager->getHubLists().front() == selector->currentText().toStdString());
    CHECK(settings->get(dcpp::SettingsManager::HUBLIST_SERVERS) == raw);
    CHECK_FALSE(manager->isDownloading());
}
