#include "AppIconTheme.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 2)
        return 2;
    const QStringList stems = {"configure", "server", "torrent", "refrlist", "find", "transfer",
        "go-down-search", "go-up-search", "favserver", "favusers", "log_file", "application-exit",
        "settings-main", "settings-connection", "settings-sharing", "settings-notifications",
        "settings-shortcuts", "settings-advanced"};
    const QStringList labels = {"Preferences", "Public hubs", "Torrents", "Refresh", "Search", "Transfers",
        "Downloads", "Uploads", "Fav. hubs", "Fav. users", "Live log", "Quit", "Main", "Connection",
        "Sharing", "Notifications", "Shortcuts", "Advanced"};
    QImage image(2440, 1800, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2);
    image.fill(QColor("#eef1f4"));
    QPainter painter(&image);
    const auto themes = app_icon_theme::modernIds();
    for (int row = 0; row < themes.size(); ++row) {
        const int top = row * 225;
        auto font = app.font();
        font.setPointSize(17);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#273746"));
        QString title = themes[row].left(1).toUpper() + themes[row].mid(1);
        if (themes[row] == "reborn")
            title += " (Default)";
        painter.drawText(QRect(20, top + 6, 1180, 32), Qt::AlignLeft | Qt::AlignVCenter, title);
        for (int dark = 0; dark < 2; ++dark) {
            const int y = top + 44 + dark * 84;
            const QColor color(dark ? "#252a30" : "#ffffff");
            painter.fillRect(12, y, 1196, 78, color);
            QPalette palette;
            palette.setColor(QPalette::Window, color);
            for (int index = 0; index < stems.size(); ++index) {
                const int x = 20 + index * 66;
                const auto icon = app_icon_theme::icon(app_icon_theme::resourcePath(themes[row]),
                    stems[index], palette).pixmap(QSize(28, 28), 2);
                if (icon.isNull())
                    return 3;
                painter.drawPixmap(x + 18, y + 10, icon);
                font.setBold(false);
                font.setPointSize(8);
                painter.setFont(font);
                painter.setPen(dark ? QColor("#d9e0e7") : QColor("#354251"));
                painter.drawText(QRect(x, y + 45, 64, 22), Qt::AlignHCenter | Qt::AlignTop, labels[index]);
            }
        }
    }
    painter.end();
    return image.save(QString::fromLocal8Bit(argv[1])) ? 0 : 4;
}
