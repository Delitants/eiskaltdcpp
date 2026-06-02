/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#include "MainWindow.h"
#include "Magnet.h"
#include "WulforUtil.h"
#include "ArenaWidgetFactory.h"
#include "QtContext.h"
#include "QtContextAware.h"
#include "icons/gv.xpm"

#ifdef HAVE_IFADDRS_H
#include <sys/types.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <fcntl.h>
#endif
#include "dcpp/ClientManager.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/Util.h"
#include "dcpp/AdcHub.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/Text.h"

#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QAbstractItemModel>
#include <QHostAddress>
#include <QMenu>
#include <QHeaderView>
#include <QResource>
#include <QTimer>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QGridLayout>
#include <QPushButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QRegularExpression>
#include <QProcess>
#include <QLocale>
#include <QImageReader>

#include <QUrlQuery>

#include "SearchFrame.h"
#include "extra/magnet.h"

#ifndef CLIENT_DATA_DIR
#define CLIENT_DATA_DIR ""
#endif

#ifndef CLIENT_ICONS_DIR
#define CLIENT_ICONS_DIR ""
#endif

#ifndef CLIENT_TRANSLATIONS_DIR
#define CLIENT_TRANSLATIONS_DIR ""
#endif

#ifndef CLIENT_SOUNDS_DIR
#define CLIENT_SOUNDS_DIR ""
#endif

#ifndef CLIENT_RES_DIR
#define CLIENT_RES_DIR ""
#endif

using namespace dcpp;

const QString WulforUtil::magnetSignature = "magnet:?xt=urn:tree:tiger:";

namespace {
bool isPreviewableImagePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return false;

    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("heic") || suffix == QStringLiteral("heif"))
        return true;

    const auto formats = QImageReader::supportedImageFormats();
    for (const QByteArray &format : formats) {
        if (suffix == QString::fromLatin1(format).toLower())
            return true;
    }

    return false;
}

void showPreviewableImage(QWidget *parent, const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QFileInfo(path).fileName());
    dialog->setMinimumSize(480, 360);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *scrollArea = new QScrollArea(dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setAlignment(Qt::AlignCenter);

    auto *label = new QLabel(scrollArea);
    label->setAlignment(Qt::AlignCenter);
    label->setBackgroundRole(QPalette::Base);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    const QPixmap pixmap = QPixmap::fromImage(image);
    QSize previewSize = pixmap.size();
    previewSize.scale(1200, 900, Qt::KeepAspectRatio);
    label->setPixmap(pixmap.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    label->resize(label->pixmap(Qt::ReturnByValue).size());
    scrollArea->setWidget(label);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto *openExternal = new QPushButton(QObject::tr("Open Externally"), dialog);
    auto *close = new QPushButton(QObject::tr("Close"), dialog);
    buttonRow->addWidget(openExternal);
    buttonRow->addWidget(close);

    QObject::connect(openExternal, &QPushButton::clicked, dialog, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::accept);

    layout->addWidget(scrollArea, 1);
    layout->addLayout(buttonRow);
    dialog->resize(previewSize.boundedTo(QSize(1200, 900)) + QSize(48, 96));
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool openWithConfiguredExternalHandler(const QString &url)
{
    const QString handler = _q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MIME_HANDLER, true)).trimmed();

    if (handler.isEmpty())
        return false;

#ifdef Q_OS_MAC
    const QFileInfo handlerInfo(handler);
    if (handler.endsWith(QStringLiteral(".app"), Qt::CaseInsensitive) && handlerInfo.isDir())
        return QProcess::startDetached(QStringLiteral("/usr/bin/open"), QStringList() << QStringLiteral("-a") << handler << url);
#endif

    return QProcess::startDetached(handler, QStringList(url));
}

enum class AdcTagMode {
    Unknown,
    Active,
    Passive
};

AdcTagMode adcTagMode(const Identity &identity)
{
    const std::string tag = identity.getTag();

    for (size_t pos = tag.find("M:"); pos != std::string::npos; pos = tag.find("M:", pos + 2)) {
        if (pos > 0 && tag[pos - 1] != '<' && tag[pos - 1] != ',' && tag[pos - 1] != ' ')
            continue;

        if (pos + 2 >= tag.size())
            continue;

        switch (tag[pos + 2]) {
        case 'A':
        case 'a':
            return AdcTagMode::Active;
        case 'P':
        case 'p':
            return AdcTagMode::Passive;
        default:
            break;
        }
    }

    return AdcTagMode::Unknown;
}

bool isPassiveForUserIcon(const Identity &identity, const UserPtr &user)
{
    if (!user)
        return false;

    switch (adcTagMode(identity)) {
    case AdcTagMode::Active:
        return false;
    case AdcTagMode::Passive:
        return true;
    case AdcTagMode::Unknown:
        break;
    }

    if (user->isSet(User::NMDC))
        return user->isSet(User::PASSIVE);

    return !identity.isTcpActive();
}

bool showPassiveOverlayForApexIcon(const Identity &identity, const UserPtr &user)
{
    if (!user)
        return false;

    switch (adcTagMode(identity)) {
    case AdcTagMode::Active:
        return false;
    case AdcTagMode::Passive:
        return true;
    case AdcTagMode::Unknown:
        break;
    }

    if (user->isSet(User::NMDC))
        return user->isSet(User::PASSIVE);

    return isPassiveForUserIcon(identity, user) && !identity.supports(AdcHub::NAT0_FEATURE);
}

int legacyConnectionIconColumn(const QString &connection)
{
    const QString conn = connection.trimmed().toUpper();

    if (conn.isEmpty())
        return -1;

    if (conn == QStringLiteral("MODEM") ||
        conn == QStringLiteral("28.8KBPS") ||
        conn == QStringLiteral("33.6KBPS") ||
        conn == QStringLiteral("56KBPS"))
        return 0;

    if (conn == QStringLiteral("ISDN") || conn.startsWith(QStringLiteral("ISDN ")))
        return 1;

    if (conn == QStringLiteral("DSL") ||
        conn == QStringLiteral("ADSL") ||
        conn == QStringLiteral("VDSL") ||
        conn.endsWith(QStringLiteral(" DSL")) ||
        conn.endsWith(QStringLiteral("DSL2")))
        return 2;

    if (conn == QStringLiteral("CABLE") || conn.endsWith(QStringLiteral(" CABLE")))
        return 3;

    if (conn.startsWith(QStringLiteral("LAN(T10")) || conn.startsWith(QStringLiteral("LAN T10")))
        return 6;

    if (conn.startsWith(QStringLiteral("LAN(T3")) || conn.startsWith(QStringLiteral("LAN T3")))
        return 5;

    if (conn.startsWith(QStringLiteral("LAN(T1")) || conn.startsWith(QStringLiteral("LAN T1")))
        return 4;

    if (conn == QStringLiteral("LAN") || conn.startsWith(QStringLiteral("LAN ")))
        return 5;

    if (conn == QStringLiteral("SATELLITE") ||
        conn == QStringLiteral("MICROWAVE") ||
        conn == QStringLiteral("WIRELESS") ||
        conn.startsWith(QStringLiteral("WI-FI")) ||
        conn.startsWith(QStringLiteral("WIFI")))
        return 2;

    return -1;
}

double connectionSpeedMbit(const QString &connection)
{
    const QString conn = connection.trimmed();

    if (conn.isEmpty())
        return 0;

    const double value = Util::toDouble(_tq(conn));

    if (value <= 0)
        return 0;

    const QString upper = conn.toUpper();

    if (upper.contains(QStringLiteral("KIB/S")) ||
        upper.contains(QStringLiteral("KB/S")) ||
        upper.contains(QStringLiteral("KBYTE")))
        return value * 8.0 / 1024.0;

    if (upper.contains(QStringLiteral("MIB/S")) ||
        upper.contains(QStringLiteral("MB/S")) ||
        upper.contains(QStringLiteral("MBYTE")))
        return value * 8.0;

    if (upper.contains(QStringLiteral("GIB/S")) ||
        upper.contains(QStringLiteral("GB/S")) ||
        upper.contains(QStringLiteral("GBYTE")))
        return value * 8192.0;

    if (upper.contains(QStringLiteral("KBPS")) || upper.contains(QStringLiteral("KB/S")))
        return value / 1000.0;

    if (upper.contains(QStringLiteral("GBPS")) || upper.contains(QStringLiteral("GBIT")))
        return value * 1000.0;

    return value;
}

