/*
 * Stubs for WulforUtil methods referenced by WulforSettings.cpp and
 * Antispam.cpp but not needed in Qt-only tests.  These satisfy the linker
 * without pulling in the full WulforUtil.cpp (which drags in MainWindow,
 * ClientManager, icon loading, etc.).
 */

#include <QString>
#include <QLocale>
#include "WulforUtil.h"

QString WulforUtil::getTranslationsPath() const {
    return QString();
}

QString WulforUtil::getNicks(const QString & /*cid*/, const QString & /*hintUrl*/) {
    return QString();
}

QString WulforUtil::getNicks(const dcpp::CID & /*cid*/, const QString & /*hintUrl*/) {
    return QString();
}

QString WulforUtil::formatBytes(int64_t bytes) {
    // Use binary units without requiring the application's SettingsManager.
    return QLocale::c().formattedDataSize(bytes, 2, QLocale::DataSizeIecFormat);
}

const QPixmap &WulforUtil::getPixmap(Icons) {
    static const QPixmap pixmap;
    return pixmap;
}

const QPixmap &WulforUtil::getPixmapForFile(const QString &) {
    static const QPixmap pixmap;
    return pixmap;
}
