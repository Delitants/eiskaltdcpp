// Linked against the actual native-enabled application's objects, then run only
// inside a private ad-hoc-signed bundle with all external networking denied.
#include "dcpp/stdinc.h"
#include "dcpp/Util.h"
#include "tests/TestContext.h"
#include "WulforSettings.h"
#include "updates/ApplicationUpdateOwner.h"
#include "updates/MacUpdateBackend.h"
#include "updates/UpdateController.h"
#include "updates/UpdateRuntimeIdentity.h"
#include "updates/SettingsUpdates.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#ifndef EISKALT_MAC_UPDATER_NATIVE
#error Native selection probe requires the explicit native application build
#endif
namespace updates {
struct ApplicationUpdateOwnerTestAccess {
    static UpdateBackend* backend(ApplicationUpdateOwner& owner) { return owner.backend.get(); }
};
}
namespace {
template<class T> T* control(QWidget& page, const char* name) {
    auto* result = page.findChild<T*>(QString::fromLatin1(name));
    if(!result) qFatal("Missing production Updates control: %s", name);
    return result;
}
QByteArray settingsBytes(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) qFatal("Cannot read private settings baseline");
    return file.readAll();
}
}
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    using namespace updates;
    const auto arguments = application.arguments();
    if(arguments.size() != 2 || (arguments[1] != "--expect-installation=enabled" &&
        arguments[1] != "--expect-installation=disabled"))
        qFatal("Explicit --expect-installation=enabled|disabled required (assertion only)");
    const bool expectedInstallation = arguments[1].endsWith("=enabled");
    if(!qEnvironmentVariable("HOME").startsWith("/private/tmp/")) qFatal("Private fixture HOME required");
    const auto runtime = currentMacUpdateRuntime();
    if(!runtime) qFatal("Current fixture bundle was not structurally admitted");
    dcpp::test::TestContext context;
    WulforSettings settings(*context.ownedCtx);
    settings.setStr("selectionProbe/sentinel", "unapplied UI edits must not change this file");
    if(!settings.saveChecked()) qFatal("Cannot save private settings baseline");
    const auto settingsPath = QString::fromStdString(dcpp::Util::getPath(dcpp::Util::PATH_USER_CONFIG))
        + QStringLiteral("EiskaltDC++_Qt.conf");
    const auto baseline = settingsBytes(settingsPath);
    {
        MacUpdateBackend direct(*context.ownedCtx);
        if(direct.supportsInstallation()) qFatal("Direct constructor bypassed production factory admission");
        if(!direct.stopWorkersForQuit()) qFatal("Direct-constructor worker fence refused");
    }
    auto owner = std::make_unique<ApplicationUpdateOwner>(settings, *context.ownedCtx);
    auto* backend = ApplicationUpdateOwnerTestAccess::backend(*owner);
    auto* controller = owner->controller();
    if(!qobject_cast<MacUpdateBackend*>(backend) || backend->parent() || controller->parent())
        qFatal("Application owner did not consume an unparented native selection");
    if(controller->preferences().channel != runtime->identity.defaultChannel)
        qFatal("Runtime identity mismatch");
    if(controller->supportsInstallation() != expectedInstallation)
        qFatal("Production factory installation capability: expected %d, actual %d",
            expectedInstallation, controller->supportsInstallation());
    const auto preferences = controller->preferences();
    {
        QDialog dialog;
        auto* layout = new QVBoxLayout(&dialog);
        auto* page = new SettingsUpdates(*controller, &dialog);
        layout->addWidget(page);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.show();
        application.processEvents();
        auto* support = control<QLabel>(*page, "updateInstallSupport");
        const auto expectedMessage = expectedInstallation
            ? SettingsUpdates::tr("Updates are staged first. Installation requires a successful normal quit and verified shutdown; crashes never authorize installation.")
            : SettingsUpdates::tr("Update installation is unavailable in this build. You can still check for updates manually.");
        if(!support->isVisible() || support->text() != expectedMessage)
            qFatal("Production Updates capability messaging mismatch");
        if(!control<QPushButton>(*page, "updateCheckNow")->isEnabled())
            qFatal("Idle manual check must remain enabled");
        for(const auto* name : {"updateCancel", "updateDownload", "updateInstallOnQuit", "updateLater", "updateSkip"})
            if(control<QPushButton>(*page, name)->isEnabled()) qFatal("Unsafe Idle action enabled: %s", name);
        auto* checks = control<QCheckBox>(*page, "updateChecks");
        auto* automatic = control<QCheckBox>(*page, "updateAutoInstall");
        if(checks->isChecked() != preferences.checks || automatic->isChecked() != preferences.autoInstall ||
            automatic->isEnabled() != (expectedInstallation && preferences.checks))
            qFatal("Initial Updates preference controls mismatch");
        checks->setChecked(true);
        if(automatic->isEnabled() != expectedInstallation) qFatal("Checks ON capability gating mismatch");
        if(expectedInstallation) automatic->click();
        const bool draftAutomatic = automatic->isChecked();
        checks->click();
        if(checks->isChecked() || automatic->isEnabled() || automatic->isChecked() != draftAutomatic)
            qFatal("Checks OFF must disable auto-install without losing its draft choice");
        checks->click();
        if(automatic->isEnabled() != expectedInstallation || automatic->isChecked() != draftAutomatic)
            qFatal("Checks ON must restore capability gating and draft choice");
        control<QCheckBox>(*page, "updateNotifications")->click();
        auto* channel = control<QComboBox>(*page, "updateChannel");
        channel->setCurrentIndex((channel->currentIndex() + 1) % channel->count());
        buttons->button(QDialogButtonBox::Cancel)->click();
        if(dialog.result() != QDialog::Rejected || dialog.isVisible()) qFatal("Cancel did not reject Updates dialog");
        // Deliberately never call SettingsUpdates::apply(): these are canceled drafts.
    }
    const auto unchanged = controller->preferences();
    if(unchanged.checks != preferences.checks || unchanged.notifications != preferences.notifications ||
        unchanged.autoInstall != preferences.autoInstall || unchanged.channel != preferences.channel)
        qFatal("Canceled Updates edits mutated controller preferences");
    if(!settings.saveChecked() || settingsBytes(settingsPath) != baseline)
        qFatal("Canceled Updates edits changed settings bytes");
    {
        SettingsUpdates reopened(*controller);
        if(control<QCheckBox>(reopened, "updateChecks")->isChecked() != preferences.checks ||
            control<QCheckBox>(reopened, "updateNotifications")->isChecked() != preferences.notifications ||
            control<QCheckBox>(reopened, "updateAutoInstall")->isChecked() != preferences.autoInstall ||
            control<QComboBox>(reopened, "updateChannel")->currentData().toInt() != static_cast<int>(preferences.channel))
            qFatal("Reopened Updates page retained canceled drafts");
    }
    // Longer than the controller's startup delay: compilation/selection must
    // never activate scheduling just because valid metadata/frameworks exist.
    QElapsedTimer wait; wait.start();
    while(wait.elapsed() < 3600) { application.processEvents(); QThread::msleep(10); }
    if(controller->lastAttempt().isValid() || controller->lastSuccess().isValid() ||
        controller->state() != UpdateController::State::Idle)
        qFatal("Construction activated updater work");
    if(!controller->stopWorkersForQuit()) qFatal("Native worker fence refused");
    if(controller->supportsInstallation() != expectedInstallation)
        qFatal("Quit fence changed immutable installation admission");
    controller->normalQuitCommitted();
    if(!ApplicationUpdateOwner::finishNormalQuit(owner) || owner) qFatal("No-update ownership was not released");
    puts("PASS production application owner: expected capability, real Updates UI, canceled drafts byte-identical, no scheduler, checked no-update disposal");
}
