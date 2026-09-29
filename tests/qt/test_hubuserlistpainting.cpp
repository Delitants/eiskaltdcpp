#include <catch2/catch_test_macros.hpp>
#include <QHeaderView>
#include <QImage>
#include <QPainter>
#include <QStandardItemModel>
#include <QTreeView>
#include <QCoreApplication>
#include <QSortFilterProxyModel>
#include "ViewLayout.h"
#include "HubUserListView.h"

namespace {
struct HubUserListPaintingFixture {
    QStandardItemModel model{4, 5};
    HubUserListView view;

    HubUserListPaintingFixture()
    {
        model.setHorizontalHeaderLabels({"Nick", "Share", "Exact share size", "Comment", "Tag"});
        for (int row = 0; row < model.rowCount(); ++row) {
            model.setData(model.index(row, 1), "2.50 TiB");
            model.setData(model.index(row, 1), int(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
        }
        view.setModel(&model);
        view.setRootIsDecorated(false);
        view.setItemsExpandable(false);
        view.setUniformRowHeights(true);
        view.setAlternatingRowColors(true);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.setStyleSheet(QStringLiteral(
            "QTreeView { border: 0; background-color: #202020;"
            " alternate-background-color: #404040; color: white;"
            " selection-background-color: #2850a0; selection-color: white; outline: 0; }"
            "QTreeView::item { color: white; }"
            "QTreeView::item:alternate { background-color: #404040; }"
            "QTreeView::item:selected { background-color: #2850a0; color: white; }"
            "QHeaderView::section { background-color: #303030; color: white;"
            " border: 0; border-right: 1px solid #606060; padding: 2px 5px; }"));
        view.header()->setStretchLastSection(false);
        view.header()->setSectionResizeMode(QHeaderView::Interactive);
        view.header()->resizeSection(0, 123);
        view.header()->resizeSection(1, 196);
        view.header()->hideSection(2);
        view.header()->resizeSection(3, 151);
        view.header()->resizeSection(4, 99);
        view.resize(650, 200);
        view.show();
        view.clearFocus();
        QCoreApplication::processEvents();
    }

    QImage render()
    {
        QImage image(view.viewport()->size(), QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        QPainter painter(&image);
        view.viewport()->render(&painter);
        return image;
    }
};
}

TEST_CASE("Hub alternate rows fill cells and spare viewport without vertical strips", "[qt][hubuserlistpainting]")
{
    HubUserListPaintingFixture fixture;
    const QByteArray savedWidths = fixture.view.header()->saveState();
    const QImage image = fixture.render();
    const int y = fixture.view.visualRect(fixture.model.index(1, 0)).top() + 1;
    const int commentStart = fixture.view.header()->sectionViewportPosition(3);
    REQUIRE(commentStart == 319);
    for (const int x : {commentStart - 90, commentStart - 3, commentStart + 3,
                        fixture.view.header()->length() + 10, image.width() - 5}) {
        INFO("alternate row pixel at " << x << "," << y
             << " = " << image.pixelColor(x, y).name().toStdString());
        CHECK(image.pixelColor(x, y) == QColor("#404040"));
    }
    CHECK(fixture.view.header()->saveState() == savedWidths);
}

TEST_CASE("Hub row fill preserves saved geometry after reorder and viewport resize", "[qt][hubuserlistpainting]")
{
    HubUserListPaintingFixture fixture;
    fixture.view.header()->moveSection(fixture.view.header()->visualIndex(3), 1);
    const QByteArray savedWidths = fixture.view.header()->saveState();
    fixture.view.resize(800, 200);
    QCoreApplication::processEvents();
    const QImage image = fixture.render();
    const int y = fixture.view.visualRect(fixture.model.index(1, 0)).center().y();
    CHECK(image.pixelColor(image.width() - 5, y) == QColor("#404040"));
    CHECK(fixture.view.header()->saveState() == savedWidths);
    CHECK(fixture.view.header()->sectionSize(0) == 123);
    CHECK(fixture.view.header()->sectionSize(1) == 196);
    CHECK(fixture.view.header()->sectionSize(3) == 151);
    CHECK(fixture.view.header()->sectionSize(4) == 99);
    CHECK(fixture.view.isColumnHidden(2));
}

TEST_CASE("Hub row fill survives filtered model swaps without changing saved widths", "[qt][hubuserlistpainting]")
{
    HubUserListPaintingFixture fixture;
    const QByteArray savedWidths = fixture.view.header()->saveState();
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&fixture.model);
    view_layout::setModelPreservingHeader(&fixture.view, &proxy);
    QCoreApplication::processEvents();
    const QImage image = fixture.render();
    const int y = fixture.view.visualRect(proxy.index(1, 0)).center().y();
    CHECK(image.pixelColor(image.width() - 5, y) == QColor("#404040"));
    CHECK(fixture.view.header()->saveState() == savedWidths);
    view_layout::setModelPreservingHeader(&fixture.view, &fixture.model);
    CHECK(fixture.view.header()->saveState() == savedWidths);
}

TEST_CASE("Hub selected row background reaches the viewport edge", "[qt][hubuserlistpainting]")
{
    HubUserListPaintingFixture fixture;
    fixture.view.selectionModel()->select(fixture.model.index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const QImage image = fixture.render();
    const int y = fixture.view.visualRect(fixture.model.index(1, 0)).center().y();
    CHECK(image.pixelColor(image.width() - 5, y) == QColor("#2850a0"));
}

TEST_CASE("Hub restored hidden Exact Share section leaves no phantom column", "[qt][hubuserlistpainting]")
{
    // Geometry-only snapshot: the hidden Exact Share entry retained a 100px span.
    const QByteArray state = QByteArray::fromBase64(
        "AAAA/wAAAAAAAAABAAAAAAAAAAABAAAAAAAAAAAAAAAJBAAAAAABAAAAAgAAAGQAAAYXAAAACQEBAAAAAAAAAAAAAAAAAABk/////wAAAIEAAAAAAAAACQAAANwAAAABAAAAAAAAAF8AAAABAAAAAAAAAGQAAAABAAAAAAAAALQAAAABAAAAAAAAAXEAAAABAAAAAAAAAF8AAAABAAAAAAAAAMgAAAABAAAAAAAAAHgAAAABAAAAAAAAALQAAAABAAAAAAAAA+gAAAAAZAAAAAAAAAAAAAAAAAAAAAE=");
    QStandardItemModel model(2, 9);
    HubUserListView view;
    view.setModel(&model);
    REQUIRE(view.restoreHeaderState(state));
    REQUIRE(view.isColumnHidden(2));
    CHECK(view.header()->sectionSize(0) == 220);
    CHECK(view.header()->sectionSize(1) == 95);
    CHECK(view.header()->sectionSize(3) == 180);
    CHECK(view.header()->sectionPosition(3) == 315);
    int visibleWidth = 0;
    for (int column = 0; column < model.columnCount(); ++column)
        visibleWidth += view.header()->sectionSize(column);
    CHECK(view.header()->length() == visibleWidth);
    view.showColumn(2);
    CHECK(view.header()->sectionSize(2) == 100);
    view.hideColumn(2);
    const QByteArray repaired = view.header()->saveState();
    REQUIRE(view.restoreHeaderState(repaired));
    CHECK(view.header()->saveState() == repaired);
    CHECK(view.header()->sectionPosition(3) == 315);
}
