#include "DockCursorGuard.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDockWidget>
#include <QHoverEvent>
#include <QTest>
#include <QTreeWidget>

namespace {
void hoverAt(QMainWindow &window, const QPoint &position) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    QHoverEvent event(QEvent::HoverMove, position, window.mapToGlobal(position), position);
#else
    QHoverEvent event(QEvent::HoverMove, position, position);
#endif
    QApplication::sendEvent(&window, &event);
}
struct RestorePointer {
    QPoint position = QCursor::pos();
    ~RestorePointer() { QCursor::setPos(position); }
};
}

TEST_CASE("Dock resize cursor is rechecked after geometry changes", "[qt][dock-cursor]")
{
    RestorePointer pointer;
    QMainWindow window;
    DockCursorGuard guard(&window);
    auto *view = new QTreeWidget(&window);
    view->viewport()->setCursor(Qt::ArrowCursor);
    window.setCentralWidget(view);
    auto *dock = new QDockWidget(&window);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setWidget(new QTreeWidget(dock));
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);
    window.resize(800, 600);
    window.show();
    QTest::qWait(30);
    const QPoint divider(300, (view->geometry().bottom() + dock->geometry().top()) / 2);
    QCursor::setPos(window.mapToGlobal(divider));
    hoverAt(window, divider);
    REQUIRE(window.cursor().shape() == Qt::SplitVCursor);

    window.resize(800, 750);
    QTest::qWait(60);
    REQUIRE(view->geometry().contains(divider));
    CHECK(window.cursor().shape() == Qt::ArrowCursor);
    CHECK(view->viewport()->cursor().shape() == Qt::ArrowCursor);

    const QPoint newDivider(300, (view->geometry().bottom() + dock->geometry().top()) / 2);
    QCursor::setPos(window.mapToGlobal(newDivider));
    hoverAt(window, newDivider);
    QTest::qWait(30);
    CHECK(window.cursor().shape() == Qt::SplitVCursor);
    const int oldTop = dock->geometry().top();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, newDivider);
    QTest::mouseMove(&window, newDivider - QPoint(0, 35));
    QTest::qWait(30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, newDivider - QPoint(0, 35));
    QTest::qWait(30);
    CHECK(dock->geometry().top() < oldTop);
    view->viewport()->setCursor(Qt::IBeamCursor);
    QCursor::setPos(view->viewport()->mapToGlobal(QPoint(30,30)));
    QTest::mouseMove(view->viewport(), QPoint(30,30));
    QTest::qWait(30);
    CHECK(view->viewport()->cursor().shape() == Qt::IBeamCursor);
}
