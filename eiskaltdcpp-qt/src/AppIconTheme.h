#pragma once

#include <QIcon>
#include <QPalette>
#include <QStringList>

namespace app_icon_theme {

QString canonicalId(const QString &id);
QStringList modernIds();
bool isModern(const QString &id);
bool isDark(const QPalette &palette);
QString resourcePath(const QString &id);
QString assetPath(const QString &directory, const QString &stem, const QPalette &palette);
QString localFilePath(const QString &asset);
QPixmap pixmap(const QString &directory, const QString &stem, const QPalette &palette,
               int logicalSize, qreal devicePixelRatio);
QIcon icon(const QString &directory, const QString &stem, const QPalette &palette,
           const QIcon &fallback = QIcon());
QIcon highlightedIcon(const QString &normalPath, const QString &activePath);

// Shared with lightweight windows that do not depend on the DC context.
QString activePath();
void setActivePath(const QString &directory);

}
