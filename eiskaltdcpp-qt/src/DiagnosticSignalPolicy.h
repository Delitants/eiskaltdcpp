#pragma once

#include <QtGlobal>

namespace diagnostic_log {

constexpr bool shouldInstallFatalSignalHandlers()
{
#if defined(Q_OS_MACOS)
    return false;
#else
    return true;
#endif
}

}
