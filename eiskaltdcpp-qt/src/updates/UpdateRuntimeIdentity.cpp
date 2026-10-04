#include "UpdateRuntimeIdentity.h"
#include "UpdateUrlPolicy.h"
#include <QRegularExpression>
#include <algorithm>
#include <limits>

namespace updates {
namespace {
QString text(const QVariantMap& info, const char* key) {
    const auto value = info.value(QLatin1String(key));
    return value.metaType().id() == QMetaType::QString ? value.toString() : QString();
}
bool flag(const QVariantMap& info, const char* key, bool expected) {
    const auto value = info.value(QLatin1String(key));
    return value.metaType().id() == QMetaType::Bool && value.toBool() == expected;
}
bool zeroInteger(const QVariant& value) {
    switch(value.metaType().id()) {
    case QMetaType::Int: case QMetaType::LongLong: return value.toLongLong() == 0;
    case QMetaType::UInt: case QMetaType::ULongLong: return value.toULongLong() == 0;
    default: return false;
    }
}
bool versionShape(const QVersionNumber& version) {
    const auto segments = version.segments();
    return !segments.isEmpty() && segments.size() <= 3 && segments.front() > 0 &&
        std::all_of(segments.begin(), segments.end(), [](int n) { return n >= 0; });
}
}
std::optional<UpdateRuntimeConfig> validateMacUpdateRuntime(const QVariantMap& info,
    const QString& cpu, const QVersionNumber& os) {
    const QVersionNumber floor = cpu == "arm64" ? QVersionNumber(26, 0) :
        cpu == "x86_64" ? QVersionNumber(14, 0) : QVersionNumber();
    if(floor.isNull() || !versionShape(os) || os < floor ||
        text(info, "CFBundleIdentifier") != "com.github.eiskaltdcpp" ||
        text(info, "CFBundleExecutable") != "EiskaltDC++" || text(info, "CFBundlePackageType") != "APPL") return {};

    const auto buildText = text(info, "CFBundleVersion");
    if(buildText.size() > 19) return {};
    bool numeric = false;
    const auto build = buildText.toULongLong(&numeric);
    if(!numeric || !build || build > quint64(std::numeric_limits<qint64>::max()) ||
        QString::number(build) != buildText) return {};

    static const QRegularExpression osSyntax(R"(\A(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*)){0,2}\z)");
    const auto minimum = text(info, "LSMinimumSystemVersion");
    if(minimum.size() > 32 || !osSyntax.match(minimum).hasMatch() ||
        QVersionNumber::compare(QVersionNumber::fromString(minimum), floor) != 0) return {};

    static const QRegularExpression semver(R"(\A(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?\z)");
    static const QRegularExpression digits(R"(\A[0-9]+\z)");
    const auto display = text(info, "CFBundleShortVersionString");
    if(display.size() > 128) return {};
    const auto version = semver.match(display);
    if(!version.hasMatch()) return {};
    const auto prerelease = version.captured(1);
    for(const auto& part : prerelease.split('.')) {
        if(part.size() > 1 && part.startsWith('0') && digits.match(part).hasMatch()) return {};
    }

    const auto key = text(info, "SUPublicEDKey").toLatin1();
    if(key.size() != 44) return {};
    const auto decoded = QByteArray::fromBase64(key, QByteArray::AbortOnBase64DecodingErrors);
    if(decoded.size() != 32 || decoded.toBase64() != key ||
        decoded == QByteArray(32, '\0')) return {};
    const auto feed = text(info, "SUFeedURL");
    const auto base = feedUrlForCpu(cpu).toString(QUrl::FullyEncoded);
    static const QRegularExpression revision(R"(\A\?revision=[1-9][0-9]*\z)");
    if(feed.size() > 2048 || !feed.startsWith(base) ||
        (feed != base && !revision.match(feed.mid(base.size())).hasMatch())) return {};

    if(!flag(info, "SURequireSignedFeed", true) || !flag(info, "SUVerifyUpdateBeforeExtraction", true) ||
        !flag(info, "SUEnableDownloaderService", false) || !flag(info, "SUEnableAutomaticChecks", false) ||
        !flag(info, "SUAutomaticallyUpdate", false) || !flag(info, "SUEnableSystemProfiling", false) ||
        !flag(info, "SUEnableInstallerLauncherService", true) ||
        !zeroInteger(info.value("SUSignedFeedFailureExpirationInterval"))) return {};
    const auto ats = info.value("NSAppTransportSecurity");
    if(ats.metaType().id() != QMetaType::QVariantMap || ats.toMap().size() != 1 ||
        !flag(ats.toMap(), "NSAllowsLocalNetworking", true)) return {};

    return UpdateRuntimeConfig{{build, prerelease.isEmpty() ? Channel::Stable : Channel::Preview,
        text(info, "CFBundleIdentifier"), cpu, os}, QUrl(feed, QUrl::StrictMode), key};
}
}
