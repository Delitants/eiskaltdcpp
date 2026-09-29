#include "ActionToolBar.h"
#include "AppIconTheme.h"
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QTabBar>
#include <QToolButton>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QDir().mkpath(QString::fromLocal8Bit(argv[1]));
    app.setFont(QFont(QStringLiteral("Lucida Grande"), 13));
    const QStringList stems = {"configure", "torrent", "server", "own_filelist", "refrlist", "reconnect",
        "network-connect", "favserver", "favusers", "find", "transfer", "download", "go-down-search",
        "go-up-search", "slow", "edit-clear", "edit-find", "edit-delete", "log_file", "application-exit"};
    const QStringList labels = {"Preferences", "Torrents", "Public hubs", "Files", "Refresh share", "Reconnect",
        "Connect", "Favourite hubs", "Favourite users", "Search", "Transfers", "Queue", "Downloaded",
        "Uploaded", "Speed limit", "Clear chat", "Find/Filter", "Chat", "Live Log", "Quit"};
    const QStringList modeNames = {"Icons only", "Text only", "Text beside icons", "Text under icons"};
    QImage sheet(2400, 2880, QImage::Format_ARGB32_Premultiplied);
    sheet.setDevicePixelRatio(2);
    sheet.fill(QColor("#e7ebf0"));
    QPainter painter(&sheet);
    for (int dark = 0; dark < 2; ++dark) {
        for (int mode = 0; mode < 4; ++mode) {
            QMainWindow window;
            QPalette palette = app.palette();
            if (dark) {
                palette.setColor(QPalette::Window, QColor("#292e34"));
                palette.setColor(QPalette::Button, QColor("#30363d"));
                palette.setColor(QPalette::Base, QColor("#252a30"));
                palette.setColor(QPalette::WindowText, QColor("#f0f3f5"));
                palette.setColor(QPalette::ButtonText, QColor("#f0f3f5"));
                palette.setColor(QPalette::Text, QColor("#f0f3f5"));
                palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#889099"));
            }
            window.setPalette(palette);
            ActionToolBar actions(&window);
            QToolBar tabs(&window);
            QTabBar tabbar;
            tabbar.setExpanding(false);
            tabbar.setUsesScrollButtons(true);
            tabbar.setDocumentMode(true);
            tabbar.setTabsClosable(true);
            tabbar.setElideMode(Qt::ElideNone);
            for (int i = 0; i < 15; ++i)
                tabbar.addTab(QStringLiteral("Hub %1 / Community").arg(i + 1));
            tabs.addWidget(&tabbar);
            for (int i = 0; i < labels.size(); ++i) {
                const auto icon = app_icon_theme::icon(app_icon_theme::resourcePath("reborn"), stems[i], palette);
                if (icon.isNull())
                    qFatal("Preview icon missing: %s", qPrintable(stems[i]));
                auto *action = actions.addAction(icon, labels[i]);
                if (i == 7) {
                    auto *menu = new QMenu(&window);
                    menu->addAction("Favourite hub one");
                    menu->addAction("Favourite hub two");
                    action->setMenu(menu);
                    auto *button = qobject_cast<QToolButton*>(actions.widgetForAction(action));
                    button->setPopupMode(QToolButton::MenuButtonPopup);
                }
                if (i == 10) {
                    action->setCheckable(true);
                    action->setChecked(true);
                }
                if (i == 14)
                    action->setEnabled(false);
                if (i == 11 && dark)
                    action->setText(QString::fromUtf8("Черга завантажень"));
                if (i == 12 && dark)
                    action->setText(QStringLiteral("Abgeschlossene Downloads"));
            }
            actions.setToolButtonStyle(static_cast<Qt::ToolButtonStyle>(mode));
            action_toolbar::separateRows(window, &actions, &tabs, nullptr);
            window.setCentralWidget(new QLabel("Chat and transfers remain below both rows"));
            window.resize(1160, 150);
            window.show();
            QApplication::sendPostedEvents();
            QApplication::processEvents();
            const QPixmap capture = window.grab();
            if (!capture.save(QString::fromLocal8Bit(argv[1]) + QString("/toolbar-%1-%2.png").arg(dark ? "dark" : "light").arg(mode)))
                return 3;
            const int top = (dark * 4 + mode) * 180;
            painter.setFont(QFont("Lucida Grande", 12, QFont::Bold));
            painter.setPen(QColor("#283646"));
            painter.drawText(20, top + 20, modeNames[mode] + (dark ? " / Dark + long translations" : " / Light"));
            painter.drawPixmap(QPoint(20, top + 30), capture, QRect(0, 0,
                capture.width(), qMin(capture.height(), qRound((actions.height() + tabs.height()) * capture.devicePixelRatio()))));
        }
    }
    painter.end();
    return sheet.save(QString::fromLocal8Bit(argv[1]) + "/all-modes.png") ? 0 : 4;
}
