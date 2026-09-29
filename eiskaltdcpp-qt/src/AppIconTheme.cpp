#include "AppIconTheme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QSvgRenderer>
#include <QTemporaryFile>
#include <QVariant>
#include <QtMath>
#include <cmath>

namespace app_icon_theme {

QString canonicalId(const QString &id)
{
    const auto trimmed = id.trimmed();
    if (trimmed.isEmpty() || trimmed.compare(QStringLiteral("apex"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("reborn");
    return trimmed;
}

QStringList modernIds()
{
    return {QStringLiteral("prism"), QStringLiteral("contour"),
            QStringLiteral("slate"), QStringLiteral("reborn")};
}

bool isModern(const QString &id)
{
    return modernIds().contains(canonicalId(id));
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightnessF() < 0.5;
}

QString resourcePath(const QString &id)
{
    return QStringLiteral(":/icon-themes/") + canonicalId(id);
}

QString assetPath(const QString &directory, const QString &stem, const QPalette &palette)
{
    const auto root = QDir::cleanPath(directory);
    const auto theme = QFileInfo(root).fileName();
    QStringList candidates;
    if (isDark(palette))
        candidates << root + QStringLiteral("/dark/") + stem + QStringLiteral(".svg");
    candidates << root + QLatin1Char('/') + stem + QStringLiteral(".svg")
               << root + QLatin1Char('/') + stem + QStringLiteral(".png");
    if (isModern(theme)) {
        // Identity/tray assets are deliberately not part of the redesign.
        candidates << root + QStringLiteral("/../apex/") + stem + QStringLiteral(".png")
                   << resourcePath(QStringLiteral("apex-brand")) + QLatin1Char('/') + stem + QStringLiteral(".png");
    }
    for (const auto &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

QString localFilePath(const QString &asset)
{
    if (!QFileInfo::exists(asset))
        return {};
    if (!asset.startsWith(QLatin1Char(':')))
        return asset;
    if (!QCoreApplication::instance())
        return {};
    // External notification daemons cannot open Qt resource URLs. Keep one
    // temporary copy per resource for the application lifetime.
    static QHash<QString, QPointer<QTemporaryFile>> copies;
    auto &copy = copies[asset];
    if (!copy) {
        copy = QTemporaryFile::createNativeFile(asset);
        if (!copy)
            return {};
        copy->setParent(QCoreApplication::instance());
    }
    return copy->fileName();
}

QPixmap pixmap(const QString &directory, const QString &stem, const QPalette &palette,
               int logicalSize, qreal devicePixelRatio)
{
    if (logicalSize <= 0 || !std::isfinite(devicePixelRatio) || devicePixelRatio <= 0)
        return {};
    const auto path = assetPath(directory, stem, palette);
    if (path.isEmpty())
        return {};
    const int pixels = qCeil(logicalSize * devicePixelRatio);
    QPixmap result;
    if (path.endsWith(QStringLiteral(".svg"))) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid())
            return {};
        result = QPixmap(pixels, pixels);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        renderer.render(&painter);
    } else {
        result.load(path);
        result = result.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    result.setDevicePixelRatio(devicePixelRatio);
    return result;
}

QIcon icon(const QString &directory, const QString &stem, const QPalette &palette, const QIcon &fallback)
{
    const auto path = assetPath(directory, stem, palette);
    return path.isEmpty() ? fallback : QIcon(path);
}

QIcon highlightedIcon(const QString &normalPath, const QString &activePath)
{
    // Keep SVG engines intact in every state; addPixmap would freeze the
    // artwork at one resolution before the toolbar knows its size or DPI.
    QIcon result(normalPath);
    if (!activePath.isEmpty()) {
        for (const auto mode : {QIcon::Active, QIcon::Selected})
            result.addFile(activePath, QSize(), mode, QIcon::Off);
        for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected})
            result.addFile(activePath, QSize(), mode, QIcon::On);
    }
    return result;
}

QString activePath()
{
    const auto app = QCoreApplication::instance();
    const auto path = app ? app->property("appIconThemePath").toString() : QString();
    return path.isEmpty() ? resourcePath(QStringLiteral("reborn")) : path;
}

void setActivePath(const QString &directory)
{
    if (auto *app = QCoreApplication::instance())
        app->setProperty("appIconThemePath", directory);
}

}
