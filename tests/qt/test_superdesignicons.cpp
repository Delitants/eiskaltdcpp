#include "AppIconTheme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSvgRenderer>
#include <QTextStream>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool condition, const QString &message) {
        if (!condition) {
            QTextStream(stderr) << "FAIL: " << message << '\n';
            ++failures;
        }
    };
    using namespace app_icon_theme;
    check(canonicalId("") == "superdesign", "fresh default");
    check(canonicalId("   ") == "superdesign", "unset whitespace default");
    check(canonicalId(" Apex ") == "reborn", "preserve existing Apex migration");
    for (const auto &id : {"prism", "contour", "slate", "reborn", "default", "faenza", "haiku", "monochrome"})
        check(canonicalId(id) == id, QString("preserve saved selection: ") + id);
    for (const auto &id : {"prism", "contour", "slate", "reborn", "superdesign"})
        check(isModern(id), QString("selectable modern family: ") + id);
    check(activePath() == resourcePath("superdesign"), "unset active path");
    setActivePath(resourcePath("reborn"));
    check(activePath() == resourcePath("reborn"), "explicit active path preserved");
    const auto root = resourcePath("superdesign");
    const auto names = QDir(resourcePath("reborn")).entryList({"*.svg"}, QDir::Files);
    check(names.size() >= 75, "baseline resource coverage");
    check(QDir(root).entryList({"*.svg"}, QDir::Files) == names, "new resource coverage");
    for (const auto &color : {QColor(Qt::white), QColor("#25282d")}) {
        QPalette palette;
        palette.setColor(QPalette::Window, color);
        for (const auto &name : names) {
            const auto stem = name.chopped(4);
            const auto path = assetPath(root, stem, palette);
            check(path.startsWith(root + '/'), "own vector asset: " + stem);
            check(path.contains("/dark/") == isDark(palette), "surface selection: " + stem);
            QSvgRenderer renderer(path);
            check(renderer.isValid(), "valid SVG: " + stem);
            for (const auto dpr : {1.0, 2.0}) {
                const auto image = pixmap(root, stem, palette, 22, dpr);
                check(!image.isNull() && image.size() == QSize(22 * dpr, 22 * dpr), "render: " + stem);
            }
        }
        for (const auto &stem : {"icon_appl", "icon_appl_big", "icon_msg", "icon_msg_big", "qt-logo"})
            check(assetPath(root, stem, palette) == QString(":/icon-themes/apex-brand/") + stem + ".png", "original branding fallback");
        QPixmap fallback(16, 16);
        fallback.fill(Qt::red);
        check(icon(root, "does-not-exist", palette, QIcon(fallback)).pixmap(16, 16).toImage()
              == fallback.toImage(), "missing icon caller fallback");
        check(assetPath(root, "does-not-exist", palette).isEmpty(), "missing asset safe failure");
    }
    return failures ? 1 : 0;
}
