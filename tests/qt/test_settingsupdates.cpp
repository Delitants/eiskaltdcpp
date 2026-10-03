#include "dcpp/stdinc.h"
#include "tests/TestContext.h"
#include "WulforSettings.h"
#include "updates/UpdateBackend.h"
#include "updates/UpdateController.h"
#include "updates/SettingsUpdates.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QRegularExpression>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTest>
#include <QTextBrowser>
#include <QTranslator>

namespace {
using namespace updates;
void events() { for(int i = 0; i < 8; ++i) QCoreApplication::processEvents(); }
struct Backend : UpdateBackend {
    bool supported = true;
    quint64 operation = 0;
    int installs = 0;
    CheckKind kind = CheckKind::Automatic;
    bool supportsInstallation() const override { return supported; }
    void check(quint64 op, Channel, CheckKind k) override { operation = op; kind = k; }
    void download(quint64 op, const UpdateOffer&) override { operation = op; }
    void cancel() override {}
    void armInstallOnQuit(const UpdateOffer&, bool) override { ++installs; }
    void revokeInstallConsent() override {}
};
struct Fixture {
    dcpp::test::TestContext context;
    WulforSettings settings{*context.ownedCtx};
    Backend backend;
    QDateTime now = QDateTime::fromString("2026-02-02T02:40:00Z", Qt::ISODate);
    UpdateIdentity identity{1, Channel::Preview, "org.test.eiskalt", "arm64", QVersionNumber(26, 0)};
    std::unique_ptr<UpdateController> controller;
    std::unique_ptr<QWidget> page;
    Fixture() { restart(); }
    void restart() {
        page.reset(); controller.reset();
        controller = std::make_unique<UpdateController>(settings, backend, identity, [&] { return now; });
        page = std::make_unique<SettingsUpdates>(*controller);
        page->resize(640, 700); page->show(); events();
    }
    template<class T> T* control(const char* name) {
        auto* result = page->findChild<T*>(QString::fromLatin1(name));
        REQUIRE(result != nullptr);
        return result;
    }
    void apply() {
        static_cast<SettingsUpdates*>(page.get())->apply();
        events();
    }
    UpdateOffer offer() const {
        return {2, "3.0.0-pre.2", "<img src=\"https://invalid.test/private\"><script>bad()</script><a href=\"file:///private\">link</a>",
            identity.bundleId, identity.cpu, QVersionNumber(26, 0), Channel::Preview, 100, QByteArray(32, 'a')};
    }
    void available() {
        controller->checkNow(); emit backend.available(backend.operation, offer()); events();
    }
    void ready() {
        available(); controller->download();
        emit backend.downloadReady(backend.operation, 2, QByteArray(32, 'a')); events();
    }
};
struct TranslationScope {
    QTranslator translator;
    explicit TranslationScope(const char* language) {
        REQUIRE(translator.load(QString::fromUtf8(language), QStringLiteral(UPDATER_TEST_QM_DIR)));
        REQUIRE(QCoreApplication::installTranslator(&translator));
    }
    ~TranslationScope() { QCoreApplication::removeTranslator(&translator); }
};
}

