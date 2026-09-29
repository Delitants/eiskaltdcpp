#include "TabButton.h"
#include "TabNavigation.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSignalSpy>
#include <QTabBar>
#include <QTest>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QWheelEvent>
#include <memory>

#ifdef TAB_NAVIGATION_INTEGRATION
#include "TabNavigationFixture.h"
#include "ToolBar.h"
#include "MultiLineToolBar.h"
#endif

namespace {
void settle()
{
    QApplication::sendPostedEvents();
    QApplication::processEvents();
}

void settleLayout()
{
    for (int pass = 0; pass < 8; ++pass)
        settle();
}

QPalette tabPalette(bool dark)
{
    QPalette palette = qApp->palette();
    palette.setColor(QPalette::Window, QColor(dark ? "#292e34" : "#eeeeee"));
    palette.setColor(QPalette::Button, QColor(dark ? "#30363d" : "#d6d6d6"));
    palette.setColor(QPalette::Base, QColor(dark ? "#252a30" : "#ffffff"));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        palette.setColor(role, QColor(dark ? "#f0f3f5" : "#121212"));
    palette.setColor(QPalette::Mid, QColor(dark ? "#687480" : "#8a8a8a"));
    palette.setColor(QPalette::Midlight, QColor(dark ? "#465566" : "#c3d5e8"));
    palette.setColor(QPalette::Highlight, QColor("#1683dc"));
    return palette;
}

struct ApplicationSheet {
    QString previous = qApp->styleSheet();

    explicit ApplicationSheet(const QPalette &palette)
    {
        // main.cpp's macInputContrastStyle wraps the application style but has no tab rule.
        qApp->setStyleSheet(previous + QStringLiteral(
            "\nQLineEdit { border: 1px solid %1; border-radius: 6px;"
            " padding: 2px 6px; background-color: %2; color: %3; }")
            .arg(palette.color(QPalette::Mid).name(), palette.color(QPalette::Base).name(),
                 palette.color(QPalette::Text).name()));
    }

