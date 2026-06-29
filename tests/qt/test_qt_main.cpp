/*
 * Custom main for Qt-based tests.
 * Creates a QApplication so that Qt models, signals, etc. work correctly.
 * Falls back to the "offscreen" platform plugin when no display is available
 * (e.g. headless CI runners).
 */

#ifdef _WIN32
#include "w.h"
#endif

#include <QApplication>
#include <catch2/catch_session.hpp>

#include <cstdlib>

int main(int argc, char* argv[]) {
    // Use offscreen rendering when no display server is available
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") &&
        !qEnvironmentVariableIsSet("DISPLAY") &&
        !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QApplication app(argc, argv);

#ifdef _WIN32
    WSADATA wsaData = {};
    const bool winsockStarted = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    if (!winsockStarted) {
        return 1;
    }
#endif

    const int result = Catch::Session().run(argc, argv);

#ifdef _WIN32
    WSACleanup();
#endif

    return result;
}
