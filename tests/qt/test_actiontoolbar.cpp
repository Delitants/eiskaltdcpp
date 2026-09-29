#include "ActionToolBar.h"
#include "AppIconTheme.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QSignalSpy>
#include <QTabBar>
#include <QToolButton>
#include <QTest>

namespace {
void settle()
{
    QApplication::sendPostedEvents();
    QApplication::processEvents();
}

QList<QAction*> populate(ActionToolBar &toolbar)
{
    QList<QAction*> actions;
    const QStringList names = {QStringLiteral("Preferences"), QStringLiteral("Torrents"),
        QString::fromUtf8("Черга завантажень"), QStringLiteral("Finished downloads"),
        QStringLiteral("Geschwindigkeitsbegrenzung ein/aus")};
    for (int i = 0; i < 20; ++i) {
        auto *action = toolbar.addAction(app_icon_theme::icon(app_icon_theme::resourcePath("reborn"),
            "download", toolbar.palette()), names.at(i % names.size()));
        action->setCheckable(i % 3 == 0);
        actions << action;
    }
    return actions;
}
}

TEST_CASE("Toolbar presentation bounds every text mode without losing action labels", "[qt][actiontoolbar]")
{
    QMainWindow window;
    ActionToolBar toolbar(&window);
    toolbar.setObjectName("fBar");
    window.addToolBar(&toolbar);
    const auto actions = populate(toolbar);
    window.resize(780, 400);
    window.show();
    for (const auto mode : {Qt::ToolButtonIconOnly, Qt::ToolButtonTextOnly,
                           Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon}) {
        toolbar.setToolButtonStyle(mode);
        settle();
        INFO(static_cast<int>(mode));
        for (auto *action : actions) {
            auto *button = qobject_cast<QToolButton*>(toolbar.widgetForAction(action));
            REQUIRE(button);
            const int limit = mode == Qt::ToolButtonTextBesideIcon ? 208 :
                mode == Qt::ToolButtonTextOnly ? 168 : mode == Qt::ToolButtonTextUnderIcon ? 136 : 48;
            CHECK(button->sizeHint().width() <= limit);
            CHECK(button->sizeHint().height() >= 36);
            CHECK(button->font() == toolbar.font());
            CHECK(button->toolTip().contains(action->text()));
            CHECK(action->text().isEmpty() == false);
        }
        auto *overflow = toolbar.findChild<QToolButton*>("qt_toolbar_ext_button");
        REQUIRE(overflow);
        CHECK(overflow->isVisible());
        const auto height = toolbar.height();
        QTest::mouseClick(overflow, Qt::LeftButton);
        settle();
        auto *menu = toolbar.findChild<QMenu*>("actionToolbarOverflow");
        REQUIRE(menu);
        CHECK(menu->isVisible());
        CHECK(menu->actions().contains(actions.last()));
        CHECK(menu->actions().last()->text() == actions.last()->text());
        CHECK(toolbar.height() == height);
        menu->close();
        overflow->click();
        settle();
        CHECK(menu->isVisible());
        CHECK(toolbar.height() == height);
        menu->close();
        window.resize(1200, 400);
        settle();
        CHECK(toolbar.height() == height);
        window.resize(780, 400);
    }
}

TEST_CASE("Toolbar and tabs remain on separate full-width rows after restoring old layouts", "[qt][actiontoolbar]")
{
    QMainWindow window;
    ActionToolBar actions(&window);
    actions.setObjectName("fBar");
    populate(actions);
    QToolBar tabs(&window);
    tabs.setObjectName("tBar");
    QTabBar tabbar;
    tabbar.setExpanding(false);
    tabbar.setUsesScrollButtons(true);
    for (int i = 0; i < 24; ++i)
        tabbar.addTab(QStringLiteral("Hub %1").arg(i));
    tabs.addWidget(&tabbar);
    QToolBar search(&window);
    search.setObjectName("sBar");
    search.addAction("Search");
    window.addToolBar(&actions);
    window.addToolBar(&tabs);
    window.addToolBar(&search);
    const auto oldState = window.saveState();
    REQUIRE(window.restoreState(oldState));
    action_toolbar::separateRows(window, &actions, &tabs, &search);
    window.resize(900, 450);
    window.show();
    for (const auto mode : {Qt::ToolButtonIconOnly, Qt::ToolButtonTextOnly,
                           Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon}) {
        actions.setToolButtonStyle(mode);
        settle();
        CHECK(window.toolBarBreak(&tabs));
        CHECK_FALSE(actions.geometry().intersects(tabs.geometry()));
        CHECK(tabs.geometry().top() >= actions.geometry().bottom());
        CHECK(tabs.width() >= window.width() - 4);
        CHECK_FALSE(search.geometry().intersects(tabs.geometry()));
    }
    const auto corrected = window.saveState();
    REQUIRE(window.restoreState(corrected));
    settle();
    CHECK(window.toolBarBreak(&tabs));
}

