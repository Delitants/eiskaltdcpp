#include <catch2/catch_test_macros.hpp>
#include "ViewLayout.h"
#include "CountryNames.h"
#include <QStandardItemModel>
#include <QTreeView>
#include <QSettings>
#include <QTemporaryDir>
#include <QSortFilterProxyModel>

TEST_CASE("Filtering a user list does not reset or save intermediate column widths", "[qt][viewlayout]") {
    QStandardItemModel model(1, 3);
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTreeView view;
    view.setModel(&model);
    view.header()->setStretchLastSection(false);
    view.header()->resizeSection(0, 287);
    view.header()->resizeSection(1, 173);
    int saves = 0;
    QObject::connect(view.header(), &QHeaderView::sectionResized, &view, [&] { ++saves; });
    view_layout::setModelPreservingHeader(&view, &proxy);
    REQUIRE(view.header()->sectionSize(0) == 287);
    view_layout::setModelPreservingHeader(&view, &model);
    REQUIRE(view.header()->sectionSize(1) == 173);
    REQUIRE(saves == 0);
}

TEST_CASE("Hub column widths survive reopening in either hub order", "[qt][viewlayout]") {
    QTemporaryDir dir;
    const QString path = dir.filePath("layout.ini");
    QStandardItemModel model(0, 3);
    QTreeView first, second;
    first.setModel(&model);
    second.setModel(&model);
    first.header()->setStretchLastSection(false);
    second.header()->setStretchLastSection(false);
    first.header()->resizeSection(0, 287);
    second.header()->resizeSection(0, 163);
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(view_layout::hubStateKey("nmdcs://one:411"), first.header()->saveState());
        settings.setValue(view_layout::hubStateKey("nmdcs://two:411"), second.header()->saveState());
    }
    QSettings restored(path, QSettings::IniFormat);
    REQUIRE(view_layout::restoreHeader(first.header(), restored.value(view_layout::hubStateKey("nmdcs://two:411")).toByteArray()));
    REQUIRE(first.header()->sectionSize(0) == 163);
    REQUIRE(view_layout::restoreHeader(first.header(), restored.value(view_layout::hubStateKey("nmdcs://one:411")).toByteArray()));
    REQUIRE(first.header()->sectionSize(0) == 287);
    REQUIRE_FALSE(view_layout::restoreHeader(first.header(), "invalid"));
}

TEST_CASE("Transfer status grows to fit text and preserves wider saved widths", "[qt][viewlayout]") {
    QStandardItemModel model(0, 3);
    QTreeView view;
    view.setModel(&model);
    view.header()->setStretchLastSection(false);
    view.header()->resizeSection(1, 70);
    const QString status = "Download complete";
    view_layout::fitColumnText(view.header(), 1, view.fontMetrics(), status);
    REQUIRE(view.header()->sectionSize(1) >= view.fontMetrics().horizontalAdvance(status) + 24);
    view.header()->resizeSection(1, 500);
    REQUIRE(view_layout::restoreHeader(view.header(), view.header()->saveState()));
    view_layout::fitColumnText(view.header(), 1, view.fontMetrics(), status);
    REQUIRE(view.header()->sectionSize(1) == 500);
}

TEST_CASE("Country tooltip uses country names rather than ISO codes", "[qt][country]") {
    REQUIRE(country_names::fromCode("NO") == "Norway");
    REQUIRE(country_names::fromCode("ua") == "Ukraine");
    REQUIRE(country_names::fromCode("US") == "United States");
    REQUIRE(country_names::fromCode("ZZ").isEmpty());
    REQUIRE(country_names::fromCode("").isEmpty());
}
