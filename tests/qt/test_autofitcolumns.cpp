#include "AutoFitColumns.h"
#include <catch2/catch_test_macros.hpp>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QSettings>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <functional>

namespace {
bool waitForFit(const std::function<bool()> &ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 1500) QTest::qWait(10);
    return ready();
}

struct Table {
    QStandardItemModel model{1, 3};
    QTreeView view;
    Table()
    {
        model.setHorizontalHeaderLabels({"User", "Encryption", "File name"});
        model.setData(model.index(0, 0), "Example user");
        model.setData(model.index(0, 1), "TLS_AES_256_GCM_SHA384");
        model.setData(model.index(0, 2), "A complete and readable transfer filename.iso");
        view.setModel(&model);
        view.setRootIsDecorated(false);
        view.resize(1000, 230);
        view.show();
        QCoreApplication::processEvents();
    }
    int textWidth(int column) const
    {
        return view.fontMetrics().horizontalAdvance(model.data(model.index(0, column)).toString());
    }
};
}

TEST_CASE("Untouched transfer columns fit incoming content and translated headers", "[qt][autofit]")
{
    Table table;
    AutoFitColumns fit(&table.view);
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2) + 8; }));
    REQUIRE(table.view.columnWidth(1) >= table.textWidth(1) + 8);
    table.model.setHeaderData(0, Qt::Horizontal, "A much longer translated user heading");
    const int heading = table.view.fontMetrics().horizontalAdvance("A much longer translated user heading");
    REQUIRE(waitForFit([&] { return table.view.columnWidth(0) >= heading + 12; }));
    table.model.setData(table.model.index(0, 2), "An even longer complete filename that arrives after the initial display.iso");
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2) + 8; }));
    const int grown = table.view.columnWidth(2);
    table.model.setData(table.model.index(0, 2), "short.iso");
    QTest::qWait(160);
    REQUIRE(table.view.columnWidth(2) == grown);
}

TEST_CASE("Manual transfer width survives content changes and a saved-layout restart", "[qt][autofit]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    int manualWidth = 0;
    {
        Table table;
        AutoFitColumns fit(&table.view);
        REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
        table.view.header()->resizeSection(1, 89);
        table.view.header()->moveSection(2, 0);
        table.view.header()->hideSection(0);
        manualWidth = table.view.columnWidth(1);
        table.model.setData(table.model.index(0, 1), "This encryption name must not override a user's narrow column");
        QTest::qWait(160);
        REQUIRE(table.view.columnWidth(1) == manualWidth);
        QSettings saved(directory.filePath("layout.ini"), QSettings::IniFormat);
        saved.setValue("columns", fit.saveState());
        saved.sync();
    }
    Table reopened;
    AutoFitColumns restored(&reopened.view);
    QSettings saved(directory.filePath("layout.ini"), QSettings::IniFormat);
    REQUIRE(restored.restoreState(saved.value("columns").toMap()));
    reopened.model.setData(reopened.model.index(0, 2), "A new filename after restart is still automatically sized to fit all of its text.iso");
    REQUIRE(waitForFit([&] { return reopened.view.columnWidth(2) >= reopened.textWidth(2); }));
    REQUIRE(reopened.view.columnWidth(1) == manualWidth);
    REQUIRE(reopened.view.header()->visualIndex(2) == 0);
    REQUIRE(reopened.view.isColumnHidden(0));
    restored.fitToContents(1);
    REQUIRE(reopened.view.columnWidth(1) >= reopened.textWidth(1) + 8);
    reopened.model.setData(reopened.model.index(0, 1), "Another longer encryption description after opting back into auto sizing");
    REQUIRE(waitForFit([&] { return reopened.view.columnWidth(1) >= reopened.textWidth(1); }));
}

TEST_CASE("Legacy saved widths are preserved until explicitly fitted", "[qt][autofit]")
{
    Table table;
    table.view.header()->setStretchLastSection(false);
    table.view.header()->resizeSection(1, 77);
    const QByteArray legacy = table.view.header()->saveState();
    AutoFitColumns fit(&table.view);
    REQUIRE(fit.restoreState({}, legacy));
    QTest::qWait(160);
    REQUIRE(table.view.columnWidth(1) == 77);
    fit.fitToContents();
    REQUIRE(table.view.columnWidth(1) >= table.textWidth(1));
    REQUIRE(table.view.columnWidth(2) >= table.textWidth(2));
}

