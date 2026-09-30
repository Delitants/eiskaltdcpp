#include "updates/UnsupportedUpdateBackend.h"
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
using namespace updates;
TEST_CASE("Unsupported updater reports capability without offering or installing", "[updates][update-backend]") {
    UnsupportedUpdateBackend backend;
    int failures = 0, offers = 0, ready = 0;
    quint64 operation = 0;
    QObject::connect(&backend, &UpdateBackend::failed, [&](quint64 op, QString message, FailureCategory category) {
        ++failures; operation = op; CHECK_FALSE(message.isEmpty()); CHECK(category == FailureCategory::Unsupported);
    });
    QObject::connect(&backend, &UpdateBackend::available, [&] { ++offers; });
    QObject::connect(&backend, &UpdateBackend::downloadReady, [&] { ++ready; });
    backend.check(7, Channel::Stable, CheckKind::Manual);
    CHECK(failures == 0);
    QCoreApplication::processEvents();
    CHECK(failures == 1); CHECK(operation == 7);
    backend.download(8, {}); backend.cancel(); QCoreApplication::processEvents();
    CHECK(failures == 1);
    backend.download(9, {}); QCoreApplication::processEvents();
    CHECK(failures == 2); CHECK(operation == 9);
    CHECK(offers == 0); CHECK(ready == 0);
}