    ~ApplicationSheet() { qApp->setStyleSheet(previous); }
};

QImage paintWidget(QWidget &widget, bool children = true)
{
    const qreal scale = widget.devicePixelRatioF();
    QImage image(QSize(qRound(widget.width() * scale), qRound(widget.height() * scale)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    QWidget::RenderFlags flags = QWidget::DrawWindowBackground;
    if (children)
        flags |= QWidget::DrawChildren;
    widget.render(&image, QPoint(), QRegion(), flags);
    return image;
}

QColor logicalPixel(const QImage &image, const QPoint &point)
{
    const qreal scale = image.devicePixelRatio();
    return image.pixelColor(int((point.x() + 0.5) * scale), int((point.y() + 0.5) * scale));
}

QImage closePatch(QToolButton &close)
{
    const QImage image = paintWidget(*close.parentWidget());
    const qreal scale = image.devicePixelRatio();
    return image.copy(QRect(qRound(close.x() * scale), qRound(close.y() * scale),
                            qRound(close.width() * scale), qRound(close.height() * scale)));
}

void checkClosePaint(QToolButton &close)
{
    REQUIRE(close.size() == QSize(24, 24));
    CHECK_FALSE(close.accessibleName().isEmpty());
    CHECK((close.focusPolicy() & Qt::TabFocus) != 0);
    close.clearFocus();
    close.setDown(false);
    close.setAttribute(Qt::WA_UnderMouse, false);
    const auto underlay = paintWidget(*close.parentWidget(), false);
    const auto composite = paintWidget(*close.parentWidget());
    for (const auto &offset : {QPoint(3, 12), QPoint(20, 12), QPoint(12, 3), QPoint(12, 20)}) {
        const QPoint point = close.pos() + offset;
        CHECK(logicalPixel(composite, point) == logicalPixel(underlay, point));
    }
    bool visibleGlyph = false;
    for (int y = 5; y < 19; ++y)
        for (int x = 5; x < 19; ++x)
            visibleGlyph |= logicalPixel(composite, close.pos() + QPoint(x, y)) !=
                            logicalPixel(underlay, close.pos() + QPoint(x, y));
    CHECK(visibleGlyph);
    const auto idle = closePatch(close);
    close.setAttribute(Qt::WA_UnderMouse, true);
    const auto hovered = closePatch(close);
    CHECK(hovered != idle);
    close.setDown(true);
    CHECK(closePatch(close) != hovered);
    close.setDown(false);
    close.setAttribute(Qt::WA_UnderMouse, false);
    close.setFocus(Qt::TabFocusReason);
    REQUIRE(close.hasFocus());
    CHECK(closePatch(close) != idle);
    close.clearFocus();
}

#ifdef TAB_NAVIGATION_INTEGRATION
struct MultilineTabs {
    QtContext context;
    MultiLineToolBar toolbar{&context.window};
    QList<FixtureTab *> tabs;
    TabFrame *frame = nullptr;
    QScrollArea *area = nullptr;

    explicit MultilineTabs(int count, bool shortTitles = false)
    {
        context.window.setFont(QFont(QStringLiteral("monospace"), 12));
        context.window.addToolBar(&toolbar);
        frame = toolbar.findChild<TabFrame *>();
        REQUIRE(frame);
        area = frame->findChild<QScrollArea *>();
        REQUIRE(area);
        for (int i = 0; i < count; ++i) {
            auto *tab = new FixtureTab(i);
            if (shortTitles)
                tab->title = QStringLiteral("Tab %1").arg(i, 2, 10, QLatin1Char('0'));
            tabs.append(tab);
            context.manager.add(tab);
        }
        context.window.resize(390, 280);
        context.window.show();
        settleLayout();
    }

    ~MultilineTabs()
    {
        for (auto *tab : tabs)
            context.manager.rem(tab);
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    TabButton *button(int index) const
    {
        const QString title = tab_navigation::compactTitle(tabs[index]->getArenaShortTitle());
        for (auto *button : frame->findChildren<TabButton *>())
            if (button->text() == title)
                return button;
        return nullptr;
    }

    QRect visibleRect(TabButton *button) const
    {
        return QRect(button->mapTo(area->viewport(), QPoint()), button->size());
    }

    void fitTwoShortTabs()
    {
        REQUIRE(button(0));
        const int tabWidth = button(0)->normalWidth();
        const int width = tabWidth * 2 + 2 + tabWidth / 4;
        for (int pass = 0; pass < 4; ++pass) {
            context.window.resize(context.window.width() + width - area->viewport()->width(), 280);
            settleLayout();
        }
        REQUIRE(area->viewport()->width() == width);
        REQUIRE(button(0)->y() == button(1)->y());
        REQUIRE(button(2)->y() > button(1)->y());
    }
};

void sendWheel(QWidget *target, QPoint pixels, QPoint angle)
{
    const QPoint local = target->rect().center();
    QWheelEvent event(QPointF(local), QPointF(target->mapToGlobal(local)), pixels, angle,
                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(target, &event);
    settleLayout();
}
#endif
}

TEST_CASE("Hub titles retain names but omit keyprints from visible URLs", "[qt][tabnavigation]")
{
    using tab_navigation::compactTitle;
    CHECK(compactTitle("adcs://example.org:1511/?kp=SHA256/ABC") == "example.org:1511");
    CHECK(compactTitle("dchub://example.org/") == "example.org");
    CHECK(compactTitle("ADCS://example.org/?kp=ABC#fragment") == "example.org");
    CHECK(compactTitle("adc://[2001:db8::1]:411/?kp=ABC") == "[2001:db8::1]:411");
    CHECK(compactTitle("  Community with a recognizable name  ") == "Community with a recognizable name");
    CHECK(compactTitle(QString::fromUtf8("\xf0\x9f\x94\x92 adcs://example.org/?kp=ABC")) ==
          QString::fromUtf8("\xf0\x9f\x94\x92 example.org"));
}

TEST_CASE("All Tabs searches complete details and invalidates removed results", "[qt][tabnavigation]")
{
    tab_navigation::AllTabsMenu menu;
    QList<tab_navigation::Entry> entries;
    for (int i = 0; i < 40; ++i)
        entries.append({quint64(i + 1), QString("Community %1").arg(i),
                        QString("adcs://host-%1.example.org/?kp=SHA256/ABC").arg(i), {}, i == 20});
    menu.setEntries(entries);
    auto *search = menu.findChild<QLineEdit*>();
    auto *list = menu.findChild<QListWidget*>();
    REQUIRE(search);
    REQUIRE(list);
    CHECK(list->count() == 40);
    search->setText("COMMUNITY host-37");
    REQUIRE(list->count() == 1);
    QSignalSpy selected(&menu, &tab_navigation::AllTabsMenu::selected);
    QTest::keyClick(search, Qt::Key_Return);
    REQUIRE(selected.count() == 1);
    CHECK(selected.last().first().toULongLong() == 38);
    entries.removeAt(37);
    menu.setEntries(entries);
    CHECK(list->count() == 0);
    QTest::keyClick(search, Qt::Key_Down);
    QTest::keyClick(search, Qt::Key_Up);
    QTest::keyClick(search, Qt::Key_Return);
    CHECK(selected.count() == 1);
    search->clear();
    CHECK(list->count() == 39);
}

TEST_CASE("Tab navigation controls follow dark palettes and reveal the active menu row", "[qt][tabnavigation]")
{
    QWidget host;
    auto palette = host.palette();
    palette.setColor(QPalette::Window, QColor("#292e34"));
    palette.setColor(QPalette::WindowText, Qt::white);
    host.setPalette(palette);
    tab_navigation::AllTabsMenu menu(&host);
    auto *button = tab_navigation::makeAllTabsButton(&menu, &host);
    host.show();
    settle();
    const auto image = button->grab().toImage();
    CHECK(image.pixelColor(1, 1) == palette.color(QPalette::Window));
    menu.setEntries({{1, "First", "first.example.org", {}, true}});
    menu.setEntries({{1, "First", "first.example.org", {}, false},
                     {2, "Current", "current.example.org", {}, true}});
    button->click();
    settle();
    auto *list = menu.findChild<QListWidget*>();
    REQUIRE(list->currentItem());
    CHECK(list->currentItem()->data(Qt::UserRole).toULongLong() == 2);
    menu.hide();
}

TEST_CASE("Multiline tab widths are bounded and close targets stay at the edge", "[qt][tabnavigation]")
{
#ifdef TAB_NAVIGATION_INTEGRATION
    QtContext context;
#endif
    TabButton button;
    button.setText(QString(250, QLatin1Char('W')));
    button.resetGeometry();
    CHECK(button.normalWidth() <= 240);
    CHECK(button.minimumSizeHint().width() <= 96);
    button.resize(160, button.normalHeight());
    button.show();
    settle();
    CHECK(button.width() <= 160);
    auto *close = button.findChild<QToolButton*>("tabCloseButton");
    REQUIRE(close);
    CHECK(close->width() >= 24);
    CHECK(button.rect().contains(close->geometry()));
    CHECK(button.width() - close->geometry().right() <= 9);
    auto *icon = button.findChild<QLabel*>("tabWidgetIcon");
    REQUIRE(icon);
    CHECK_FALSE(icon->geometry().intersects(close->geometry()));
    CHECK_FALSE(button.titleRect().intersects(close->geometry()));
    CHECK_FALSE(button.titleRect().intersects(icon->geometry()));
    CHECK(button.elidedTitle().contains(QChar(0x2026)));
    CHECK(button.fontMetrics().horizontalAdvance(button.elidedTitle()) <= button.titleRect().width());
    QSignalSpy closed(&button, &TabButton::closeRequest);
    QTest::mouseClick(close, Qt::LeftButton);
    CHECK(closed.count() == 1);
    QTest::mouseClick(&button, Qt::MiddleButton);
    CHECK(closed.count() == 2);
}

TEST_CASE("Tab painting stays bounded through font palette and resize changes", "[qt][tabnavigation]")
{
#ifdef TAB_NAVIGATION_INTEGRATION
    QtContext context;
#endif
    TabButton button;
    button.setText(QString(120, QLatin1Char('M')));
    button.setWidgetIcon(qApp->style()->standardIcon(QStyle::SP_DirIcon));
    struct StyleCounter : QObject {
        int changes = 0;
        bool eventFilter(QObject *, QEvent *event) override {
            if (event->type() == QEvent::StyleChange) ++changes;
            return false;
        }
    } counter;
    button.installEventFilter(&counter);
    for (bool dark : {false, true}) {
        QPalette palette = button.palette();
        palette.setColor(QPalette::Button, dark ? QColor("#30363d") : QColor("#e5e9ed"));
        palette.setColor(QPalette::ButtonText, dark ? Qt::white : Qt::black);
        button.setPalette(palette);
        for (int points : {12, 19}) {
            QFont font = button.font();
            font.setPointSize(points);
            button.setFont(font);
            button.resetGeometry();
            for (int width : {96, 160, 240}) {
                button.resize(width, button.normalHeight());
                button.show();
                settle();
                button.setChecked(true);
                CHECK(button.width() == width);
                CHECK_FALSE(button.grab().isNull());
                QFont activeFont = button.font();
                activeFont.setBold(true);
                CHECK(QFontMetrics(activeFont).horizontalAdvance(button.elidedTitle()) <= button.titleRect().width());
            }
        }
    }
    CHECK(counter.changes == 0);
    CHECK(button.styleSheet().isEmpty());
}

TEST_CASE("Tab close targets retain equal end insets and clear text gaps", "[qt][tabnavigation][tab-followup]")
{
#ifdef TAB_NAVIGATION_INTEGRATION
    QtContext context;
#endif
    TabButton button;
    button.setText(QString(120, QLatin1Char('W')));
    button.setAutoExclusive(false);
    for (int points : {12, 19}) {
        QFont font = button.font();
        font.setPointSize(points);
        button.setFont(font);
        button.resetGeometry();
        button.show();
        for (int width : {80, 96, 160, 240}) {
            button.resize(width, button.normalHeight());
            settleLayout();
            auto *close = button.findChild<QToolButton *>("tabCloseButton");
            auto *icon = button.findChild<QLabel *>("tabWidgetIcon");
            REQUIRE(close);
            REQUIRE(icon);
            REQUIRE(close->size() == QSize(24, 24));
            CHECK(button.width() - close->geometry().right() - 1 == icon->x());
            CHECK(button.titleRect().left() - icon->geometry().right() - 1 ==
                  close->x() - button.titleRect().right() - 1);
            CHECK(button.rect().contains(close->geometry()));
            CHECK_FALSE(button.titleRect().intersects(close->geometry()));
            CHECK_FALSE(button.titleRect().intersects(icon->geometry()));
            for (bool active : {false, true}) {
                button.setChecked(active);
                QFont titleFont = button.font();
                titleFont.setBold(active);
                CHECK(QFontMetrics(titleFont).horizontalAdvance(button.elidedTitle()) <= button.titleRect().width());
            }
        }
    }
}

TEST_CASE("Multiline close paint stays transparent and exposes input states", "[qt][tabnavigation][tab-followup]")
{
#ifdef TAB_NAVIGATION_INTEGRATION
    QtContext context;
#endif
    for (bool dark : {false, true}) {
        const auto palette = tabPalette(dark);
        ApplicationSheet sheet(palette);
        QWidget host;
        host.setPalette(palette);
        TabButton button(&host);
        button.setText(QStringLiteral("Public Hubs"));
        button.setAutoExclusive(false);
        button.resize(180, button.normalHeight());
        host.resize(220, 80);
        host.show();
        host.activateWindow();
        settleLayout();
        auto *close = button.findChild<QToolButton *>("tabCloseButton");
        REQUIRE(close);
        for (bool active : {false, true}) {
            button.setChecked(active);
            checkClosePaint(*close);
        }
        QSignalSpy closed(&button, &TabButton::closeRequest);
        close->setFocus(Qt::TabFocusReason);
        QTest::keyClick(close, Qt::Key_Space);
        CHECK(closed.count() == 1);
    }
}

TEST_CASE("Close hitbox edges do not close or activate the wrong target", "[qt][tabnavigation][tab-followup]")
{
#ifdef TAB_NAVIGATION_INTEGRATION
    QtContext context;
#endif
    TabButton button;
    button.setText(QStringLiteral("Remote file list"));
    button.resize(220, button.normalHeight());
    button.show();
    settleLayout();
    auto *close = button.findChild<QToolButton *>("tabCloseButton");
    REQUIRE(close);
    QSignalSpy closed(&button, &TabButton::closeRequest);
    QSignalSpy activated(&button, &TabButton::clicked);
    int expected = 0;
    for (const auto &point : {QPoint(0, 0), QPoint(23, 0), QPoint(0, 23), QPoint(23, 23)}) {
        QTest::mouseClick(close, Qt::LeftButton, Qt::NoModifier, point);
        CHECK(closed.count() == ++expected);
    }
    CHECK(activated.isEmpty());
    QTest::mousePress(close, Qt::LeftButton, Qt::NoModifier, QPoint(12, 12));
    QTest::mouseRelease(close, Qt::LeftButton, Qt::NoModifier, QPoint(-1, 12));
    CHECK(closed.count() == expected);
    QTest::mouseClick(&button, Qt::LeftButton, Qt::NoModifier, button.titleRect().center());
    CHECK(closed.count() == expected);
    CHECK(activated.count() == 1);
}

TEST_CASE("Tab and overflow controls paint a keyboard focus cue", "[qt][tabnavigation][tab-followup]")
{
#ifdef TAB_NAVIGATION_INTEGRATION
    QtContext context;
#endif
    QWidget host;
    host.setPalette(tabPalette(false));
    host.setFocusPolicy(Qt::StrongFocus);
    host.resize(340, 80);
    TabButton tab(&host);
    tab.setText(QStringLiteral("Public Hubs"));
    tab.setChecked(true);
    tab.resize(180, tab.normalHeight());
    tab_navigation::AllTabsMenu menu(&host);
    auto *all = tab_navigation::makeAllTabsButton(&menu, &host);
    all->move(200, 0);
    auto *row = tab_navigation::makeScrollButton(Qt::DownArrow, &host);
    row->move(240, 0);
    host.show();
    host.activateWindow();
    settleLayout();
    for (QWidget *control : {static_cast<QWidget *>(&tab), static_cast<QWidget *>(all), static_cast<QWidget *>(row)}) {
        host.setFocus();
        control->setAttribute(Qt::WA_UnderMouse, false);
        const auto idle = paintWidget(*control, false);
        control->setFocus(Qt::TabFocusReason);
        REQUIRE(control->hasFocus());
        CHECK(paintWidget(*control, false) != idle);
    }
}

#ifdef TAB_NAVIGATION_INTEGRATION
TEST_CASE("Singleline close paint follows inactive and active tab surfaces", "[qt][tabnavigation][tab-followup]")
{
    for (bool dark : {false, true}) {
        QtContext context;
        const auto palette = tabPalette(dark);
        ApplicationSheet sheet(palette);
        context.window.setPalette(palette);
        ToolBar toolbar(&context.window);
        context.window.addToolBar(&toolbar);
        toolbar.initTabs();
        QList<FixtureTab *> tabs;
        for (int i = 0; i < 2; ++i) {
            auto *tab = new FixtureTab(i);
            tab->title = i == 0 ? QStringLiteral("Public Hubs") : QStringLiteral("Remote file list");
            tabs.append(tab);
            context.manager.add(tab);
        }
        context.window.resize(620, 180);
        context.window.show();
        context.window.activateWindow();
        settleLayout();
        auto *bar = toolbar.findChild<QTabBar *>();
        REQUIRE(bar);
        for (int index : {0, 1}) {
            context.manager.activate(tabs[1]);
            auto *close = qobject_cast<QToolButton *>(bar->tabButton(index, QTabBar::RightSide));
            REQUIRE(close);
            checkClosePaint(*close);
        }
        for (auto *tab : tabs)
            context.manager.rem(tab);
        settleLayout();
    }
}

TEST_CASE("Same-height multiline title reflow reveals the displaced active tab", "[qt][tabnavigation][tab-followup]")
{
    MultilineTabs fixture(7, true);
    fixture.fitTwoShortTabs();
    auto *active = fixture.button(5);
    REQUIRE(active);
    fixture.context.manager.activate(fixture.tabs[5]);
    fixture.area->verticalScrollBar()->setValue(0);
    settleLayout();
    REQUIRE(fixture.area->viewport()->rect().contains(fixture.visibleRect(active)));
    const QSize before = fixture.area->widget()->size();
    const int previousRow = active->y();
    fixture.tabs[0]->title = QString(200, QLatin1Char('W'));
    fixture.frame->redraw();
    settleLayout();
    REQUIRE(fixture.area->widget()->size() == before);
    REQUIRE(active->y() > previousRow);
    CHECK(fixture.area->viewport()->rect().contains(fixture.visibleRect(active)));
    fixture.area->verticalScrollBar()->setValue(0);
    fixture.frame->redraw();
    settleLayout();
    CHECK(fixture.area->verticalScrollBar()->value() == 0);
}

TEST_CASE("Same-height multiline reorder reveals after final tab placement", "[qt][tabnavigation][tab-followup]")
{
    MultilineTabs fixture(7, true);
    fixture.fitTwoShortTabs();
    auto *source = fixture.button(0);
    auto *target = fixture.button(6);
    REQUIRE(source);
    REQUIRE(target);
    fixture.context.manager.activate(fixture.tabs[0]);
    fixture.area->verticalScrollBar()->setValue(0);
    settleLayout();
    const QSize before = fixture.area->widget()->size();
    REQUIRE(QMetaObject::invokeMethod(fixture.frame, "slotDropped", Qt::DirectConnection,
        Q_ARG(TabButton*, source), Q_ARG(TabButton*, target)));
    settleLayout();
    REQUIRE(fixture.area->widget()->size() == before);
    REQUIRE(source->isChecked());
    CHECK(fixture.area->viewport()->rect().contains(fixture.visibleRect(source)));
}

TEST_CASE("Fine wheel input scrolls overflowing rows without changing selection", "[qt][tabnavigation][tab-followup]")
{
    MultilineTabs fixture(40);
    fixture.context.manager.activate(fixture.tabs[0]);
    auto *bar = fixture.area->verticalScrollBar();
    REQUIRE(bar->maximum() > 200);
    bar->setValue(40);
    auto *target = fixture.button(2);
    REQUIRE(target);
    QSignalSpy activated(&fixture.context.manager, &ArenaWidgetManager::activated);
    SECTION("pixel deltas retain their fine distance") {
        sendWheel(target, QPoint(0, -7), QPoint());
        CHECK(bar->value() == 47);
    }
    SECTION("small angle deltas are not discarded") {
        for (int step = 0; step < 4; ++step)
            sendWheel(target, QPoint(), QPoint(0, -30));
        CHECK(bar->value() > 40);
    }
    CHECK(activated.isEmpty());
    REQUIRE(fixture.button(0));
    CHECK(fixture.button(0)->isChecked());
    const int scrolled = bar->value();
    fixture.frame->redraw();
    settleLayout();
    CHECK(bar->value() == scrolled);
}

TEST_CASE("Fine angle deltas retain tab cycling when no rows overflow", "[qt][tabnavigation][tab-followup]")
{
    MultilineTabs fixture(2, true);
    REQUIRE(fixture.area->verticalScrollBar()->maximum() == 0);
    auto *first = fixture.button(0);
    REQUIRE(first);
    fixture.context.manager.activate(fixture.tabs[0]);
    QSignalSpy activated(&fixture.context.manager, &ArenaWidgetManager::activated);
    for (int step = 0; step < 3; ++step)
        sendWheel(first, QPoint(), QPoint(0, -30));
    CHECK(activated.isEmpty());
    sendWheel(first, QPoint(), QPoint(0, -30));
    REQUIRE(activated.count() == 1);
    CHECK(qvariant_cast<ArenaWidget *>(activated.first().first()) == fixture.tabs[1]);
}

TEST_CASE("Tab identity rejects stale menu selections and destroyed owners", "[qt][tabnavigation]")
{
    tab_navigation::Registry registry;
    auto *tab = new FixtureTab(1);
    const auto oldId = registry.add(tab);
    CHECK(registry.resolve(oldId) == tab);
    registry.remove(tab);
    CHECK(registry.resolve(oldId) == nullptr);
    const auto newId = registry.add(tab);
    CHECK(newId != oldId);
    delete tab;
    CHECK(registry.resolve(newId) == nullptr);
    auto *script = new ScriptWidget();
    auto *surface = new QWidget();
    script->setWidget(surface);
    const auto scriptId = registry.add(script);
    delete script;
    CHECK(registry.resolve(scriptId) == nullptr);
    delete surface;
}

TEST_CASE("Singleline tabs elide keyprint URLs and retain scroll and list access", "[qt][tabnavigation]")
{
    QtContext context;
    ToolBar toolbar(&context.window);
    context.window.addToolBar(&toolbar);
    toolbar.initTabs();
    QList<FixtureTab*> tabs;
    for (int i = 0; i < 40; ++i) {
        auto *tab = new FixtureTab(i);
        tabs << tab;
        context.manager.add(tab);
    }
    context.window.resize(620, 220);
    context.window.show();
    settle();
    auto *bar = toolbar.findChild<QTabBar*>();
    REQUIRE(bar);
    CHECK(bar->count() == 40);
    CHECK(bar->elideMode() == Qt::ElideRight);
    CHECK(bar->usesScrollButtons());
    CHECK(bar->tabText(0) == "archive-0.example.test:1511");
    CHECK(bar->tabToolTip(0).contains("kp=SHA256/"));
    for (int i = 0; i < bar->count(); ++i)
        CHECK(bar->tabRect(i).width() <= 240);
    auto *all = toolbar.findChild<QToolButton*>("allTabsButton");
    REQUIRE(all);
    CHECK(all->isVisible());
    CHECK(toolbar.rect().contains(all->mapTo(&toolbar, all->rect().center())));
    for (int width : {620, 320, 220}) {
        context.window.resize(width, 220);
        for (int selected : {0, 20, 39}) {
            context.manager.activate(tabs[selected]);
            settle();
            CHECK(bar->currentIndex() == selected);
            CHECK(bar->rect().intersects(bar->tabRect(selected)));
            auto *close = bar->tabButton(selected, QTabBar::RightSide);
            REQUIRE(close);
            CHECK(bar->rect().contains(close->geometry()));
            CHECK(all->isVisible());
        }
    }
    all->click();
    settle();
    auto *menu = toolbar.findChild<QMenu*>("allTabsMenu");
    REQUIRE(menu);
    auto *search = menu->findChild<QLineEdit*>();
    auto *list = menu->findChild<QListWidget*>();
    REQUIRE(search);
    REQUIRE(list);
    CHECK(list->count() == 40);
    search->setText("ARCHIVE-33.EXAMPLE");
    settle();
    REQUIRE(list->count() == 1);
    CHECK(list->item(0)->text().contains("archive-33"));
    QTest::keyClick(search, Qt::Key_Return);
    settle();
    CHECK(bar->currentIndex() == 33);
    const auto staleId = bar->tabData(33).toULongLong();
    context.manager.rem(tabs.takeAt(33));
    QSignalSpy activated(&context.manager, &ArenaWidgetManager::activated);
    REQUIRE(QMetaObject::invokeMethod(menu, "selected", Qt::DirectConnection, Q_ARG(quint64, staleId)));
    CHECK(activated.isEmpty());
    menu->close();
    for (auto *tab : tabs)
        context.manager.rem(tab);
    settle();
}

TEST_CASE("Long singleline moves preserve every activation and close identity", "[qt][tabnavigation]")
{
    QtContext context;
    ToolBar toolbar(&context.window);
    context.window.addToolBar(&toolbar);
    toolbar.initTabs();
    QList<FixtureTab*> order;
    for (int i = 0; i < 6; ++i) {
        order << new FixtureTab(i);
        context.manager.add(order.last());
    }
    auto *bar = toolbar.findChild<QTabBar*>();
    REQUIRE(bar);
    bar->moveTab(0, 5);
    order.move(0, 5);
    QSignalSpy activated(&context.manager, &ArenaWidgetManager::activated);
    for (int i = 0; i < order.size(); ++i) {
        activated.clear();
        bar->setCurrentIndex((i + 1) % order.size());
        bar->setCurrentIndex(i);
        REQUIRE_FALSE(activated.isEmpty());
        CHECK(qvariant_cast<ArenaWidget*>(activated.last().at(0)) == order[i]);
    }
    auto *close = bar->tabButton(2, QTabBar::RightSide);
    REQUIRE(close);
    for (int redraw = 0; redraw < 3; ++redraw) {
        emit context.timer.second();
        CHECK(bar->tabButton(2, QTabBar::RightSide) == close);
    }
    auto *button = qobject_cast<QToolButton*>(close);
    if (!button) button = close->findChild<QToolButton*>();
    REQUIRE(button);
    QSignalSpy removed(&context.manager, &ArenaWidgetManager::removed);
    button->click();
    REQUIRE(removed.count() == 1);
    CHECK(qvariant_cast<ArenaWidget*>(removed.first().at(0)) == order[2]);
    order.removeAt(2);
    for (auto *tab : order)
        context.manager.rem(tab);
    settle();
}

TEST_CASE("Tab mode singleton toggles keep actions and stale IDs separate", "[qt][tabnavigation]")
{
    for (bool multiline : {false, true}) {
        QtContext context;
        std::unique_ptr<QToolBar> toolbar;
        if (multiline) {
            toolbar = std::make_unique<MultiLineToolBar>(&context.window);
        } else {
            auto *single = new ToolBar(&context.window);
            single->initTabs();
            toolbar.reset(single);
        }
        auto *tab = new FixtureTab(1);
        tab->setState(ArenaWidget::Singleton | ArenaWidget::RaiseOnStart);
        QAction action;
        action.setCheckable(true);
        tab->setToolButton(&action);
        context.manager.add(tab);
        CHECK(action.isChecked());
        context.manager.toggle(tab);
        CHECK_FALSE(action.isChecked());
        CHECK(bool(tab->state() & ArenaWidget::Hidden));
        context.manager.toggle(tab);
        CHECK(action.isChecked());
        CHECK_FALSE(bool(tab->state() & ArenaWidget::Hidden));
        context.manager.rem(tab);
        delete tab;
    }
}

TEST_CASE("Multiline reorder and inactive close preserve the selected arena", "[qt][tabnavigation]")
{
    QtContext context;
    MultiLineToolBar toolbar(&context.window);
    context.window.addToolBar(&toolbar);
    auto *frame = toolbar.findChild<TabFrame*>();
    REQUIRE(frame);
    QList<FixtureTab*> tabs;
    for (int i = 0; i < 6; ++i) {
        tabs << new FixtureTab(i);
        context.manager.add(tabs.last());
    }
    auto buttons = frame->findChildren<TabButton*>();
    REQUIRE(buttons.size() == 6);
    REQUIRE(QMetaObject::invokeMethod(frame, "slotDropped", Qt::DirectConnection,
        Q_ARG(TabButton*, buttons[0]), Q_ARG(TabButton*, buttons[5])));
    CHECK(buttons[0]->isChecked());
    QSignalSpy activated(&context.manager, &ArenaWidgetManager::activated);
    context.manager.rem(tabs.takeAt(2));
    CHECK(activated.isEmpty());
    CHECK(buttons[0]->isChecked());
    QSignalSpy removed(&context.manager, &ArenaWidgetManager::removed);
    buttons[0]->findChild<QToolButton*>("tabCloseButton")->click();
    REQUIRE(removed.count() == 1);
    CHECK(qvariant_cast<ArenaWidget*>(removed.first().first()) == tabs.first());
    tabs.removeFirst();
    for (auto *tab : tabs)
        context.manager.rem(tab);
    settle();
}

TEST_CASE("Multiline overflow keeps the active tab reachable and lists offline tabs", "[qt][tabnavigation]")
{
    QtContext context;
    MultiLineToolBar toolbar(&context.window);
    context.window.addToolBar(&toolbar);
    QList<FixtureTab*> tabs;
    for (int i = 0; i < 40; ++i) {
        tabs << new FixtureTab(i);
        context.manager.add(tabs.last());
    }
    context.window.resize(390, 280);
    context.window.show();
    settle();
    auto *all = toolbar.findChild<QToolButton*>("allTabsButton");
    REQUIRE(all);
    CHECK(all->isVisible());
    CHECK(toolbar.height() <= 130);
    auto *frame = toolbar.findChild<TabFrame*>();
    REQUIRE(frame);
    for (auto *button : frame->findChildren<TabButton*>())
        CHECK(button->width() <= 240);
    auto *area = frame->findChild<QScrollArea*>();
    REQUIRE(area);
    for (int selected : {0, 20, 39}) {
        context.manager.activate(tabs[selected]);
        settle();
        TabButton *active = nullptr;
        for (auto *button : frame->findChildren<TabButton*>())
            if (button->isChecked()) active = button;
        REQUIRE(active);
        CHECK(area->viewport()->rect().contains(QRect(active->mapTo(area->viewport(), QPoint()), active->size())));
    }
    auto *previous = frame->findChild<QToolButton*>("previousTabRow");
    REQUIRE(previous);
    const int beforeScroll = area->verticalScrollBar()->value();
    previous->click();
    CHECK(area->verticalScrollBar()->value() < beforeScroll);
    const int scrolled = area->verticalScrollBar()->value();
    frame->redraw();
    settle();
    CHECK(area->verticalScrollBar()->value() == scrolled);
    all->click();
    settle();
    auto *menu = toolbar.findChild<QMenu*>("allTabsMenu");
    REQUIRE(menu);
    auto *list = menu->findChild<QListWidget*>();
    auto *search = menu->findChild<QLineEdit*>();
    REQUIRE(list);
    REQUIRE(search);
    REQUIRE(list->count() == 40);
    search->setText("archive-39");
    REQUIRE(list->count() == 1);
    context.manager.rem(tabs.takeLast());
    settle();
    CHECK(list->count() == 0);
    QTest::keyClick(search, Qt::Key_Return);
    menu->close();
    for (auto *tab : tabs)
        context.manager.rem(tab);
    settle();
}
#endif