TEST_CASE("Updater compiled catalogs localize real controls, consent and errors", "[settings-updates][updater-catalogs]") {
    struct Captions {
        const char *language, *checks, *checkAccessible, *checkNow, *version, *consent, *noFallback, *unsupported;
    };
    const Captions english{"en", "&Check for updates automatically", "Check for updates automatically",
        "Check &Now", "Available version: 3.0.0-pre.2",
        "This version is selected for installation after a successful normal quit. No restart is forced.",
        "The update connection failed. No direct fallback was used.",
        "Self-updating is not supported on this platform or build."};
    const Captions ukrainian{"uk", "&Автоматично перевіряти оновлення", "Автоматично перевіряти оновлення",
        "Перевірити &зараз", "Доступна версія: 3.0.0-pre.2",
        "Цю версію вибрано для встановлення після успішного штатного виходу. Примусового перезапуску не буде.",
        "Не вдалося встановити з'єднання для оновлення. Пряме з'єднання не використовувалося.",
        "Самооновлення не підтримується на цій платформі або в цій збірці."};
    const Captions russian{"ru", "&Автоматически проверять обновления", "Автоматически проверять обновления",
        "Проверить &сейчас", "Доступная версия: 3.0.0-pre.2",
        "Эта версия выбрана для установки после успешного штатного выхода. Принудительного перезапуска не будет.",
        "Не удалось установить соединение для обновления. Прямое соединение не использовалось.",
        "Самообновление не поддерживается на этой платформе или в этой сборке."};
    const Captions* captions = &english;
    SECTION("English") { captions = &english; }
    SECTION("Ukrainian") { captions = &ukrainian; }
    SECTION("Russian") { captions = &russian; }
    TranslationScope translation(captions->language);
    for(const auto& entry : {std::pair{"updates::SettingsUpdates", "&Check for updates automatically"},
                            std::pair{"updates::UpdateController", "The update connection failed. No direct fallback was used."},
                            std::pair{"updates::MacUpdateBackend", "Unable to create private update staging."},
                            std::pair{"updates::UpdateTransport", "An update download is already running."}}) {
        CHECK_FALSE(translation.translator.translate(entry.first, entry.second).isEmpty());
    }
    Fixture f;
    CHECK(f.control<QCheckBox>("updateChecks")->text() == QString::fromUtf8(captions->checks));
    CHECK(f.control<QCheckBox>("updateChecks")->accessibleName() == QString::fromUtf8(captions->checkAccessible));
    CHECK(f.control<QPushButton>("updateCheckNow")->text() == QString::fromUtf8(captions->checkNow));
    f.ready();
    CHECK(f.control<QLabel>("updateVersion")->text() == QString::fromUtf8(captions->version));
    f.control<QPushButton>("updateInstallOnQuit")->click();
    CHECK(f.control<QLabel>("updateResult")->text() == QString::fromUtf8(captions->consent));
    CHECK(f.backend.installs == 0);
    f.controller->checkNow();
    emit f.backend.failed(f.backend.operation, "Private diagnostic must not appear", FailureCategory::Network);
    events();
    CHECK(f.control<QLabel>("updateResult")->text() == QString::fromUtf8(captions->noFallback));
    CHECK(QCoreApplication::translate("updates::UnsupportedUpdateBackend",
        "Self-updating is not supported on this platform or build.") == QString::fromUtf8(captions->unsupported));
}

TEST_CASE("Updater controls and diagnostics resolve in every shipped language", "[settings-updates][updater-catalogs]") {
    const struct { const char *language, *checkNow; } examples[] = {
        {"be", "Праверыць зараз"}, {"bg", "Провери сега"}, {"cs", "Zkontrolovat nyní"},
        {"da", "Kontrollér nu"}, {"de", "Jetzt prüfen"}, {"el", "Έλεγχος τώρα"},
        {"en", "Check Now"}, {"es", "Comprobar ahora"}, {"eu", "Egiaztatu orain"},
        {"fr", "Vérifier maintenant"}, {"hu", "Ellenőrzés most"}, {"ie", "Controlar nu"},
        {"it", "Controlla ora"}, {"ka", "ახლავე შემოწმება"}, {"pl", "Sprawdź teraz"},
        {"pt_BR", "Verificar agora"}, {"ru", "Проверить сейчас"}, {"sk", "Skontrolovať teraz"},
        {"sr", "Провери сада"}, {"sr@latin", "Proveri sada"}, {"sv_SE", "Kontrollera nu"},
        {"tr", "Şimdi denetle"}, {"uk", "Перевірити зараз"}, {"vi", "Kiểm tra ngay"},
        {"zh_CN", "立即检查"}
    };
    for(const auto& example : examples) {
        DYNAMIC_SECTION(example.language) {
            TranslationScope translation(example.language);
            for(const auto& entry : {std::pair{"updates::SettingsUpdates", "Available version: %1"},
                                    std::pair{"updates::UpdateController", "The update connection failed. No direct fallback was used."},
                                    std::pair{"updates::MacUpdateBackend", "Unable to create private update staging."},
                                    std::pair{"updates::UpdateTransport", "An update download is already running."},
                                    std::pair{"updates::UnsupportedUpdateBackend", "Self-updating is not supported on this platform or build."}})
                CHECK_FALSE(translation.translator.translate(entry.first, entry.second).isEmpty());
            Fixture f;
            auto caption = f.control<QPushButton>("updateCheckNow")->text();
            caption.remove(QRegularExpression("\\s*\\(&[A-Za-z]\\)$"));
            caption.remove('&');
            CHECK(caption == QString::fromUtf8(example.checkNow));
            QSet<QString> mnemonics;
            const auto add = [&](const QString& text) {
                const auto match = QRegularExpression("(?<!&)&([^&])").match(text);
                REQUIRE(match.hasMatch());
                CHECK_FALSE(mnemonics.contains(match.captured(1).toLower()));
                mnemonics.insert(match.captured(1).toLower());
            };
            for(auto* control : f.page->findChildren<QCheckBox*>()) add(control->text());
            for(auto* control : f.page->findChildren<QPushButton*>()) add(control->text());
            for(auto* label : f.page->findChildren<QLabel*>()) if(label->buddy()) add(label->text());
            CHECK(mnemonics.size() == 10);
            f.ready();
            CHECK(f.control<QLabel>("updateVersion")->text().contains("3.0.0-pre.2"));
            CHECK_FALSE(f.control<QLabel>("updateVersion")->text().contains("%1"));
            f.control<QPushButton>("updateInstallOnQuit")->click();
            CHECK(f.backend.installs == 0);
        }
    }
}