int speedIconColumnFromMbit(double speedMbit)
{
    if (speedMbit <= 0)
        return 5;

    if (speedMbit < 0.1)
        return 0;

    if (speedMbit < 0.5)
        return 1;

    if (speedMbit < 1)
        return 2;

    if (speedMbit < 2)
        return 3;

    if (speedMbit < 5)
        return 4;

    if (speedMbit < 1000)
        return 5;

    return 6;
}

int connectionIconColumn(const QString &connection, const QMap<QString, int> &exactSpeeds)
{
    const QString conn = connection.trimmed();

    const auto exact = exactSpeeds.constFind(conn);
    if (exact != exactSpeeds.constEnd())
        return exact.value();

    const int legacy = legacyConnectionIconColumn(conn);
    if (legacy >= 0)
        return legacy;

    return speedIconColumnFromMbit(connectionSpeedMbit(conn));
}

int apexUserImageIndex(const Identity &identity, bool isAway, bool isOp, const QString &connection)
{
    constexpr int APEX_STATUS_AWAY = 0x02;
    constexpr int APEX_STATUS_SERVER = 0x04;
    constexpr int APEX_STATUS_FIREBALL = 0x08;

    int image = 12;
    const int status = static_cast<int>(identity.getStatus());
    const QString conn = connection.trimmed();
    const QString upperConn = conn.toUpper();

    if (isOp || identity.isOp()) {
        image = 0;
    } else if (status & APEX_STATUS_FIREBALL) {
        image = 1;
    } else if (status & APEX_STATUS_SERVER) {
        image = 2;
    } else if (upperConn == QStringLiteral("28.8KBPS") ||
               upperConn == QStringLiteral("33.6KBPS") ||
               upperConn == QStringLiteral("56KBPS") ||
               upperConn == QStringLiteral("MODEM") ||
               upperConn == QStringLiteral("ISDN")) {
        image = 6;
    } else if (upperConn == QStringLiteral("SATELLITE") ||
               upperConn == QStringLiteral("MICROWAVE") ||
               upperConn == QStringLiteral("WIRELESS") ||
               upperConn.startsWith(QStringLiteral("WI-FI")) ||
               upperConn.startsWith(QStringLiteral("WIFI"))) {
        image = 8;
    } else if (upperConn == QStringLiteral("DSL") ||
               upperConn == QStringLiteral("ADSL") ||
               upperConn == QStringLiteral("VDSL") ||
               upperConn == QStringLiteral("CABLE")) {
        image = 9;
    } else if (upperConn.startsWith(QStringLiteral("LAN"))) {
        image = 11;
    } else if (upperConn.startsWith(QStringLiteral("NETLIMITER"))) {
        image = 3;
    } else {
        const double uploadSpeedMbit = conn.isEmpty()
            ? (8 * Util::toDouble(identity.get("US")) / 1024 / 1024)
            : connectionSpeedMbit(conn);

        if (uploadSpeedMbit >= 10) {
            image = 10;
        } else if (uploadSpeedMbit > 0.1) {
            image = 7;
        } else if (uploadSpeedMbit >= 0.01) {
            image = 4;
        } else if (uploadSpeedMbit > 0) {
            image = 5;
        }
    }

    if (isAway || identity.isAway() || (status & APEX_STATUS_AWAY))
        image += 13;

    const bool apexConnectableAdc =
        !identity.get("FS").empty() &&
        identity.supports(AdcHub::ADCS_FEATURE) &&
        identity.supports(AdcHub::SEGA_FEATURE) &&
        ((identity.supports(AdcHub::TCP4_FEATURE) && identity.supports(AdcHub::UDP4_FEATURE)) ||
         identity.supports(AdcHub::NAT0_FEATURE));

    if (apexConnectableAdc)
        image += 26;

    if (showPassiveOverlayForApexIcon(identity, identity.getUser()))
        image += 52;

    return qBound(0, image, 103);
}
}

WulforUtil::WulforUtil(dcpp::DCContext& ctx)
    : QtContextAware(ctx)
{
    qRegisterMetaType< QMap<QString,QVariant> >("VarMap");
    qRegisterMetaType<dcpp::UserPtr>("dcpp::UserPtr");
    qRegisterMetaType< QMap<QString,QString> >("QMap<QString,QString>");
    qRegisterMetaType< dcpp::Identity >("dcpp::Identity");

    memset(userIconCache, 0, sizeof (userIconCache));

    userIcons = new QImage();
    apexUserIcons = false;

    connectionSpeeds["0.005"]   = 0;
    connectionSpeeds["0.01"]    = 0;
    connectionSpeeds["0.02"]    = 0;
    connectionSpeeds["0.05"]    = 0;
    connectionSpeeds["0.1"]     = 1;
    connectionSpeeds["0.2"]     = 1;
    connectionSpeeds["0.5"]     = 2;
    connectionSpeeds["1"]       = 3;
    connectionSpeeds["2"]       = 4;
    connectionSpeeds["5"]       = 5;
    connectionSpeeds["10"]      = 5;
    connectionSpeeds["20"]      = 5;
    connectionSpeeds["50"]      = 5;
    connectionSpeeds["100"]     = 5;
    connectionSpeeds["1000"]    = 6;

    QtEnc2DCEnc["CP949"]        = "CP949 (Korean)";
    QtEnc2DCEnc["GB18030-0"]    = "GB18030 (Chinese)";
    QtEnc2DCEnc["ISO 8859-2"]   = "ISO-8859-2 (Central Europe)";
    QtEnc2DCEnc["ISO 8859-7"]   = "ISO-8859-7 (Greek)";
    QtEnc2DCEnc["ISO 8859-8"]   = "ISO-8859-8 (Hebrew)";
    QtEnc2DCEnc["ISO 8859-9"]   = "ISO-8859-9 (Turkish)";
    QtEnc2DCEnc["ISO 2022-JP"]  = "ISO-2022-JP (Japanese)";
    QtEnc2DCEnc["KOI8-R"]       = "KOI8-R (Cyrillic)";
    QtEnc2DCEnc["UTF-8"]        = "UTF-8 (Unicode)";
    QtEnc2DCEnc["TIS-620"]      = "TIS-620 (Thai)";
    QtEnc2DCEnc["WINDOWS-1250"] = "CP1250 (Central Europe)";
    QtEnc2DCEnc["WINDOWS-1251"] = "CP1251 (Cyrillic)";
    QtEnc2DCEnc["WINDOWS-1252"] = "CP1252 (Western Europe)";
    QtEnc2DCEnc["WINDOWS-1256"] = "CP1256 (Arabic)";
    QtEnc2DCEnc["WINDOWS-1257"] = "CP1257 (Baltic)";

    bin_path = qApp->applicationDirPath() + "/";
    app_icons_path = findAppIconsPath() + "/";

    initFileTypes();
}

WulforUtil::~WulforUtil(){
    delete userIcons;

    clearUserIconCache();
}

bool WulforUtil::loadUserIcons(){
    const QString userIconsPath = findUserIconsPath();
    const bool isApexTheme = QFileInfo(userIconsPath).fileName().compare(QStringLiteral("apex"), Qt::CaseInsensitive) == 0;

    if (loadUserIconsFromFile(userIconsPath + QString("/usericons.png"))) {
        apexUserIcons = isApexTheme;
        return true;
    }

    apexUserIcons = false;
    return false;
}

