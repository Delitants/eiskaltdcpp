#include "updates/UpdateRuntimeIdentity.h"
#include <QCoreApplication>
#include <QSysInfo>
#include <cstdio>

int main(int argc, char** argv) {
    // Qt coerces malformed bundle-version fields during its own construction.
    // Bypass that fixture-only read so the independent CF validator is exercised.
    QCoreApplication::setApplicationVersion("runtime-identity-fixture");
    QCoreApplication application(argc, argv);
    const auto config = updates::currentMacUpdateRuntime();
    if(config) {
        std::printf("ELIGIBLE=1 BUILD=%llu CPU=%s CHANNEL=%s\n",
            static_cast<unsigned long long>(config->identity.build), qPrintable(config->identity.cpu),
            config->identity.defaultChannel == updates::Channel::Preview ? "preview" : "stable");
    } else std::printf("ELIGIBLE=0 CPU=%s\n", qPrintable(QSysInfo::buildCpuArchitecture()));
    return 0;
}
