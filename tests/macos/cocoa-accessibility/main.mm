#import <AppKit/AppKit.h>
#include <mach-o/dyld.h>
#include <QAccessible>
#include <QApplication>
#include <QFileInfo>
#include <QSet>
#include <QStandardItemModel>
#include <QTableView>
#include <QTreeView>
#include <cstdio>

// Declare only the native bridge methods exercised by this functional fixture.
@interface NSObject (EiskaltAccessibilityFixture)
+ (id)elementWithId:(QAccessible::Id)identifier;
+ (void)removeElementsFromCache:(NSArray *)elements;
- (id)initWithId:(QAccessible::Id)identifier role:(NSString *)role;
- (NSArray *)accessibilitySelectedChildren;
- (QAccessibleInterface *)qtInterface;
@end

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const QString root = QString::fromLocal8Bit(argv[1]);
    const QString expected = QFileInfo(root + "/platforms/libqcocoa.dylib").canonicalFilePath();
    if (expected.isEmpty())
        return 2;
    qunsetenv("QT_QPA_PLATFORM_PLUGIN_PATH");
    qputenv("QT_QPA_PLATFORM", "cocoa");
    qputenv("QT_ACCESSIBILITY", "1");
    QCoreApplication::setLibraryPaths({root});
    QApplication app(argc, argv);
    int plugins = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const QFileInfo loaded(QString::fromUtf8(_dyld_get_image_name(i)));
        if (loaded.fileName() != "libqcocoa.dylib")
            continue;
        if (loaded.canonicalFilePath() != expected)
            return 2;
        ++plugins;
    }
    if (plugins != 1)
        return 2;
    std::printf("Plugin: %s\n", qPrintable(expected));
    QAccessible::setActive(true);
    Class bridge = NSClassFromString(@"QMacAccessibilityElement");
    if (!bridge)
        return 2;
    QStandardItemModel model(3, 2);
    for (int row = 0; row < model.rowCount(); ++row)
        for (int column = 0; column < model.columnCount(); ++column)
            model.setData(model.index(row, column), QString("Cell %1 %2").arg(row).arg(column));
    QTableView view;
    view.setModel(&model);
    view.setSelectionBehavior(QAbstractItemView::SelectRows);
    view.show();
    app.processEvents();
    auto *table = QAccessible::queryAccessibleInterface(&view);
    const auto tableId = QAccessible::uniqueId(table);

    // Native placeholders borrow their table ID; releasing one must not
    // unregister the still-live table. Stop here on failure, before any use.
    for (NSString *role in @[NSAccessibilityRowRole, NSAccessibilityColumnRole, NSAccessibilityCellRole]) {
        @autoreleasepool {
            id placeholder = [[bridge alloc] initWithId:tableId role:role];
            [placeholder release];
        }
        if (QAccessible::accessibleInterface(tableId) != table) {
            std::fprintf(stderr, "FAIL borrowed table identity lost after placeholder release\n");
            return 1;
        }
    }
    std::puts("PASS placeholder ownership");

    @autoreleasepool {
        id row = [[bridge alloc] initWithId:tableId role:NSAccessibilityRowRole];
        [bridge removeElementsFromCache:@[row]];
        [row release];
        if (QAccessible::accessibleInterface(tableId) != table) {
            std::fprintf(stderr, "FAIL placeholder cleanup removed borrowed table identity\n");
            return 1;
        }
    }
    std::puts("PASS placeholder array cleanup");

    for (int round = 0; round < 12; ++round) {
        QList<QAccessible::Id> selectedIds;
        @autoreleasepool {
            const int selectedRow = round % model.rowCount();
            view.selectRow(selectedRow);
            id nativeTable = [bridge elementWithId:tableId];
            NSArray *selected = [nativeTable accessibilitySelectedChildren];
            if (selected.count != 2 || QAccessible::accessibleInterface(tableId) != table) {
                std::fprintf(stderr, "FAIL selected row accessibility\n");
                return 1;
            }
            QSet<int> columns;
            for (id cell in selected) {
                auto *iface = [cell qtInterface];
                auto *cellInterface = iface ? iface->tableCellInterface() : nullptr;
                if (![[cell accessibilityRole] isEqualToString:NSAccessibilityCellRole]
                    || !cellInterface || cellInterface->rowIndex() != selectedRow
                    || columns.contains(cellInterface->columnIndex()))
                    return 1;
                columns.insert(cellInterface->columnIndex());
                if (iface->text(QAccessible::Name) != model.data(model.index(selectedRow,
                        cellInterface->columnIndex())).toString())
                    return 1;
                selectedIds.append(QAccessible::uniqueId(iface));
            }
            model.insertRow(model.rowCount());
            model.removeRow(model.rowCount() - 1);
            app.processEvents();
        }
        if (QAccessible::accessibleInterface(tableId) != table) {
            std::fprintf(stderr, "FAIL live table identity changed after model update\n");
            return 1;
        }
        for (auto id : selectedIds) {
            if (!QAccessible::accessibleInterface(id)) {
                std::fprintf(stderr, "FAIL surviving cell identity lost after deferred row cleanup\n");
                return 1;
            }
        }
    }
    std::puts("PASS selection and model updates with accessibility enabled");
    view.clearSelection();
    if ([[bridge elementWithId:tableId] accessibilitySelectedChildren].count != 0)
        return 1;
    std::puts("PASS empty selection");

    QTreeView tree;
    tree.setModel(&model);
    tree.setSelectionBehavior(QAbstractItemView::SelectRows);
    tree.show();
    app.processEvents();
    const auto treeId = QAccessible::uniqueId(QAccessible::queryAccessibleInterface(&tree));
    for (int row = 0; row < model.rowCount(); ++row) {
        @autoreleasepool {
            tree.selectionModel()->select(model.index(row, 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            NSArray *selected = [[bridge elementWithId:treeId] accessibilitySelectedChildren];
            if (selected.count != 2)
                return 1;
        }
        app.processEvents();
        if (!QAccessible::accessibleInterface(treeId))
            return 1;
    }
    std::puts("PASS tree row selection");

    id retiredRow = [[[[bridge elementWithId:tableId] accessibilityRows] firstObject] retain];
    @autoreleasepool {
        model.insertRow(model.rowCount());
        app.processEvents();
    }
    if (!retiredRow || [retiredRow qtInterface] != nullptr) {
        std::fprintf(stderr, "FAIL detached synthetic row retains borrowed table identity\n");
        [retiredRow release];
        return 1;
    }
    [retiredRow release];
    std::puts("PASS retained detached row invalidated immediately");

    QList<QAccessible::Id> survivingIds;
    id retainedNativeCell = nil;
    @autoreleasepool {
        view.selectRow(1);
        NSArray *cells = [[bridge elementWithId:tableId] accessibilitySelectedChildren];
        for (id cell in cells)
            survivingIds.append(QAccessible::uniqueId([cell qtInterface]));
        retainedNativeCell = [cells.firstObject retain];
        model.insertRow(0);
        model.insertColumn(0);
        app.processEvents();
    }
    for (auto id : survivingIds) {
        auto *iface = QAccessible::accessibleInterface(id);
        if (!iface || !iface->isValid())
            return 1;
        NSObject *nativeCell = [bridge elementWithId:id];
        auto *cell = [nativeCell qtInterface]->tableCellInterface();
        if (cell->rowIndex() != 2 || cell->columnIndex() < 1)
            return 1;
    }
    id shiftedParent = [retainedNativeCell accessibilityParent];
    if (!shiftedParent || [shiftedParent accessibilityIndex] != 2)
        return 1;
    NSArray *shiftedChildren = [shiftedParent accessibilityChildren];
    if (shiftedChildren.count != 3)
        return 1;
    auto *promoted = [shiftedChildren[1] qtInterface];
    if (!promoted || QAccessible::uniqueId(promoted) != survivingIds.first())
        return 1;
    [retainedNativeCell release];
    std::puts("PASS native parent coordinates and placeholder promotion after shifts");
    @autoreleasepool {
        model.removeRow(0);
        model.removeColumn(0);
        app.processEvents();
    }
    for (auto id : survivingIds) {
        auto *iface = QAccessible::accessibleInterface(id);
        if (!iface || !iface->isValid() || iface->tableCellInterface()->rowIndex() != 1)
            return 1;
    }
    std::puts("PASS surviving identities after row and column shifts");

    id retainedCell = [[[bridge elementWithId:tableId] accessibilitySelectedChildren].firstObject retain];
    @autoreleasepool {
        model.clear();
        app.processEvents();
    }
    for (auto id : survivingIds) {
        if (QAccessible::accessibleInterface(id))
            return 1;
    }
    if (!retainedCell || [retainedCell qtInterface] != nullptr)
        return 1;
    [retainedCell release];
    std::puts("PASS model reset invalidates removed cells");

    auto *temporaryView = new QTableView;
    temporaryView->setModel(&model);
    const auto temporaryId = QAccessible::uniqueId(QAccessible::queryAccessibleInterface(temporaryView));
    id retainedTable = [[bridge elementWithId:temporaryId] retain];
    @autoreleasepool {
        delete temporaryView;
        app.processEvents();
    }
    if (QAccessible::accessibleInterface(temporaryId) || [retainedTable qtInterface] != nullptr)
        return 1;
    [retainedTable release];
    std::puts("PASS view destruction invalidates retained native table");
    return 0;
}