QString WulforUtil::findAppIconsPath() const
{
    // Try to find application icons directory
    const QString icon_theme = qtCtx()->settings()->getStr(WS_APP_ICONTHEME);

    const QStringList roots = {
        QDir::currentPath() + "/icons/appl",
#if defined(Q_OS_MAC)
        bin_path + "/../Resources/icons/appl",
        "/Applications/EiskaltDC++.app/Contents/Resources/icons/appl",
#endif // defined(Q_OS_MAC)
        QDir::homePath() + "/.eiskaltdc++/icons/appl",
        bin_path + "/appl",
        bin_path + "/icons/appl",
        bin_path + "/../icons/appl",
        CLIENT_ICONS_DIR "/appl",
        bin_path + CLIENT_ICONS_DIR "/appl",
        bin_path + "/../" CLIENT_ICONS_DIR "/appl",
        bin_path + "/../../" CLIENT_ICONS_DIR "/appl"
    };

    auto findThemePath = [&roots](const QString &theme) -> QString {
        if (theme.trimmed().isEmpty())
            return QString();

        for (const QString &root : roots) {
            const QString settings_path = QDir::toNativeSeparators(root + "/" + theme);
            if (QDir(settings_path).exists())
                return settings_path;
        }

        return QString();
    };

    QString themePath = findThemePath(icon_theme);
    if (!themePath.isEmpty())
        return themePath;

    // Settings can reference a missing/removed theme after upgrades.
    themePath = findThemePath(QStringLiteral("default"));
    if (!themePath.isEmpty())
        return themePath;

    // Last resort: use first available icon theme folder.
    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;

        const QStringList themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!themes.isEmpty())
            return QDir::toNativeSeparators(dir.absoluteFilePath(themes.first()));
    }

    return QString();
}

QString WulforUtil::findUserIconsPath() const
{
    // Try to find icons directory
    const QString user_theme = qtCtx()->settings()->getStr(WS_APP_USERTHEME);

    const QStringList roots = {
        QDir::currentPath() + "/icons/user",
#if defined(Q_OS_MAC)
        bin_path + "/../Resources/icons/user",
        "/Applications/EiskaltDC++.app/Contents/Resources/icons/user",
#endif // defined(Q_OS_MAC)
        QDir::homePath() + "/.eiskaltdc++/icons/user",
        bin_path + "icons/user",
        bin_path + "/icons/user",
        bin_path + "/../icons/user",
        bin_path + "/user",
        CLIENT_ICONS_DIR "/user",
        bin_path + CLIENT_ICONS_DIR "/user",
        bin_path + "/../" CLIENT_ICONS_DIR "/user",
        bin_path + "/../../" CLIENT_ICONS_DIR "/user"
    };

    auto findThemePath = [&roots](const QString &theme) -> QString {
        if (theme.trimmed().isEmpty())
            return QString();

        for (const QString &root : roots) {
            const QString settings_path = QDir::toNativeSeparators(root + "/" + theme);
            if (QDir(settings_path).exists())
                return settings_path;
        }

        return QString();
    };

    QString themePath = findThemePath(user_theme);
    if (!themePath.isEmpty())
        return themePath;

    themePath = findThemePath(QStringLiteral("default"));
    if (!themePath.isEmpty())
        return themePath;

    // Last resort: use first available user icon theme folder.
    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;

        const QStringList themes = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!themes.isEmpty())
            return QDir::toNativeSeparators(dir.absoluteFilePath(themes.first()));
    }

    return QString();
}

QString WulforUtil::getAppIconsPath() const
{
    return app_icons_path;
}

QString WulforUtil::getEmoticonsPath() const
{
#if defined (Q_OS_WIN) || defined (Q_OS_HAIKU)
    static const QString emoticonsPath = bin_path + "/" CLIENT_DATA_DIR "/emoticons/";
#elif defined (Q_OS_MAC)
    static const QString emoticonsPath = bin_path + "/../Resources/emoticons/";
#else // Other OS
    static QString emoticonsPath = CLIENT_DATA_DIR "/emoticons/";
    if (!QDir(emoticonsPath).exists()) // Fix for Snap, AppImage, etc.
        emoticonsPath = bin_path + "/../../" + emoticonsPath;
#endif
    return QDir(emoticonsPath).absolutePath() + "/";
}

QString WulforUtil::getClientIconsPath() const
{
#if defined (Q_OS_WIN) || defined (Q_OS_HAIKU)
    static const QString iconsPath = bin_path + "/" CLIENT_ICONS_DIR "/";
#elif defined (Q_OS_MAC)
    static const QString iconsPath = bin_path + "/../Resources/" CLIENT_ICONS_DIR "/";
#else // Other OS
    static QString iconsPath = CLIENT_ICONS_DIR "/";
    if (!QDir(iconsPath).exists()) // Fix for Snap, AppImage, etc.
        iconsPath = QString(bin_path + "/../../" + iconsPath);
#endif
    return QDir(iconsPath).absolutePath();
}

QString WulforUtil::getTranslationsPath() const
{
#if defined (Q_OS_WIN) || defined (Q_OS_HAIKU)
    static const QString translationsPath = bin_path + "/" CLIENT_TRANSLATIONS_DIR "/";
#elif defined (Q_OS_MAC)
    static const QString translationsPath = bin_path + "/../Resources/translations/";
#else // Other OS
    static QString translationsPath = CLIENT_TRANSLATIONS_DIR "/";
    if (!QDir(translationsPath).exists()) // Fix for Snap, AppImage, etc.
        translationsPath = QString(bin_path + "/../../" + translationsPath);
#endif
    return QDir(QDir::toNativeSeparators(translationsPath)).absolutePath();
}

QString WulforUtil::getAspellDataPath() const
{
#if defined (Q_OS_WIN) || defined (Q_OS_HAIKU)
    static const QString aspellDataPath = bin_path + "/" CLIENT_DATA_DIR "/aspell/";
#elif defined (Q_OS_MAC)
    static const QString aspellDataPath = bin_path + "/../Resources/aspell/";
#elif defined(LOCAL_ASPELL_DATA) // Other OS
    static const QString aspellDataPath = CLIENT_DATA_DIR "/aspell/";
    if (!QDir(QDir::toNativeSeparators(aspellDataPath)).exists())
        return QString(bin_path + "/../../" + aspellDataPath);
#else
    static const QString aspellDataPath = QString();
#endif
    return QDir(aspellDataPath).absolutePath();
}

QString WulforUtil::countryFlagEmoji(const QString &countryCode)
{
    const QString code = countryCode.trimmed().toUpper();
    if (code.size() != 2 || !code.at(0).isLetter() || !code.at(1).isLetter())
        return QString();

    const uint first = 0x1F1E6u + static_cast<uint>(code.at(0).unicode() - 'A');
    const uint second = 0x1F1E6u + static_cast<uint>(code.at(1).unicode() - 'A');

    QString out;
    out.append(QChar::highSurrogate(first));
    out.append(QChar::lowSurrogate(first));
    out.append(QChar::highSurrogate(second));
    out.append(QChar::lowSurrogate(second));
    return out;
}