TEST_CASE("Updater settings preserve explicit OFF choices and accessible native defaults", "[settings-updates]") {
    Fixture f;
    auto* checks = f.control<QCheckBox>("updateChecks");
    auto* notices = f.control<QCheckBox>("updateNotifications");
    auto* install = f.control<QCheckBox>("updateAutoInstall");
    CHECK(checks->isChecked()); CHECK(notices->isChecked()); CHECK_FALSE(install->isChecked());
    for(auto* control : {checks, notices, install}) {
        CHECK_FALSE(control->accessibleName().isEmpty());
        CHECK(control->text().contains('&'));
        CHECK(control->focusPolicy() != Qt::NoFocus);
    }
    checks->setFocus(); QTest::keyClick(checks, Qt::Key_Space);
    CHECK_FALSE(checks->isChecked());
    notices->setChecked(false); install->setChecked(true);
    CHECK_FALSE(install->isEnabled());
    CHECK(f.control<QPushButton>("updateCheckNow")->isEnabled());
    CHECK(f.controller->preferences().checks); // Editing is not an implicit Apply.
    f.apply(); f.restart();
    CHECK_FALSE(f.control<QCheckBox>("updateChecks")->isChecked());
    CHECK_FALSE(f.control<QCheckBox>("updateNotifications")->isChecked());
    CHECK_FALSE(f.control<QCheckBox>("updateAutoInstall")->isEnabled());
    f.control<QPushButton>("updateCheckNow")->click(); events();
    CHECK(f.controller->state() == UpdateController::State::Checking);
    CHECK(f.backend.kind == CheckKind::Manual);
}

TEST_CASE("Updater settings use the saved channel and show successful manual results with notifications OFF", "[settings-updates]") {
    Fixture f;
    f.control<QCheckBox>("updateNotifications")->setChecked(false); f.apply();
    auto* channel = f.control<QComboBox>("updateChannel");
    CHECK_FALSE(channel->accessibleName().isEmpty());
    channel->setCurrentIndex(0); f.apply(); f.restart();
    CHECK(f.controller->preferences().channel == Channel::Stable);
    const auto previousSuccess = f.control<QLabel>("updateLastSuccess")->text();
    f.control<QPushButton>("updateCheckNow")->click();
    emit f.backend.noUpdate(f.backend.operation); events();
    CHECK_FALSE(f.control<QLabel>("updateResult")->text().isEmpty());
    CHECK(f.controller->lastSuccess() == f.now);
    CHECK(f.control<QLabel>("updateLastSuccess")->text() != previousSuccess);
    const auto successfulCheck = f.control<QLabel>("updateLastSuccess")->text();
    CHECK(f.control<QPushButton>("updateCheckNow")->isEnabled());
    f.restart();
    CHECK(f.control<QLabel>("updateLastSuccess")->text() == successfulCheck);
}

TEST_CASE("Updater settings reserve distinct mnemonic labels on platforms that use them", "[settings-updates]") {
    const char* language = "en";
    SECTION("English") { language = "en"; }
    SECTION("Ukrainian") { language = "uk"; }
    SECTION("Russian") { language = "ru"; }
    TranslationScope translation(language);
    Fixture f;
    QStringList shortcuts;
    const auto add = [&](const QString& text) {
        // macOS disables automatic mnemonic sequences. Check the portable
        // labels without changing process-global Qt behavior for other tests.
        const auto match = QRegularExpression("(?<!&)&([^&])").match(text);
        CHECK(match.hasMatch());
        shortcuts.append(match.captured(1).toLower());
    };
    for(auto* control : f.page->findChildren<QCheckBox*>()) add(control->text());
    for(auto* control : f.page->findChildren<QPushButton*>()) add(control->text());
    for(auto* label : f.page->findChildren<QLabel*>()) if(label->buddy()) add(label->text());
    REQUIRE(shortcuts.size() == 10);
    CHECK(QSet<QString>(shortcuts.begin(), shortcuts.end()).size() == 10);
}