TEST_CASE("Model reset and hiding columns do not replace manual width choices", "[qt][autofit]")
{
    Table table;
    AutoFitColumns fit(&table.view);
    table.view.header()->resizeSection(1, 187);
    table.model.clear();
    table.model.setColumnCount(3);
    table.model.setHorizontalHeaderLabels({"User", "Encryption", "File name"});
    table.model.appendRow({new QStandardItem("User"), new QStandardItem("A very long replacement encryption description"),
                           new QStandardItem("A new full filename after resetting the model.iso")});
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    REQUIRE(table.view.columnWidth(1) == 187);
    table.view.hideColumn(1);
    table.view.showColumn(1);
    QTest::qWait(160);
    REQUIRE(table.view.columnWidth(1) == 187);
}

TEST_CASE("Header drag locks one column while font changes still fit untouched columns", "[qt][autofit]")
{
    Table table;
    AutoFitColumns fit(&table.view);
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    auto *header = table.view.header();
    const int start = header->sectionViewportPosition(0) + header->sectionSize(0) - 1;
    const QPoint handle(start, header->height() / 2);
    QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, handle);
    QTest::mouseMove(header->viewport(), handle + QPoint(65, 0));
    QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier, handle + QPoint(65, 0));
    const int dragged = table.view.columnWidth(0);
    REQUIRE(dragged > start + 40);
    QFont larger = table.view.font();
    larger.setPointSize(22);
    table.view.setFont(larger);
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    REQUIRE(table.view.columnWidth(0) == dragged);
}

TEST_CASE("Auto fitting settles without repeated width saves or periodic work", "[qt][autofit]")
{
    Table table;
    AutoFitColumns fit(&table.view);
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    QSignalSpy changes(&fit, &AutoFitColumns::layoutChanged);
    QTest::qWait(300);
    REQUIRE(changes.isEmpty());
    for (int update = 0; update < 50; ++update)
        table.model.setData(table.model.index(0, 2), QString(90 + update, 'x'));
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    REQUIRE(changes.count() == 1);
    changes.clear();
    QTest::qWait(300);
    REQUIRE(changes.isEmpty());
}

TEST_CASE("Legacy transfer layout notifications trigger fitting after live updates", "[qt][autofit]")
{
    Table table;
    AutoFitColumns fit(&table.view);
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    QTest::qWait(160);
    {
        const QSignalBlocker legacyUpdate(&table.model);
        table.model.setData(table.model.index(0, 2), QString(130, 'W'));
    }
    emit table.model.layoutChanged();
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
}

TEST_CASE("Restoring automatic widths schedules a fresh fit after the first display", "[qt][autofit]")
{
    Table table;
    const QVariantMap saved{{"version", 1}, {"header", table.view.header()->saveState()},
                            {"manual", QVariantMap{}}};
    AutoFitColumns fit(&table.view);
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
    QTest::qWait(160);
    REQUIRE(fit.restoreState(saved));
    REQUIRE(waitForFit([&] { return table.view.columnWidth(2) >= table.textWidth(2); }));
}

TEST_CASE("Legacy stretched last columns keep their visible width during migration", "[qt][autofit]")
{
    Table table;
    table.view.header()->setStretchLastSection(false);
    table.view.header()->resizeSection(2, 145);
    table.view.header()->setStretchLastSection(true);
    QTest::qWait(30);
    const int visibleWidth = table.view.columnWidth(2);
    REQUIRE(visibleWidth > 145);
    const auto legacy = table.view.header()->saveState();
    AutoFitColumns fit(&table.view);
    REQUIRE(fit.restoreState({}, legacy));
    QTest::qWait(160);
    REQUIRE(table.view.columnWidth(2) == visibleWidth);
    REQUIRE_FALSE(table.view.header()->stretchLastSection());
}

TEST_CASE("Legacy stretched widths survive restoration into a narrower window", "[qt][autofit]")
{
    Table table;
    table.view.header()->setStretchLastSection(true);
    QTest::qWait(30);
    const int previousWidth = table.view.columnWidth(2);
    const auto legacy = table.view.header()->saveState();
    Table reopened;
    reopened.view.resize(600, 230);
    QTest::qWait(30);
    AutoFitColumns fit(&reopened.view);
    REQUIRE(fit.restoreState({}, legacy));
    QTest::qWait(160);
    REQUIRE(reopened.view.columnWidth(2) == previousWidth);
}
