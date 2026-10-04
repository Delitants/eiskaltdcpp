#include "UpdateRuntimeIdentity.h"
#ifdef Q_OS_MAC
#include <QCoreApplication>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QSysInfo>
#include <CoreFoundation/CoreFoundation.h>

namespace {
QString string(CFStringRef value) {
    if(!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
    const auto length = CFStringGetLength(value);
    if(length > 2048) return {};
    // Preserve embedded NULs for exact metadata validation, never C-string trim.
    return QString::fromCFString(value);
}
QVariant scalar(CFTypeRef value) {
    if(!value) return {};
    const auto type = CFGetTypeID(value);
    if(type == CFStringGetTypeID()) return string(static_cast<CFStringRef>(value));
    if(type == CFBooleanGetTypeID()) return bool(CFBooleanGetValue(static_cast<CFBooleanRef>(value)));
    if(type == CFNumberGetTypeID()) {
        const auto number = static_cast<CFNumberRef>(value);
        if(CFNumberIsFloatType(number)) return {};
        qlonglong n = 0;
        if(CFNumberGetValue(number, kCFNumberSInt64Type, &n)) return n;
    }
    return {};
}
QString path(CFURLRef url) {
    if(!url) return {};
    const auto value = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    const auto result = string(value);
    if(value) CFRelease(value);
    return result;
}
}
#endif

namespace updates {
std::optional<UpdateRuntimeConfig> currentMacUpdateRuntime() {
#ifdef Q_OS_MAC
    if(!QCoreApplication::instance()) return {};
    const auto bundle = CFBundleGetMainBundle();
    if(!bundle) return {};
    const auto bundleUrl = CFBundleCopyBundleURL(bundle);
    const auto executableUrl = CFBundleCopyExecutableURL(bundle);
    const QFileInfo directory(path(bundleUrl)), executable(path(executableUrl));
    if(bundleUrl) CFRelease(bundleUrl);
    if(executableUrl) CFRelease(executableUrl);
    const auto actual = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const auto root = directory.canonicalFilePath();
    if(actual.isEmpty() || root.isEmpty() || !directory.isDir() || directory.suffix() != "app" ||
        !executable.isExecutable() || executable.canonicalFilePath() != actual ||
        actual != root + "/Contents/MacOS/EiskaltDC++") return {};

    const auto dictionary = CFBundleGetInfoDictionary(bundle);
    if(!dictionary) return {};
    QVariantMap info;
    // Preserve CF booleans versus numbers/strings; QVariant coercion is unsafe
    // for the native trust switches. Only bounded scalar schema fields are read.
    const struct { const char* name; CFStringRef key; } keys[] = {
        {"CFBundleIdentifier", CFSTR("CFBundleIdentifier")}, {"CFBundleExecutable", CFSTR("CFBundleExecutable")},
        {"CFBundlePackageType", CFSTR("CFBundlePackageType")}, {"CFBundleVersion", CFSTR("CFBundleVersion")},
        {"CFBundleShortVersionString", CFSTR("CFBundleShortVersionString")}, {"LSMinimumSystemVersion", CFSTR("LSMinimumSystemVersion")},
        {"SUPublicEDKey", CFSTR("SUPublicEDKey")}, {"SUFeedURL", CFSTR("SUFeedURL")},
        {"SURequireSignedFeed", CFSTR("SURequireSignedFeed")}, {"SUVerifyUpdateBeforeExtraction", CFSTR("SUVerifyUpdateBeforeExtraction")},
        {"SUEnableDownloaderService", CFSTR("SUEnableDownloaderService")}, {"SUEnableAutomaticChecks", CFSTR("SUEnableAutomaticChecks")},
        {"SUAutomaticallyUpdate", CFSTR("SUAutomaticallyUpdate")}, {"SUEnableSystemProfiling", CFSTR("SUEnableSystemProfiling")},
        {"SUEnableInstallerLauncherService", CFSTR("SUEnableInstallerLauncherService")},
        {"SUSignedFeedFailureExpirationInterval", CFSTR("SUSignedFeedFailureExpirationInterval")}
    };
    for(const auto& key : keys) info[QLatin1String(key.name)] = scalar(CFDictionaryGetValue(dictionary, key.key));
    const auto ats = CFDictionaryGetValue(dictionary, CFSTR("NSAppTransportSecurity"));
    if(ats && CFGetTypeID(ats) == CFDictionaryGetTypeID() && CFDictionaryGetCount(static_cast<CFDictionaryRef>(ats)) == 1)
        info["NSAppTransportSecurity"] = QVariantMap{{"NSAllowsLocalNetworking",
            scalar(CFDictionaryGetValue(static_cast<CFDictionaryRef>(ats), CFSTR("NSAllowsLocalNetworking")))}};
    const auto os = QOperatingSystemVersion::current();
    return validateMacUpdateRuntime(info, QSysInfo::buildCpuArchitecture(),
        QVersionNumber(os.majorVersion(), os.minorVersion(), qMax(0, os.microVersion())));
#else
    return {};
#endif
}
}
