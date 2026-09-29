// Built only by the isolated fixture harness, whose services cannot read live settings.
#include "TabNavigationFixture.h"
#include "ToolBar.h"
#include "MultiLineToolBar.h"
#include "TabButton.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTabBar>
#include <QTest>
#include <QToolButton>
#include <memory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Isolated Tab Navigation Fixture"));
    app.setFont(QFont(QStringLiteral("Lucida Grande"), 13));
    if (argc != 2)
        return 2;
    const QString directory = QString::fromLocal8Bit(argv[1]);
    const int requestedCount = qEnvironmentVariableIntValue("EISKALT_QA_TAB_COUNT");
    const int count = requestedCount > 0 ? qBound(2, requestedCount, 40) : 40;
    QDir().mkpath(directory);
    for (bool dark : {false, true}) {
        for (bool multiline : {false, true}) {
            for (int width : {1000, 390, 220}) {
                QtContext context;
                auto &window = context.window;
                auto palette = app.palette();
                palette.setColor(QPalette::Window, QColor(dark ? "#292e34" : "#eeeeee"));
                palette.setColor(QPalette::Button, QColor(dark ? "#30363d" : "#d6d6d6"));
                palette.setColor(QPalette::Base, QColor(dark ? "#252a30" : "#ffffff"));
                for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
                    palette.setColor(role, QColor(dark ? "#f0f3f5" : "#20252b"));
                palette.setColor(QPalette::Mid, QColor(dark ? "#687480" : "#9aa3ab"));
                palette.setColor(QPalette::Highlight, QColor(dark ? "#72b8ed" : "#2477b8"));
                window.setPalette(palette);
                std::unique_ptr<QToolBar> toolbar;
                if (multiline) {
                    toolbar = std::make_unique<MultiLineToolBar>(&window);
                } else {
                    auto *single = new ToolBar(&window);
                    single->initTabs();
                    toolbar.reset(single);
                }
                window.addToolBar(toolbar.get());
                window.setWindowTitle(QStringLiteral("Synthetic %1-tab fixture").arg(count));
                auto *label = new QLabel(QStringLiteral("%1 synthetic tabs, including offline keyprint URLs.\n"
                    "Active: blue underline. Unread: orange icon.\nNo live application or settings are used."));
                label->setText(label->text().arg(count));
                label->setWordWrap(true);
                label->setMargin(12);
                window.setCentralWidget(label);
                QList<FixtureTab*> tabs;
                for (int i = 0; i < count; ++i) {
                    auto *tab = new FixtureTab(i);
                    tab->unread = i % 7 == 0;
                    tabs << tab;
                    context.manager.add(tab);
                }
                window.resize(width, 240);
                window.show();
                context.manager.activate(tabs[qMin(35, count - 1)]);
                emit context.timer.second();
                QTest::qWait(80);
                const auto name = QString("%1-%2-%3").arg(multiline ? "multiline" : "singleline")
                    .arg(dark ? "dark" : "light").arg(width);
                if (!window.grab().save(directory + QLatin1Char('/') + name + ".png"))
                    return 3;
                if (width == 390) {
                    auto *all = toolbar->findChild<QToolButton*>("allTabsButton");
                    all->click();
                    QTest::qWait(40);
                    auto *menu = toolbar->findChild<QMenu*>("allTabsMenu");
                    if (!menu->grab().save(directory + QLatin1Char('/') + name + "-all-tabs.png"))
                        return 4;
                    menu->findChild<QLineEdit*>()->setText(QStringLiteral("archive-33"));
                    QTest::qWait(20);
                    if (!menu->grab().save(directory + QLatin1Char('/') + name + "-search.png"))
                        return 5;
                    menu->hide();
                }
                for (auto *tab : tabs)
                    context.manager.rem(tab);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        }
    }
    return 0;
}
