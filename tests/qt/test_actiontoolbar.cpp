#include "ActionToolBar.h"
#include "AppIconTheme.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDir>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPaintEngine>
#include <QProxyStyle>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyleOptionToolButton>
#include <QTabBar>
#include <QToolButton>
#include <QTest>

namespace {
void settle()
{
    QApplication::sendPostedEvents();
    QApplication::processEvents();
}

QString visibleCaption(QString text)
{
    text.replace("&&", QString(QChar(0x001f)));
    text.remove('&');
    return text.replace(QChar(0x001f), QLatin1Char('&'));
}

// Observe the option produced by QToolButton itself, not a test approximation
// of its font, menu features, or allocated paint rectangle.
class CaptionPaintEngine final : public QPaintEngine
{
public:
    CaptionPaintEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}
    bool begin(QPaintDevice *) override { setActive(true); return true; }
    bool end() override { setActive(false); return true; }
    Type type() const override { return QPaintEngine::User; }
    void updateState(const QPaintEngineState &) override {}
    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override {}
    void drawPath(const QPainterPath &) override {}
    void drawTextItem(const QPointF &, const QTextItem &item) override { text += item.text(); }
    QString text;
};

class CaptionPaintDevice final : public QPaintDevice
{
public:
    explicit CaptionPaintDevice(QPaintDevice *reference) : reference(reference) {}
    QPaintEngine *paintEngine() const override { return &engine; }
    mutable CaptionPaintEngine engine;
protected:
    int metric(PaintDeviceMetric metric) const override
    {
        switch (metric) {
        case PdmWidth: return reference->width();
        case PdmHeight: return reference->height();
        case PdmWidthMM: return reference->widthMM();
        case PdmHeightMM: return reference->heightMM();
        case PdmDpiX: return reference->logicalDpiX();
        case PdmDpiY: return reference->logicalDpiY();
        case PdmPhysicalDpiX: return reference->physicalDpiX();
        case PdmPhysicalDpiY: return reference->physicalDpiY();
        case PdmDepth: return reference->depth();
        case PdmNumColors: return reference->colorCount();
        case PdmDevicePixelRatio: return reference->devicePixelRatio();
        case PdmDevicePixelRatioScaled: return qRound(reference->devicePixelRatioF() * devicePixelRatioFScale());
        default: return QPaintDevice::metric(metric);
        }
    }
private:
    QPaintDevice *reference;
};

class ButtonPaintProbe final : public QProxyStyle
{
public:
    explicit ButtonPaintProbe(QStyle *delegate) : QProxyStyle("Fusion"), delegate(delegate) {}
    QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                           const QSize &size, const QWidget *widget) const override
    {
        return delegate->sizeFromContents(type, option, size, widget);
    }
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget) const override
    {
        return delegate->subControlRect(control, option, subControl, widget);
    }
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override
    {
        if (control == CC_ToolButton) {
            if (const auto *button = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
                painted = true;
                actual = *button;
                actionRect = delegate->subControlRect(control, button, SC_ToolButton, widget);
                menuRect = delegate->subControlRect(control, button, SC_ToolButtonMenu, widget);
                CaptionPaintDevice capture(painter->device());
                QPainter recording(&capture);
                delegate->drawComplexControl(control, option, &recording, widget);
                drawnCaption = capture.engine.text;
            }
        }
        delegate->drawComplexControl(control, option, painter, widget);
    }
    mutable bool painted = false;
    mutable QStyleOptionToolButton actual;
    mutable QRect actionRect;
    mutable QRect menuRect;
    mutable QString drawnCaption;
private:
    QStyle *delegate;
};

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
    window.resize(640, 400);
    window.show();
    for (const auto mode : {Qt::ToolButtonIconOnly, Qt::ToolButtonTextOnly,
                           Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon}) {
        toolbar.setToolButtonStyle(mode);
        settle();
        INFO(static_cast<int>(mode));
        for (auto *action : actions) {
            auto *button = qobject_cast<QToolButton*>(toolbar.widgetForAction(action));
            REQUIRE(button);
            if (mode == Qt::ToolButtonTextUnderIcon) {
                const auto label = visibleCaption(button->text());
                const auto metrics = button->fontMetrics();
                const int inkWidth = qMax(metrics.horizontalAdvance(label), metrics.boundingRect(label).width());
                CHECK(button->sizeHint().width() >= qMax(toolbar.iconSize().width(), inkWidth));
                CHECK(button->sizeHint().width() <= qMax(toolbar.iconSize().width(), inkWidth) + 16);
            } else {
                const int limit = mode == Qt::ToolButtonTextBesideIcon ? 208 :
                    mode == Qt::ToolButtonTextOnly ? 168 : 48;
                CHECK(button->sizeHint().width() <= limit);
            }
            CHECK(button->sizeHint().height() >= 24);
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
        REQUIRE_FALSE(menu->actions().isEmpty());
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
        window.resize(640, 400);
    }
}