TEST_CASE("Updater settings reflect offer download progress and consent without installation", "[settings-updates]") {
    Fixture f; f.available();
    CHECK(f.control<QLabel>("updateVersion")->text().contains("3.0.0-pre.2"));
    auto* notes = f.control<QTextBrowser>("updateNotes");
    CHECK(notes->toPlainText() == f.offer().notes);
    CHECK_FALSE(notes->openExternalLinks()); CHECK_FALSE(notes->openLinks());
    CHECK(f.control<QPushButton>("updateDownload")->isEnabled());
    CHECK_FALSE(f.control<QPushButton>("updateInstallOnQuit")->isEnabled());
    f.control<QPushButton>("updateDownload")->click(); events();
    REQUIRE(f.controller->state() == UpdateController::State::Downloading);
    CHECK(f.control<QPushButton>("updateCancel")->isEnabled());
    emit f.backend.progress(f.backend.operation, 37, 100); events();
    CHECK(f.control<QProgressBar>("updateProgress")->value() == 37);
    emit f.backend.downloadReady(f.backend.operation, 2, QByteArray(32, 'a')); events();
    CHECK(f.control<QPushButton>("updateInstallOnQuit")->isEnabled());
    CHECK_FALSE(f.control<QPushButton>("updateDownload")->isEnabled());
    f.control<QPushButton>("updateInstallOnQuit")->click(); events();
    CHECK(f.backend.installs == 0);
    CHECK(f.controller->state() == UpdateController::State::Ready);
    CHECK_FALSE(f.control<QLabel>("updateResult")->text().isEmpty());
    f.control<QPushButton>("updateLater")->click(); events();
    CHECK(f.controller->state() == UpdateController::State::Ready);
    CHECK(f.backend.installs == 0);
    f.control<QPushButton>("updateSkip")->click(); events();
    CHECK(f.controller->state() == UpdateController::State::Idle);
    CHECK_FALSE(f.control<QPushButton>("updateInstallOnQuit")->isEnabled());
}

TEST_CASE("Updater settings cancel download and discard stale progress", "[settings-updates]") {
    Fixture f; f.available(); f.control<QPushButton>("updateDownload")->click(); events();
    const auto oldOperation = f.backend.operation;
    f.control<QPushButton>("updateCancel")->click(); events();
    emit f.backend.progress(oldOperation, 80, 100); events();
    CHECK(f.controller->state() == UpdateController::State::Idle);
    CHECK(f.control<QProgressBar>("updateProgress")->isHidden());
    CHECK_FALSE(f.control<QPushButton>("updateCancel")->isEnabled());
}

TEST_CASE("Updater settings show indeterminate progress when total size is unknown", "[settings-updates]") {
    Fixture f; f.available(); f.control<QPushButton>("updateDownload")->click(); events();
    emit f.backend.progress(f.backend.operation, 0, 0); events();
    CHECK(f.control<QProgressBar>("updateProgress")->minimum() == 0);
    CHECK(f.control<QProgressBar>("updateProgress")->maximum() == 0);
    emit f.backend.progress(f.backend.operation, 25, 100); events();
    CHECK(f.control<QProgressBar>("updateProgress")->maximum() == 100);
    CHECK(f.control<QProgressBar>("updateProgress")->value() == 25);
}

TEST_CASE("Updater settings fail closed when backend installation is unavailable", "[settings-updates]") {
    Fixture f; f.backend.supported = false; f.restart(); f.available();
    CHECK_FALSE(f.control<QPushButton>("updateDownload")->isEnabled());
    CHECK_FALSE(f.control<QCheckBox>("updateAutoInstall")->isEnabled());
    CHECK_FALSE(f.control<QLabel>("updateInstallSupport")->text().isEmpty());
    f.ready();
    CHECK_FALSE(f.control<QPushButton>("updateInstallOnQuit")->isEnabled());
    CHECK(f.backend.installs == 0);
}

TEST_CASE("Updater settings display redacted manual failures but no unsolicited notices when disabled", "[settings-updates]") {
    Fixture f; f.control<QCheckBox>("updateNotifications")->setChecked(false); f.apply();
    const auto before = f.control<QLabel>("updateResult")->text();
    f.controller->start(); f.controller->poll();
    emit f.backend.failed(f.backend.operation, "password=secret/private", FailureCategory::Network); events();
    CHECK(f.control<QLabel>("updateResult")->text() == before);
    f.control<QPushButton>("updateCheckNow")->click();
    emit f.backend.failed(f.backend.operation, "password=secret/private", FailureCategory::Network); events();
    const auto result = f.control<QLabel>("updateResult")->text();
    CHECK_FALSE(result.isEmpty()); CHECK_FALSE(result.contains("secret")); CHECK_FALSE(result.contains("/private"));
}

