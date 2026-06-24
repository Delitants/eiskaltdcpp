#include "MenuIconHelper.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPixmap>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Menu actions with application icons can opt in under the global safeguard", "[menuicons]") {
    qApp->setAttribute(Qt::AA_DontShowIconsInMenus, true);

    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::blue);
    QAction action(QIcon(pixmap), QStringLiteral("Browse files"), nullptr);
    action.setIconVisibleInMenu(false);

    MenuIconHelper::enableFor(&action);

    CHECK(action.isIconVisibleInMenu());
}

TEST_CASE("Menu actions without icons remain unchanged", "[menuicons]") {
    QAction action(QStringLiteral("No icon"), nullptr);
    action.setIconVisibleInMenu(false);

    MenuIconHelper::enableFor(&action);
    MenuIconHelper::enableFor(static_cast<QAction*>(nullptr));

    CHECK_FALSE(action.isIconVisibleInMenu());
}

TEST_CASE("Submenu parent actions can opt in", "[menuicons]") {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::green);
    QMenu menu(QStringLiteral("Copy data"));
    menu.setIcon(QIcon(pixmap));
    menu.menuAction()->setIconVisibleInMenu(false);

    MenuIconHelper::enableFor(&menu);
    MenuIconHelper::enableFor(static_cast<QMenu*>(nullptr));

    CHECK(menu.menuAction()->isIconVisibleInMenu());
}