TEST_CASE("Under-icon captions fit actual paint geometry after font and mode changes", "[qt][actiontoolbar][caption-geometry]")
{
    const auto applicationFont = QApplication::font();
    const auto restoreApplicationFont = qScopeGuard([&] {
        QApplication::setFont(applicationFont);
        settle();
    });
    QMainWindow window;
    ActionToolBar toolbar(&window);
    window.addToolBar(&toolbar);
    const QStringList labels = {QStringLiteral("Preferences"), QStringLiteral("Torrents"),
        QStringLiteral("Files"), QStringLiteral("&Search"), QStringLiteral("Chat && messages"),
        QString::fromUtf8("Черга завантажень"),
        QStringLiteral("Geschwindigkeitsbegrenzung einschalten / ausschalten")};
    QList<QAction*> actions;
    for (const auto &label : labels)
        actions << toolbar.addAction(app_icon_theme::icon(app_icon_theme::resourcePath("superdesign"),
            "configure", toolbar.palette()), label);
    auto *split = qobject_cast<QToolButton*>(toolbar.widgetForAction(actions.at(1)));
    REQUIRE(split);
    QMenu splitMenu;
    splitMenu.addAction("Saved choice");
    split->setMenu(&splitMenu);
    split->setPopupMode(QToolButton::MenuButtonPopup);
    window.show();
    const auto originalFont = toolbar.font();
    for (const int pixels : {12, 18, 24}) {
        auto font = originalFont;
        font.setPixelSize(pixels);
        QApplication::setFont(font);
        settle();
        CHECK(toolbar.font() == font);
        for (const auto previous : {Qt::ToolButtonIconOnly, Qt::ToolButtonTextOnly,
                                    Qt::ToolButtonTextBesideIcon}) {
            toolbar.setToolButtonStyle(previous);
            settle();
            toolbar.setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            // Scroll the longest translation into a first-position slot too:
            // hiding it at narrow widths must not hide a sizing regression.
            for (const bool longestFirst : {false, true}) {
                if (longestFirst) {
                    toolbar.removeAction(actions.last());
                    toolbar.insertAction(actions.first(), actions.last());
                }
                for (const int width : {480, 960, 2400}) {
                    window.resize(width, 500);
                    settle();
                    INFO("font px=" << pixels << " previous=" << int(previous)
                         << " window=" << width << " actual=" << window.width());
                    int visibleCount = 0;
                    for (auto *action : actions) {
                        auto *button = qobject_cast<QToolButton*>(toolbar.widgetForAction(action));
                        REQUIRE(button);
                        if (button->isHidden())
                            continue;
                        ++visibleCount;
                        const QSize allocated = button->size();
                        const QSize hint = button->sizeHint();
                        auto *originalStyle = button->style();
                        ButtonPaintProbe probe(originalStyle);
                        button->setStyle(&probe);
                        const auto restoreStyle = qScopeGuard([&] { button->setStyle(originalStyle); });
                        for (const qreal dpr : {1.0, 2.0}) {
                            QImage image(allocated * dpr, QImage::Format_ARGB32_Premultiplied);
                            image.setDevicePixelRatio(dpr);
                            image.fill(Qt::transparent);
                            button->render(&image);
                            REQUIRE(probe.painted);
                            const auto captureDirectory = qEnvironmentVariable("EISKALT_TOOLBAR_CAPTURE_DIR");
                            if (!captureDirectory.isEmpty() && pixels == 12 && previous == Qt::ToolButtonIconOnly
                                && !longestFirst && width == 960 && actions.indexOf(action) < 3) {
                                REQUIRE(QDir().mkpath(captureDirectory));
                                CHECK(image.save(QDir(captureDirectory).filePath(
                                    QStringLiteral("%1-%2px-dpr%3.png").arg(visibleCaption(action->text()))
                                        .arg(pixels).arg(dpr))));
                            }
                            const auto &option = probe.actual;
                            const QFontMetrics metrics(option.font);
                            const auto label = visibleCaption(option.text);
                            const int textWidth = qMax(metrics.horizontalAdvance(label), metrics.boundingRect(label).width());
                            // The intended compact contract is at most 4px
                            // per side, 2px above/below and 2px icon/text gap.
                            const auto content = probe.actionRect.adjusted(4, 2, -4, -2);
                            INFO("label=" << label.toStdString() << " DPR=" << dpr
                                 << " allocated=" << allocated.width() << 'x' << allocated.height()
                                 << " hint=" << hint.width() << 'x' << hint.height()
                                 << " SC=" << probe.actionRect.x() << ',' << probe.actionRect.y()
                                 << ' ' << probe.actionRect.width() << 'x' << probe.actionRect.height()
                                 << " font=" << option.font.toString().toStdString()
                                 << " widgetFont=" << button->font().toString().toStdString()
                                 << " textWidth=" << textWidth
                                 << " painted=" << probe.drawnCaption.toStdString()
                                 << " advanceF=" << QFontMetricsF(option.font).horizontalAdvance(label));
                            CHECK(probe.drawnCaption == label);
                            CHECK(button->size() == allocated);
                            CHECK(option.font == button->font());
                            CHECK(option.rect == button->rect());
                            CHECK(allocated.width() >= hint.width());
                            CHECK(content.width() >= textWidth);
                            CHECK(probe.actionRect.width() <= qMax(option.iconSize.width(), textWidth) + 16);
                            CHECK(content.height() >= option.iconSize.height() + 2 + metrics.height());
                            CHECK(allocated.height() <= option.iconSize.height() + metrics.height() + 8);
                            CHECK(toolbar.rect().contains(button->geometry()));
                            CHECK(toolbar.visibleRegion().contains(button->geometry()));
                            if (button == split) {
                                CHECK_FALSE(probe.menuRect.isEmpty());
                                CHECK_FALSE(probe.actionRect.intersects(probe.menuRect));
                            }
                        }
                    }
                    auto *extension = toolbar.findChild<QToolButton*>("qt_toolbar_ext_button");
                    REQUIRE(extension);
                    if (extension->isVisible()) {
                        extension->click();
                        settle();
                        auto *overflow = toolbar.findChild<QMenu*>("actionToolbarOverflow");
                        REQUIRE(overflow);
                        REQUIRE(overflow->isVisible());
                        for (auto *action : actions) {
                            const auto *widget = toolbar.widgetForAction(action);
                            REQUIRE(widget);
                            const bool inOverflow = overflow->actions().contains(action)
                                || (action == actions.at(1) && overflow->actions().contains(splitMenu.menuAction()));
                            CHECK((!widget->isHidden() || inOverflow));
                            if (!widget->isHidden()) {
                                CHECK_FALSE(inOverflow);
                                CHECK_FALSE(widget->geometry().intersects(extension->geometry()));
                            }
                        }
                        overflow->close();
                    } else {
                        CHECK(visibleCount == actions.size());
                    }
                }
                if (longestFirst) {
                    toolbar.removeAction(actions.last());
                    toolbar.addAction(actions.last());
                }
            }
        }
    }
}