TEST_CASE("Updater settings detach safely when the controller is destroyed", "[settings-updates]") {
    Fixture f; f.available(); f.controller.reset(); events();
    CHECK_FALSE(f.page->isEnabled());
    f.control<QPushButton>("updateCheckNow")->click(); f.apply(); events();
}

TEST_CASE("Updater long checkboxes wrap without losing native input behavior", "[settings-updates]") {
    TranslationScope translation("uk");
    Fixture f;
    auto* check = f.control<QCheckBox>("updateAutoInstall");
    REQUIRE(check->hasHeightForWidth());
    const auto wrappedHeight = check->heightForWidth(200);
    REQUIRE(wrappedHeight > check->sizeHint().height());
    check->setFixedSize(200, wrappedHeight); events();
    REQUIRE(check->isEnabled());
    CHECK_FALSE(check->isChecked());
    QTest::mouseClick(check, Qt::LeftButton, Qt::NoModifier, QPoint(80, wrappedHeight - 3));
    CHECK(check->isChecked());
    check->setFocus();
    QTest::keyClick(check, Qt::Key_Space);
    CHECK_FALSE(check->isChecked());
    CHECK_FALSE(check->accessibleName().isEmpty());
    CHECK(f.backend.installs == 0);
}

TEST_CASE("Updater settings keep native controls inside narrow light and dark layouts", "[settings-updates]") {
    const char* language = GENERATE("be", "bg", "cs", "da", "de", "el", "en", "es", "eu", "fr", "hu", "ie",
        "it", "ka", "pl", "pt_BR", "ru", "sk", "sr", "sr@latin", "sv_SE", "tr", "uk", "vi", "zh_CN");
    TranslationScope translation(language);
    Fixture f;
    f.control<QPushButton>("updateCheckNow");
    f.available();
    for(bool dark : {false, true}) {
        QPalette palette = f.page->palette();
        palette.setColor(QPalette::Window, dark ? QColor("#242424") : QColor("#fafafa"));
        palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Base, dark ? QColor("#181818") : Qt::white);
        palette.setColor(QPalette::Text, dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Button, dark ? QColor("#343434") : QColor("#efefef"));
        palette.setColor(QPalette::ButtonText, dark ? Qt::white : Qt::black);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, dark ? QColor("#888888") : QColor("#777777"));
        f.page->setPalette(palette); f.page->resize(420, 800); events();
        INFO("language=" << language << "; auto-install minimum="
             << f.control<QCheckBox>("updateAutoInstall")->minimumSizeHint().width()
             << "; install-button minimum="
             << f.control<QPushButton>("updateInstallOnQuit")->minimumSizeHint().width()
             << "; checks minimum=" << f.control<QCheckBox>("updateChecks")->minimumSizeHint().width()
             << "; notifications minimum=" << f.control<QCheckBox>("updateNotifications")->minimumSizeHint().width());
        CHECK(f.page->width() <= 420);
        for(auto* check : f.page->findChildren<QCheckBox*>()) {
            CHECK(check->width() >= check->minimumSizeHint().width());
            CHECK(check->height() >= check->heightForWidth(check->width()));
            CHECK(f.page->rect().contains(QRect(check->mapTo(f.page.get(), QPoint()), check->size())));
        }
        for(auto* button : f.page->findChildren<QPushButton*>()) {
            const auto bounds = QRect(button->mapTo(f.page.get(), QPoint()), button->size());
            CHECK(f.page->rect().contains(bounds));
            CHECK(button->width() >= button->minimumSizeHint().width());
            CHECK_FALSE(button->accessibleName().isEmpty());
        }
        if(!qEnvironmentVariableIsEmpty("EISKALT_SETTINGS_SCREENSHOTS")) {
            const auto directory = qEnvironmentVariable("EISKALT_SETTINGS_SCREENSHOTS");
            const auto theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
            REQUIRE(f.page->grab().save(directory + QStringLiteral("/%1-%2-narrow.png").arg(QString::fromUtf8(language), theme)));
            f.page->resize(800, 800); events();
            REQUIRE(f.page->grab().save(directory + QStringLiteral("/%1-%2-wide.png").arg(QString::fromUtf8(language), theme)));
        }
    }
}
