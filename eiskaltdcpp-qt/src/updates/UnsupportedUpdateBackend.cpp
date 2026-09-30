#include "UnsupportedUpdateBackend.h"
#include <QTimer>
namespace updates {
void UnsupportedUpdateBackend::reject(quint64 op) {
    operation = op;
    const auto generation = ++epoch;
    QTimer::singleShot(0, this, [this, op, generation] {
        if(epoch == generation) emit failed(op, tr("Self-updating is not supported on this platform or build."), FailureCategory::Unsupported);
    });
}
void UnsupportedUpdateBackend::check(quint64 op, Channel, CheckKind) { reject(op); }
void UnsupportedUpdateBackend::download(quint64 op, const UpdateOffer&) { reject(op); }
void UnsupportedUpdateBackend::cancel() { ++epoch; }
void UnsupportedUpdateBackend::armInstallOnQuit(const UpdateOffer&, bool) { reject(operation); }
void UnsupportedUpdateBackend::revokeInstallConsent() {}
}