static QString countryCodeFromLabel(QString label)
{
    label = label.trimmed();
    if (label.isEmpty())
        return QString();

    if (label.size() == 2 && label.at(0).isLetter() && label.at(1).isLetter())
        return label.toUpper();

    const QString normalized = label.simplified().toLower();
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("russian federation"), QStringLiteral("RU")},
        {QStringLiteral("czech republic"), QStringLiteral("CZ")},
        {QStringLiteral("united kingdom"), QStringLiteral("GB")},
        {QStringLiteral("united states"), QStringLiteral("US")},
        {QStringLiteral("turkey"), QStringLiteral("TR")},
        {QStringLiteral("turkiye"), QStringLiteral("TR")},
        {QStringLiteral("türkiye"), QStringLiteral("TR")},
        {QStringLiteral("south korea"), QStringLiteral("KR")},
        {QStringLiteral("north korea"), QStringLiteral("KP")},
        {QStringLiteral("moldova, republic of"), QStringLiteral("MD")},
        {QStringLiteral("iran, islamic republic of"), QStringLiteral("IR")},
        {QStringLiteral("syrian arab republic"), QStringLiteral("SY")},
        {QStringLiteral("venezuela, bolivarian republic of"), QStringLiteral("VE")},
        {QStringLiteral("tanzania, united republic of"), QStringLiteral("TZ")},
        {QStringLiteral("bolivia, plurinational state of"), QStringLiteral("BO")},
        {QStringLiteral("viet nam"), QStringLiteral("VN")}
    };

    if (aliases.contains(normalized))
        return aliases.value(normalized);

    const auto locales = QLocale::matchingLocales(QLocale::AnyLanguage, QLocale::AnyScript, QLocale::AnyCountry);

    for (const auto &locale : locales) {
        const QString countryName = QLocale::territoryToString(locale.territory()).simplified().toLower();
        if (countryName != normalized)
            continue;

        const QString localeName = locale.name();
        const QString code = localeName.section(QLatin1Char('_'), 1, 1).toUpper();
        if (code.size() == 2)
            return code;
    }

    return QString();
}

QString WulforUtil::flaggedCountryLabel(const QString &countryText, const QString &countryCode)
{
    QString code = countryCode.trimmed().toUpper();
    QString label = countryText.trimmed();

    if (code.isEmpty() && label.size() == 2 && label.at(0).isLetter() && label.at(1).isLetter())
        code = label.toUpper();
    if (code.size() != 2)
        code = countryCodeFromLabel(label);

    const QString flag = countryFlagEmoji(code);
    if (flag.isEmpty())
        return label;

    if (label.isEmpty())
        label = code;

    return flag + QStringLiteral(" ") + label;
}

QString WulforUtil::flaggedIpLabel(const QString &ip)
{
    if (ip.trimmed().isEmpty())
        return ip;

    const QString countryCode = _q(Util::getIpCountry(_tq(ip)));
    const QString flag = countryFlagEmoji(countryCode);

    if (flag.isEmpty())
        return ip;

    return flag + QStringLiteral(" ") + ip;
}

QString WulforUtil::getClientResourcesPath() const
{
    const QString icon_theme = qtCtx()->settings()->getStr(WS_APP_ICONTHEME);

#if defined(Q_OS_WIN) || defined(Q_OS_HAIKU)
    const QString client_res_path = bin_path + CLIENT_RES_DIR "/" + icon_theme + ".rcc";
#elif defined(Q_OS_MAC)
    const QString client_res_path = bin_path + QString("/../Resources/" CLIENT_RES_DIR "/") + icon_theme + ".rcc";
#else // Other systems
    QString client_res_path = QString(CLIENT_RES_DIR) + PATH_SEPARATOR_STR + icon_theme + ".rcc";
    if (!QFile(client_res_path).exists()) // Build tree: .rcc next to binary
        client_res_path = bin_path + icon_theme + ".rcc";
    if (!QFile(client_res_path).exists()) // Fix for Snap, AppImage, etc.
        client_res_path = bin_path + "/../../" + QString(CLIENT_RES_DIR) + PATH_SEPARATOR_STR + icon_theme + ".rcc";
#endif

    return QDir(client_res_path).absolutePath();;
}

bool WulforUtil::loadUserIconsFromFile(QString file){
    if (userIcons->load(file, "PNG")){
        clearUserIconCache();

        return true;
    }

    return false;
}

void WulforUtil::clearUserIconCache(){
    for (int x = 0; x < USERLIST_XPM_COLUMNS; ++x) {
        for (int y = 0; y < USERLIST_XPM_ROWS; ++y) {
            if (userIconCache[x][y]) {
                delete userIconCache[x][y];
                userIconCache[x][y] = nullptr;
            }
        }
    }
}

QPixmap *WulforUtil::getUserIcon(const UserPtr &id, bool isAway, bool isOp, const QString &sp){

    if (!id)
        return &m_PixmapMap[eiUSERS];

    Identity iid = dcCtx().getClientManager()->getOnlineUserIdentity(id);

    int x = connectionIconColumn(sp, connectionSpeeds);
    int y = 0;

    if (apexUserIcons) {
        const int image = apexUserImageIndex(iid, isAway, isOp, sp);
        x = image % USERLIST_XPM_COLUMNS;
        y = image / USERLIST_XPM_COLUMNS;
    } else {
        if (isAway)
            y += 1;

        if (id->isSet(User::TLS))
            y += 2;

        if( (iid.supports(AdcHub::ADCS_FEATURE) && iid.supports(AdcHub::SEGA_FEATURE)) &&
            ((iid.supports(AdcHub::TCP4_FEATURE) && iid.supports(AdcHub::UDP4_FEATURE)) || iid.supports(AdcHub::NAT0_FEATURE)))
            y += 4;

        if (isOp)
            y += 8;

        if (isPassiveForUserIcon(iid, id)){
            y += 16;

            if (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::INCOMING_CONNECTIONS, true) == SettingsManager::INCOMING_FIREWALL_PASSIVE)
                x = 7;
        }
    }

    if (userIconCache[x][y] == nullptr) {
        userIconCache[x][y] = new QPixmap(
                QPixmap::fromImage(
                                    userIcons->copy(
                                                    x * USERLIST_ICON_SIZE,
                                                    y * USERLIST_ICON_SIZE,
                                                    USERLIST_ICON_SIZE,
                                                    USERLIST_ICON_SIZE
                                                   )
                                    )
                );
    }

    return userIconCache[x][y];
}

static const int PXMTHEMESIDE = 22;
QPixmap WulforUtil::FROMTHEME(const QString &name, bool resource){
    if (resource){
        return QIcon(":/"+name+".png").pixmap(PXMTHEMESIDE, PXMTHEMESIDE);
    }
    else{
        return loadPixmap(name+".png");
    }
}

QPixmap WulforUtil::FROMTHEME_SIDE(const QString &name, bool resource, const int side){
    if (resource)
        return QIcon(":/"+name+".png").pixmap(side, side);
    else{
        return loadPixmap(name+".png").scaled(side, side);
    }
}