TEST_CASE("Toolbar style changes retain enabled, checked, and split-menu actions", "[qt][actiontoolbar]")
{
    ActionToolBar toolbar;
    const auto actions = populate(toolbar);
    auto *toggle = actions.first();
    toggle->setChecked(true);
    actions.at(1)->setEnabled(false);
    auto *button = qobject_cast<QToolButton*>(toolbar.widgetForAction(toggle));
    REQUIRE(button);
    QMenu menu;
    menu.addAction("Saved hub");
    button->setMenu(&menu);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    QSignalSpy triggered(toggle, &QAction::triggered);
    for (const auto mode : {Qt::ToolButtonTextOnly, Qt::ToolButtonTextUnderIcon,
                           Qt::ToolButtonTextBesideIcon, Qt::ToolButtonIconOnly}) {
        toolbar.setToolButtonStyle(mode);
        settle();
        CHECK(button->isChecked());
        CHECK(button->menu() == &menu);
        CHECK(button->popupMode() == QToolButton::MenuButtonPopup);
        CHECK_FALSE(actions.at(1)->isEnabled());
    }
    button->click();
    CHECK(triggered.count() == 1);
}

TEST_CASE("Toolbar overflow follows light and dark palette changes", "[qt][actiontoolbar]")
{
    ActionToolBar toolbar;
    populate(toolbar);
    auto *overflow = toolbar.findChild<QToolButton*>("qt_toolbar_ext_button");
    REQUIRE(overflow);
    for (const auto &color : {QColor("#20252a"), QColor("#f0f3f5")}) {
        auto palette = toolbar.palette();
        palette.setColor(QPalette::ButtonText, color);
        toolbar.setPalette(palette);
        const auto image = overflow->icon().pixmap(16, 16).toImage();
        bool foundOpaquePixel = false;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixelColor(x, y);
                if (pixel.alpha() < 200)
                    continue;
                foundOpaquePixel = true;
                CHECK(qAbs(pixel.lightness() - color.lightness()) < 4);
            }
        }
        CHECK(foundOpaquePixel);
    }
}

TEST_CASE("Overflow retains split-button submenus and their primary action", "[qt][actiontoolbar]")
{
    QMainWindow window;
    ActionToolBar toolbar(&window);
    window.addToolBar(&toolbar);
    const auto actions = populate(toolbar);
    auto *primary = actions.last();
    auto *button = qobject_cast<QToolButton*>(toolbar.widgetForAction(primary));
    QMenu savedHubs;
    QAction savedHub("Saved hub", &window);
    savedHubs.addAction(&savedHub);
    QObject::connect(&savedHubs, &QMenu::aboutToShow, [&] {
        savedHubs.clear();
        savedHubs.addAction(&savedHub);
    });
    button->setMenu(&savedHubs);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    window.resize(500, 400);
    window.show();
    settle();
    REQUIRE(button->isHidden());
    auto *overflow = toolbar.findChild<QToolButton*>("qt_toolbar_ext_button");
    REQUIRE(overflow);
    overflow->click();
    settle();
    auto *menu = toolbar.findChild<QMenu*>("actionToolbarOverflow");
    REQUIRE(menu);
    CHECK(menu->actions().contains(savedHubs.menuAction()));
    REQUIRE(QMetaObject::invokeMethod(&savedHubs, "aboutToShow", Qt::DirectConnection));
    CHECK(savedHubs.actions().contains(primary));
    CHECK(savedHubs.actions().contains(&savedHub));
    CHECK(primary->menu() == nullptr);
    QSignalSpy triggered(primary, &QAction::triggered);
    primary->trigger();
    CHECK(triggered.count() == 1);
    menu->close();
}

TEST_CASE("Toolbar follows application font changes after construction", "[qt][actiontoolbar]")
{
    QMainWindow window;
    ActionToolBar toolbar(&window);
    window.addToolBar(&toolbar);
    const auto actions = populate(toolbar);
    const auto original = QApplication::font();
    auto changed = original;
    changed.setPointSize(original.pointSize() > 0 ? original.pointSize() + 3 : 16);
    QApplication::setFont(changed);
    settle();
    CHECK(toolbar.font() == changed);
    CHECK(toolbar.widgetForAction(actions.first())->font() == changed);
    QApplication::setFont(original);
    settle();
}