TEST_CASE("Compact text toolbar uses content widths and keeps all overflow actions reachable", "[qt][actiontoolbar][compact-native]")
{
    QMainWindow window;
    ActionToolBar toolbar(&window);
    window.addToolBar(&toolbar);
    toolbar.setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *shortAction = toolbar.addAction(QStringLiteral("Go"));
    const auto rest = populate(toolbar);
    window.resize(960, 500);
    window.show();
    settle();
    auto *shortButton = qobject_cast<QToolButton*>(toolbar.widgetForAction(shortAction));
    REQUIRE(shortButton);
    CHECK(shortButton->sizeHint().width() <= shortButton->fontMetrics().horizontalAdvance("Go") + 16);
    CHECK(shortButton->sizeHint().height() <= qMax(28, shortButton->fontMetrics().height() + 8));
    for (int width : {960, 1186, 1600}) {
        window.resize(width, 500);
        settle();
        auto *extension = toolbar.findChild<QToolButton*>("qt_toolbar_ext_button");
        REQUIRE(extension);
        REQUIRE(extension->isVisible());
        QTest::keyClick(extension, Qt::Key_Down);
        settle();
        auto *menu = toolbar.findChild<QMenu*>("actionToolbarOverflow");
        REQUIRE(menu);
        REQUIRE(menu->isVisible());
        for (auto *action : rest) {
            auto *widget = toolbar.widgetForAction(action);
            REQUIRE(widget);
            CHECK((!widget->isHidden() || menu->actions().contains(action)));
        }
        CHECK(menu->isWindow());
        CHECK(menu->height() > toolbar.height());
        menu->close();
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