bool WulforUtil::loadIcons(){
    m_bError = false;

    app_icons_path = findAppIconsPath() + "/";

    const QString fname = getClientResourcesPath();
    bool resourceFound = false;
    if (QFile(fname).exists() && !qtCtx()->settings()->getBool("app/use-icon-theme", false))
        resourceFound = QResource::registerResource(fname);

    m_PixmapMap.clear();

    m_PixmapMap[eiAWAY]         = FROMTHEME("im-user-away", resourceFound);
    m_PixmapMap[eiBOOKMARK_ADD] = FROMTHEME("bookmark-new", resourceFound);
    m_PixmapMap[eiCLEAR]        = FROMTHEME("edit-clear",   resourceFound);
    m_PixmapMap[eiCONFIGURE]    = FROMTHEME("configure",    resourceFound);
    m_PixmapMap[eiCONNECT]      = FROMTHEME("network-connect", resourceFound);
    m_PixmapMap[eiCONNECT_NO]   = FROMTHEME("network-disconnect", resourceFound);
    m_PixmapMap[eiDOWN]         = FROMTHEME("go-down", resourceFound);
    m_PixmapMap[eiDOWNLIST]     = FROMTHEME("go-down-search", resourceFound);
    m_PixmapMap[eiDOWNLOAD]     = FROMTHEME("download", resourceFound);
    m_PixmapMap[eiDOWNLOAD_AS]  = FROMTHEME("download", resourceFound);
    m_PixmapMap[eiEDIT]         = FROMTHEME("document-edit", resourceFound);
    m_PixmapMap[eiEDITADD]      = FROMTHEME("list-add", resourceFound);
    m_PixmapMap[eiEDITCOPY]     = FROMTHEME("edit-copy", resourceFound);
    m_PixmapMap[eiEDITDELETE]   = FROMTHEME("edit-delete", resourceFound);
    m_PixmapMap[eiEDITCLEAR]    = FROMTHEME_SIDE("edit-clear-locationbar-rtl", resourceFound, 16);
    m_PixmapMap[eiEMOTICON]     = FROMTHEME("face-smile", resourceFound);
    m_PixmapMap[eiEXIT]         = FROMTHEME("application-exit", resourceFound);
    m_PixmapMap[eiFILECLOSE]    = FROMTHEME("dialog-close", resourceFound);
    m_PixmapMap[eiFILEFIND]     = FROMTHEME("edit-find", resourceFound);
    m_PixmapMap[eiFILTER]       = FROMTHEME("view-filter", resourceFound);
    m_PixmapMap[eiFOLDER_BLUE]  = FROMTHEME("folder-blue", resourceFound);
    m_PixmapMap[eiHIDEWINDOW]   = FROMTHEME("view-close", resourceFound);
    m_PixmapMap[eiUP]           = FROMTHEME("go-up", resourceFound);
    m_PixmapMap[eiUPLIST]       = FROMTHEME("go-up-search", resourceFound);
    m_PixmapMap[eiZOOM_IN]      = FROMTHEME("zoom-in", resourceFound);
    m_PixmapMap[eiZOOM_OUT]     = FROMTHEME("zoom-out", resourceFound);
    m_PixmapMap[eiTOP]          = FROMTHEME("go-top", resourceFound);
    m_PixmapMap[eiNEXT]         = FROMTHEME("go-next", resourceFound);
    m_PixmapMap[eiPREVIOUS]     = FROMTHEME("go-previous", resourceFound);

    m_PixmapMap[eiFILETYPE_APPLICATION] = FROMTHEME("application-x-executable", resourceFound);
    m_PixmapMap[eiFILETYPE_ARCHIVE]     = FROMTHEME("application-x-archive", resourceFound);
    m_PixmapMap[eiFILETYPE_DOCUMENT]    = FROMTHEME("text-x-generic", resourceFound);
    m_PixmapMap[eiFILETYPE_MP3]         = FROMTHEME("audio-x-generic", resourceFound);
    m_PixmapMap[eiFILETYPE_PICTURE]     = FROMTHEME("image-x-generic", resourceFound);
    m_PixmapMap[eiFILETYPE_UNKNOWN]     = FROMTHEME("unknown", resourceFound);
    m_PixmapMap[eiFILETYPE_VIDEO]       = FROMTHEME("video-x-generic", resourceFound);

    m_PixmapMap[eiADLS]         = FROMTHEME("adls", resourceFound);
    m_PixmapMap[eiBALL_GREEN]   = FROMTHEME("ball_green", resourceFound);
    m_PixmapMap[eiCHAT]         = FROMTHEME("chat", resourceFound);
    m_PixmapMap[eiCONSOLE]      = FROMTHEME("console", resourceFound);
    m_PixmapMap[eiERASER]       = FROMTHEME("eraser", resourceFound);
    m_PixmapMap[eiFAV]          = FROMTHEME("fav", resourceFound);
    m_PixmapMap[eiFAVADD]       = FROMTHEME("favadd", resourceFound);
    m_PixmapMap[eiFAVREM]       = FROMTHEME("favrem", resourceFound);
    m_PixmapMap[eiFAVSERVER]    = FROMTHEME("favserver", resourceFound);
    m_PixmapMap[eiFAVUSERS]     = FROMTHEME("favusers", resourceFound);
    m_PixmapMap[eiFIND]         = FROMTHEME("find", resourceFound);
    m_PixmapMap[eiFREESPACE]    = FROMTHEME("freespace", resourceFound);
    m_PixmapMap[eiGUI]          = FROMTHEME("gui", resourceFound);
    m_PixmapMap[eiGV]           = QPixmap(gv_xpm);
    m_PixmapMap[eiHASHING]      = FROMTHEME("hashing", resourceFound);
    m_PixmapMap[eiHISTORY]      = FROMTHEME("history", resourceFound);
    m_PixmapMap[eiHUBMSG]       = FROMTHEME("hubmsg", resourceFound);
    m_PixmapMap[eiICON_APPL]    = FROMTHEME_SIDE("icon_appl_big", resourceFound, 128);
    m_PixmapMap[eiMAGNET]       = FROMTHEME("magnet", resourceFound);
    m_PixmapMap[eiMESSAGE]      = FROMTHEME("message", resourceFound);
    m_PixmapMap[eiMESSAGE_TRAY_ICON] = FROMTHEME_SIDE("icon_msg_big", resourceFound, 128);
    m_PixmapMap[eiOWN_FILELIST] = FROMTHEME("own_filelist", resourceFound);
    m_PixmapMap[eiOPENLIST]     = FROMTHEME("openlist", resourceFound);
    m_PixmapMap[eiOPEN_LOG_FILE]= FROMTHEME("log_file", resourceFound);
    m_PixmapMap[eiPLUGIN]       = FROMTHEME("plugin", resourceFound);
    m_PixmapMap[eiPMMSG]        = FROMTHEME("pmmsg", resourceFound);
    m_PixmapMap[eiRECONNECT]    = FROMTHEME("reconnect", resourceFound);
    m_PixmapMap[eiREFRLIST]     = FROMTHEME("refrlist", resourceFound);
    m_PixmapMap[eiRELOAD]       = FROMTHEME("reload", resourceFound);
    m_PixmapMap[eiSERVER]       = FROMTHEME("server", resourceFound);
    m_PixmapMap[eiSETTINGS_CONNECTION] = FROMTHEME("settings-connection", resourceFound);
    m_PixmapMap[eiSETTINGS_DOWNLOADS]  = FROMTHEME("settings-downloads", resourceFound);
    m_PixmapMap[eiSETTINGS_GUI]        = FROMTHEME("settings-gui", resourceFound);
    m_PixmapMap[eiSETTINGS_MAIN]       = FROMTHEME("settings-main", resourceFound);
    m_PixmapMap[eiSETTINGS_SHORTCUTS]  = FROMTHEME("settings-shortcuts", resourceFound);
    m_PixmapMap[eiSPAM]         = FROMTHEME("spam", resourceFound);
    m_PixmapMap[eiSPY]          = FROMTHEME("spy", resourceFound);
    m_PixmapMap[eiSPEED_LIMIT_OFF]  = FROMTHEME("slow_off", resourceFound);
    m_PixmapMap[eiSPEED_LIMIT_ON]   = FROMTHEME("slow", resourceFound);

    m_PixmapMap[eiSPLASH]       = QPixmap();
    m_PixmapMap[eiSTATUS]       = FROMTHEME("status", resourceFound);
    m_PixmapMap[eiTRANSFER]     = FROMTHEME("transfer", resourceFound);
    m_PixmapMap[eiTRANSFER_HIGHLIGHT] = FROMTHEME("transfer-highlight", resourceFound);
    m_PixmapMap[eiUSERS]        = FROMTHEME("users", resourceFound);
    m_PixmapMap[eiQUEUED_USERS] = FROMTHEME("queued-users", resourceFound);
    m_PixmapMap[eiQUEUED_USERS_HIGHLIGHT] = FROMTHEME("queued-users-highlight", resourceFound);
    m_PixmapMap[eiQT_LOGO]      = FROMTHEME("qt-logo", resourceFound);

    return !m_bError;
}

QPixmap WulforUtil::loadPixmap(const QString &file){
    QString f;
    QPixmap p;

    f = app_icons_path + file;
    f = QDir::toNativeSeparators(f);

    if (p.load(f))
        return p;

    printf("loadPixmap: Can't load '%s'\n", f.toUtf8().constData());

    m_bError = true;

    p = QPixmap(gv_xpm);

    return p;
}

const QPixmap &WulforUtil::getPixmap(enum WulforUtil::Icons e){
    return m_PixmapMap[static_cast<qulonglong>(e)];
}

QString WulforUtil::getNicks(const QString &cid, const QString &hintUrl){
    return getNicks(CID(cid.toStdString()), hintUrl);
}

QString WulforUtil::getNickViaOnlineUser(const QString &cid, const QString &hintUrl) {
    OnlineUser* user = dcCtx().getClientManager()->findOnlineUser(CID(_tq(cid)), _tq(hintUrl), true);
    return user ? _q(user->getIdentity().getNick()) : QString();
}

QString WulforUtil::getNicks(const CID &cid, const QString &hintUrl){
    return _q(dcpp::Util::toString(dcCtx().getClientManager()->getNicks(cid, _tq(hintUrl))));
}

void WulforUtil::textToHtml(QString &str, bool print){
    if (print){
        str.replace(";", "&#59;");
        str.replace("<", "&lt;");
        str.replace(">", "&gt;");
    }
    else {
        str.replace("\n", "<br/>");
        str.replace("\t", "&nbsp;&nbsp;&nbsp;&nbsp;");
    }
}

void WulforUtil::initFileTypes(){
    m_FileTypeMap.clear();

    // MP3 2
    m_FileTypeMap["A52"]  = eiFILETYPE_MP3;
    m_FileTypeMap["AAC"]  = eiFILETYPE_MP3;
    m_FileTypeMap["AC3"]  = eiFILETYPE_MP3;
    m_FileTypeMap["APE"]  = eiFILETYPE_MP3;
    m_FileTypeMap["AIFF"] = eiFILETYPE_MP3;
    m_FileTypeMap["AU"]   = eiFILETYPE_MP3;
    m_FileTypeMap["DTS"]  = eiFILETYPE_MP3;
    m_FileTypeMap["FLA"]  = eiFILETYPE_MP3;
    m_FileTypeMap["FLAC"] = eiFILETYPE_MP3;
    m_FileTypeMap["MID"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MOD"]  = eiFILETYPE_MP3;
    m_FileTypeMap["M4A"]  = eiFILETYPE_MP3;
    m_FileTypeMap["M4P"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MPC"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MP1"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MP2"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MP3"]  = eiFILETYPE_MP3;
    m_FileTypeMap["OGG"]  = eiFILETYPE_MP3;
    m_FileTypeMap["RA"]   = eiFILETYPE_MP3;
    m_FileTypeMap["SHN"]  = eiFILETYPE_MP3;
    m_FileTypeMap["SPX"]  = eiFILETYPE_MP3;
    m_FileTypeMap["WAV"]  = eiFILETYPE_MP3;
    m_FileTypeMap["WMA"]  = eiFILETYPE_MP3;
    m_FileTypeMap["WV"]   = eiFILETYPE_MP3;
    m_FileTypeMap["669"]  = eiFILETYPE_MP3;
    m_FileTypeMap["AIF"]  = eiFILETYPE_MP3;
    m_FileTypeMap["AMF"]  = eiFILETYPE_MP3;
    m_FileTypeMap["AMS"]  = eiFILETYPE_MP3;
    m_FileTypeMap["DBM"]  = eiFILETYPE_MP3;
    m_FileTypeMap["DMF"]  = eiFILETYPE_MP3;
    m_FileTypeMap["DSM"]  = eiFILETYPE_MP3;
    m_FileTypeMap["FAR"]  = eiFILETYPE_MP3;
    m_FileTypeMap["IT"]   = eiFILETYPE_MP3;
    m_FileTypeMap["MDL"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MED"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MIDI"] = eiFILETYPE_MP3;
    m_FileTypeMap["MOD"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MOL"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MPA"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MPC"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MPP"]  = eiFILETYPE_MP3;
    m_FileTypeMap["MTM"]  = eiFILETYPE_MP3;
    m_FileTypeMap["NST"]  = eiFILETYPE_MP3;
    m_FileTypeMap["OKT"]  = eiFILETYPE_MP3;
    m_FileTypeMap["PSM"]  = eiFILETYPE_MP3;
    m_FileTypeMap["PTM"]  = eiFILETYPE_MP3;
    m_FileTypeMap["RA"]   = eiFILETYPE_MP3;
    m_FileTypeMap["RMI"]  = eiFILETYPE_MP3;
    m_FileTypeMap["S3M"]  = eiFILETYPE_MP3;
    m_FileTypeMap["STM"]  = eiFILETYPE_MP3;
    m_FileTypeMap["ULT"]  = eiFILETYPE_MP3;
    m_FileTypeMap["UMX"]  = eiFILETYPE_MP3;
    m_FileTypeMap["WOW"]  = eiFILETYPE_MP3;
    m_FileTypeMap["XM"]   = eiFILETYPE_MP3;


    // ARCHIVE 3
    m_FileTypeMap["7Z"]  = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["ACE"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["ARJ"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["BZ2"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["CAB"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["EX_"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["GZ"]  = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["HQX"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["JAR"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["ISO"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["MDF"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["MDS"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["NRG"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["LZH"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["LHA"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["RAR"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["RPM"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["SEA"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["TAR"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["TGZ"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["VCD"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["BWT"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["CCD"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["CDI"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["PDI"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["CUE"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["ISZ"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["IMG"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["VC4"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["UC2"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["ZIP"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["ZOO"] = eiFILETYPE_ARCHIVE;
    m_FileTypeMap["Z"]   = eiFILETYPE_ARCHIVE;

    // DOCUMENT 4
    m_FileTypeMap["CFG"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["CHM"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["CONF"]  = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["CPP"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["CSS"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["C"]     = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["DIZ"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["DOC"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["DOCX"]  = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["H"]     = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["HLP"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["HTM"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["HTML"]  = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["INI"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["INF"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["LOG"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["NFO"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["ODG"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["ODP"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["ODS"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["ODT"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["PDF"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["PHP"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["PPT"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["PS"]    = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["PDF"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["SHTML"] = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["SXC"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["SXD"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["SXI"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["SXW"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["TXT"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["RFT"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["RDF"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["RTF"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["XML"]   = eiFILETYPE_DOCUMENT;
    m_FileTypeMap["XLS"]   = eiFILETYPE_DOCUMENT;

    // APPL 5
    m_FileTypeMap["BAT"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["CGI"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["COM"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["DLL"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["EXE"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["HQX"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["JS"]  = eiFILETYPE_APPLICATION;
    m_FileTypeMap["SH"]  = eiFILETYPE_APPLICATION;
    m_FileTypeMap["SO"]  = eiFILETYPE_APPLICATION;
    m_FileTypeMap["SYS"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["VXD"] = eiFILETYPE_APPLICATION;
    m_FileTypeMap["MSI"] = eiFILETYPE_APPLICATION;

    // PICTURE 6
    m_FileTypeMap["3DS"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["A11"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["ACB"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["ADC"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["ADI"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["AFI"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["AI"]   = eiFILETYPE_PICTURE;
    m_FileTypeMap["AIS"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["ANS"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["ART"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["B8"]   = eiFILETYPE_PICTURE;
    m_FileTypeMap["BMP"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["CBM"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["DCX"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["EPS"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["EMF"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["GIF"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["HEIC"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["HEIF"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["ICO"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["IMG"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["JPEG"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["JPE"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["JPG"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["PCT"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["PCX"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["PIC"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["PICT"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["PNG"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["PS"]   = eiFILETYPE_PICTURE;
    m_FileTypeMap["PSD"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["PSP"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["RLE"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["TGA"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["TIF"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["TIFF"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["WEBP"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["AVIF"] = eiFILETYPE_PICTURE;
    m_FileTypeMap["XPM"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["XIF"]  = eiFILETYPE_PICTURE;
    m_FileTypeMap["WMF"]  = eiFILETYPE_PICTURE;

    // VIDEO 7
    m_FileTypeMap["AVI"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["ASF"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["ASX"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["DAT"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["DIVX"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["DV"]    = eiFILETYPE_VIDEO;
    m_FileTypeMap["FLV"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["M1V"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["M2V"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["M4V"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MKV"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MOV"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MOVIE"] = eiFILETYPE_VIDEO;
    m_FileTypeMap["MP4"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPE"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPEG"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPEG1"] = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPEG2"] = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPEG4"] = eiFILETYPE_VIDEO;
    m_FileTypeMap["MP1V"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["MP2V"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPV1"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPV2"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPG"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPS"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["MPV"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["OGM"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["PXP"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["QT"]    = eiFILETYPE_VIDEO;
    m_FileTypeMap["RAM"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["RM"]    = eiFILETYPE_VIDEO;
    m_FileTypeMap["RV"]    = eiFILETYPE_VIDEO;
    m_FileTypeMap["RMVB"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["VIV"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["VIVO"]  = eiFILETYPE_VIDEO;
    m_FileTypeMap["VOB"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["WMV"]   = eiFILETYPE_VIDEO;
    m_FileTypeMap["TS"]    = eiFILETYPE_VIDEO;
}

const QPixmap &WulforUtil::getPixmapForFile(const QString &file){
    QString ext = QFileInfo(file).suffix().toUpper();

    if (m_FileTypeMap.contains(ext))
        return getPixmap(m_FileTypeMap[ext]);
    else
        return getPixmap(eiFILETYPE_UNKNOWN);
}

QString WulforUtil::qtEnc2DcEnc(QString name){
    if (QtEnc2DCEnc.contains(name))
        return QtEnc2DCEnc[name].left(QtEnc2DCEnc[name].indexOf(" "));

    // "System default" (or any unknown encoding) → use the OS charset
    // detected by dcpp at startup (e.g. "CP1252" on Western-European
    // Windows, "UTF-8" on modern Linux).  Returning an empty string
    // would cause iconv_open("UTF-8", "") to fail silently.
    return QString::fromStdString(dcpp::Text::systemCharset);
}

QString WulforUtil::dcEnc2QtEnc(QString name){
    for (auto it = QtEnc2DCEnc.begin(); it != QtEnc2DCEnc.end(); ++it){
        if (it.value() == name || !it.value().indexOf(name))
            return it.key();
    }

    return tr("System default");
}

QStringList WulforUtil::encodings(){
    return QtEnc2DCEnc.keys();
}

bool WulforUtil::openUrl(const QString &url){
    const QUrl parsedUrl = QUrl::fromUserInput(url);
    if (parsedUrl.isLocalFile()) {
        const QString localPath = parsedUrl.toLocalFile();
        if (isPreviewableImagePath(localPath))
            showPreviewableImage(qtCtx()->mainWindow(), localPath);
        else
            QDesktopServices::openUrl(parsedUrl);
        return true;
    }
    else if (QFileInfo(url).isAbsolute() && QFileInfo(url).exists()) {
        if (isPreviewableImagePath(url))
            showPreviewableImage(qtCtx()->mainWindow(), url);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(url));
        return true;
    }
    else if (url.startsWith("http://") || url.startsWith("www.") || url.startsWith(("ftp://")) || url.startsWith("https://")){
        if (!openWithConfiguredExternalHandler(url))
            QDesktopServices::openUrl(QUrl::fromEncoded(url.toUtf8()));
    }
    else if (url.startsWith("adc://") || url.startsWith("adcs://")){
        qtCtx()->mainWindow()->newHubFrame(url, "UTF-8");
    }
    else if (url.startsWith("dchub://") || url.startsWith("nmdcs://")){
        qtCtx()->mainWindow()->newHubFrame(url, qtCtx()->settings()->getStr(WS_DEFAULT_LOCALE));
    }
    else if (url.startsWith("magnet:") && url.contains("urn:tree:tiger")){
        QString magnet = url;
        Magnet *m = new Magnet(qtCtx()->mainWindow());

        m->setLink(magnet);
        m->exec();

        m->deleteLater();
    }
    else if (url.startsWith("magnet:")){
        const QString magnet = url;

        QUrlQuery u;

        if (!magnet.contains("+")) {
                u.setQuery(magnet.toUtf8());
        } else {
            QString _l = magnet;

            _l.replace("+", "%20");
                u.setQuery(_l.toUtf8());
        }

        StringMap params;
        magnet::parseUri(magnet.toStdString(), params);

        if (!params["kt"].empty()) {
            const QString keywords = _q(params["kt"]);
            QString hub = _q(params["xs"]);

            if (!hub.isEmpty() && !hub.contains("://"))
                hub.prepend("dchub://");

            if (keywords.isEmpty())
                return false;

            if (!hub.isEmpty())
                WulforUtil::openUrl(hub);

            SearchFrame *sfr = ArenaWidgetFactory().create<SearchFrame>();
            sfr->fastSearch(keywords, false);
        }
        else {
            if (!openWithConfiguredExternalHandler(url))
                QDesktopServices::openUrl(QUrl::fromEncoded(url.toUtf8()));
        }
    }
    else
        return false;

    return true;
}

bool WulforUtil::getUserCommandParams(const UserCommand& uc, StringMap& params) {

    StringList names;
    string::size_type i = 0, j = 0;
    const string cmd_str = uc.getCommand();

    while((i = cmd_str.find("%[line:", i)) != string::npos) {
        if ((j = cmd_str.find("]", (i += 7))) == string::npos)
            break;

        names.push_back(cmd_str.substr(i, j - i));
        i = j + 1;
    }

    if (names.empty())
        return true;

    QDialog dlg(qtCtx()->mainWindow());
    dlg.setWindowTitle(_q(uc.getDisplayName().back()));

    QVBoxLayout *vlayout = new QVBoxLayout(&dlg);

    std::vector<std::function<void ()> > valueFs;

    for (const auto &name : names) {
        QString caption = _q(name);

        if (uc.adc()) {
            caption.replace("\\\\", "\\");
            caption.replace("\\s", " ");
        }

        int combo_sel = -1;
        QString combo_caption = caption;
        combo_caption.replace("//", "\t");
        QStringList combo_values = combo_caption.split("/");

        if (combo_values.size() > 2) {
            QString tmp = combo_values.takeFirst();

            bool isNumber = false;
            combo_sel = combo_values.takeFirst().toInt(&isNumber);
            if (!isNumber || combo_sel >= combo_values.size())
                combo_sel = -1;
            else
                caption = tmp;
        }

        QGroupBox *box = new QGroupBox(caption, &dlg);
        QHBoxLayout *hlayout = new QHBoxLayout(box);

        if (combo_sel >= 0) {
            for (auto &val : combo_values)
                val.replace("\t", "/");

            QComboBox *combo = new QComboBox(box);
            hlayout->addWidget(combo);

            combo->addItems(combo_values);
            combo->setEditable(true);
            combo->setCurrentIndex(combo_sel);
            combo->lineEdit()->setReadOnly(true);

            valueFs.push_back([combo, name, &params] {
                params["line:" + name] = combo->currentText().toStdString();
            });

        } else {
            QLineEdit *line = new QLineEdit(box);
            hlayout->addWidget(line);

            valueFs.push_back([line, name, &params] {
                params["line:" + name] = line->text().toStdString();
            });
        }

        vlayout->addWidget(box);
    }

    QDialogButtonBox *buttonBox = new QDialogButtonBox(&dlg);
    buttonBox->setOrientation(Qt::Horizontal);
    buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    vlayout->addWidget(buttonBox);

    dlg.setFixedHeight(vlayout->sizeHint().height());

    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    for (const auto &fs : valueFs)
        fs();

    return true;
}

QStringList WulforUtil::getLocalIfaces(){
    QStringList ifaces;

#ifdef Q_OS_HAIKU
#undef HAVE_IFADDRS_H
#endif // Q_OS_HAIKU

#ifdef HAVE_IFADDRS_H
    struct ifaddrs *ifap;

    if (getifaddrs(&ifap) == 0){
        for (struct ifaddrs *i = ifap; i ; i = i->ifa_next){
            struct sockaddr *sa = i->ifa_addr;

            // If the interface is up, is not a loopback and it has an address
            if ((i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK) && sa && !ifaces.contains(i->ifa_name))
                ifaces.push_back(i->ifa_name);
        }

        freeifaddrs(ifap);
    }
#endif

    return ifaces;
}

QStringList WulforUtil::getLocalIPs(){
    QStringList addresses;

#ifdef HAVE_IFADDRS_H
    struct ifaddrs *ifap;

    if (getifaddrs(&ifap) == 0){
        for (struct ifaddrs *i = ifap; i ; i = i->ifa_next){
            struct sockaddr *sa = i->ifa_addr;

            // If the interface is up, is not a loopback and it has an address
            if ((i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK) && sa){
                void* src = nullptr;
                socklen_t len;

                // IPv4 address
                if (sa->sa_family == AF_INET){
                    struct sockaddr_in* sai = (struct sockaddr_in*)sa;
                    src = (void*) &(sai->sin_addr);
                    len = INET_ADDRSTRLEN;
                }
                // IPv6 address
                else if (sa->sa_family == AF_INET6){
                    struct sockaddr_in6* sai6 = (struct sockaddr_in6*)sa;
                        src = (void*) &(sai6->sin6_addr);
                        len = INET6_ADDRSTRLEN;
                }

                // Convert the binary address to a string and add it to the output list
                if (src){
                    std::vector<char> address(len);
                    inet_ntop(sa->sa_family, src, address.data(), len);
                    addresses.push_back(address.data());
                }
            }
        }

        freeifaddrs(ifap);
    }
#else
    addresses.push_back(Util::getLocalIp().c_str());
#endif

    return addresses;
}

QString WulforUtil::formatBytes(int64_t aBytes){
    return _q(Util::formatBytes(aBytes));
}

QString WulforUtil::makeMagnet(const QString &path, const int64_t size, const QString &tth){
    if (path.isEmpty() || tth.isEmpty())
        return QString();

    return magnetSignature + tth + "&xl=" + _q(Util::toString(size)) + "&dn=" + _q(Util::encodeURI(path.toStdString()));
}

void WulforUtil::splitMagnet(const QString &magnet, int64_t &size, QString &tth, QString &name){
    size = 0;
    tth = "";
    name = "";

    StringMap params;
    if (magnet::parseUri(magnet.toStdString(),params)) {
        // Name of file or directory (or search keywords if name is not set)
        name = _q(params["dn"]);
        if (name.isEmpty() && !params["kt"].empty()) {
            name = _q(params["kt"]);
        }

        // BitTorrent magnet links are quite popular nowadays...
        if (!magnet.contains("urn:btih:") && !magnet.contains("urn:btmh:")) {
            tth = _q(params["xt"]);
        }

        // Size of file or directory
        if (!params["xl"].empty()) {
            size = Util::toInt64(params["xl"]);
        }
        if (!params["dl"].empty()) { // this size is more valuable if it is set
            size = Util::toInt64(params["dl"]);
        }
    }
}

int WulforUtil::sortOrderToInt(Qt::SortOrder order){
    if (order == Qt::AscendingOrder)
        return 0;
    else
        return 1;
}

Qt::SortOrder WulforUtil::intToSortOrder(int i){
    if (!i)
        return Qt::AscendingOrder;
    else
        return Qt::DescendingOrder;
}

QString WulforUtil::getHubNames(const dcpp::CID &cid){
    StringList hubs = dcCtx().getClientManager()->getHubNames(cid, "");

    if (hubs.empty())
        return tr("Offline");
    else
        return _q(Util::toString(hubs));
}

QString WulforUtil::getHubNames(const dcpp::UserPtr &user){
    return getHubNames(user->getCID());
}

QString WulforUtil::getHubNames(const QString &cid){
    return getHubNames(CID(_tq(cid)));
}

QString WulforUtil::compactToolTipText(QString text, int maxlen, QString sep)
{
    int len = text.size();

    if (len <= maxlen)
        return text;

    int n = 0;
    int k = maxlen;

    while((len-k) > 0){
        if(text.at(k) == ' ' || (k == n))
        {
            if(k == n)
                k += maxlen;

            text.insert(k+1,sep);

            len++;
            k += maxlen + 1;
            n += maxlen + 1;
        }
        else k--;
    }

    return text;
}

void WulforUtil::headerMenu(QTreeView *tree){
    if (!tree || !tree->model() || !tree->header())
        return;

    QMenu * mcols = new QMenu(nullptr);
    QAbstractItemModel *model = tree->model();
    QAction * column;

    int count = 0;
    for (int i = 0; i < model->columnCount(); ++i)
        count += tree->header()->isSectionHidden(tree->header()->logicalIndex(i))? 0 : 1;

    bool allowDisable = count > 1;
    int index;

    for (int i = 0; i < model->columnCount(); ++i) {
        index = tree->header()->logicalIndex(i);
        column = mcols->addAction(model->headerData(index, Qt::Horizontal).toString());
        column->setCheckable(true);

        bool checked = !tree->header()->isSectionHidden(index);

        column->setChecked(checked);
        column->setData(index);

        if (checked && !allowDisable)
            column->setEnabled(false);
    }

    QAction * chosen = mcols->exec(QCursor::pos());

    if (chosen) {
        index = chosen->data().toInt();

        if (tree->header()->isSectionHidden(index)) {
            tree->header()->showSection(index);
        } else {
            tree->header()->hideSection(index);
        }
    }

    delete mcols;
}

QMenu *WulforUtil::buildUserCmdMenu(const QList<QString> &hub_list, int ctx, QWidget* parent) {
    dcpp::StringList hubs;
    for (const auto &hub : hub_list)
        hubs.push_back(_tq(hub));

    return buildUserCmdMenu(hubs, ctx, parent);
}

QMenu *WulforUtil::buildUserCmdMenu(const std::string& hub_url, int ctx, QWidget* parent) {
    return buildUserCmdMenu(StringList(1, hub_url), ctx, parent);
}

QMenu *WulforUtil::buildUserCmdMenu(const StringList& hub_list, int ctx, QWidget* parent) {
    UserCommand::List userCommands = dcCtx().getFavoriteManager()->getUserCommands(ctx, hub_list);

    if (userCommands.empty())
        return nullptr;

    QMenu *ucMenu = new QMenu(tr("User commands"), parent);

    QMenu *menuPtr = ucMenu;
    for (size_t n = 0; n < userCommands.size(); ++n) {
        UserCommand *uc = &userCommands[n];
        if (uc->getType() == UserCommand::TYPE_SEPARATOR) {
            // Avoid double separators...
            if (!menuPtr->actions().isEmpty() &&
                !menuPtr->actions().last()->isSeparator())
            {
                menuPtr->addSeparator();
            }
        } else if (uc->isRaw() || uc->isChat()) {
            menuPtr = ucMenu;
            auto _begin = uc->getDisplayName().begin();
            auto _end = uc->getDisplayName().end();
            for(; _begin != _end; ++_begin) {
                const QString name = _q(*_begin);
                if (_begin + 1 == _end) {
                    menuPtr->addAction(name)->setData(uc->getId());
                } else {
                    bool found = false;
                    QListIterator<QAction*> iter(menuPtr->actions());
                    while(iter.hasNext()) {
                        QAction *item = iter.next();
                        if (item->menu() && item->text() == name) {
                            found = true;
                            menuPtr = item->menu();
                            break;
                        }
                    }

                    if (!found) {
                        menuPtr = menuPtr->addMenu(name);
                    }
                }
            }
        }
    }
    return ucMenu;
}

bool WulforUtil::isTTH ( const QString& text ) {
    static const QRegularExpression tthPattern("\\A[A-Z0-9]+\\z");
    return ((text.length() == 39) && tthPattern.match(text).hasMatch());
}
