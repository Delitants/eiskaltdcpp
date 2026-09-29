#ifdef USE_TORRENT
#include <catch2/catch_test_macros.hpp>
#include "TorrentCreateDialog.h"
#include "TorrentWindow.h"
#include "torrent/TorrentCreator.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentTrackers.h"
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <atomic>
#include <future>
#include <functional>
#include <memory>

using eiskalt::torrent::TorrentCreator;

namespace {
class CompactFormStyle : public QProxyStyle {
public:
    CompactFormStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *data = nullptr) const override
    {
        // Reproduce the native macOS form policy even on offscreen CI.
        if (hint == SH_FormLayoutFieldGrowthPolicy) return QFormLayout::FieldsStayAtSizeHint;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

template<class T> T *control(QObject &dialog, const char *name)
{
    auto *result = dialog.findChild<T *>(QString::fromLatin1(name));
    REQUIRE(result);
    return result;
}

TEST_CASE("Creator fields fill available width and trackers absorb resizing", "[qt][torrent-create-dialog]")
{
    CompactFormStyle style;
    TorrentCreateDialog dialog(nullptr);
    SECTION("application style") {}
    SECTION("restrictive macOS form policy") {
        dialog.setStyle(&style);
        for (auto *widget : dialog.findChildren<QWidget *>()) widget->setStyle(&style);
    }
    auto *source = control<QLineEdit>(dialog, "torrentCreateSource");
    auto *output = control<QLineEdit>(dialog, "torrentCreateOutput");
    auto *trackers = control<QPlainTextEdit>(dialog, "torrentCreateTrackers");
    dialog.resize(740, 650);
    dialog.show();
    QTest::qWait(20);
    const auto contentWidth = dialog.layout()->contentsRect().width();
    REQUIRE(source->parentWidget()->width() >= contentWidth - 4);
    REQUIRE(output->parentWidget()->width() >= contentWidth - 4);
    REQUIRE(trackers->width() >= contentWidth - 4);
    REQUIRE(source->width() > 320);
    REQUIRE(trackers->height() >= 160);
    const auto sourceWidth = source->width();
    const auto outputWidth = output->width();
    const auto trackerSize = trackers->size();
    const auto originalSize = dialog.size();
    dialog.resize(originalSize + QSize(240, 180));
    QTest::qWait(20);
    REQUIRE(source->width() >= sourceWidth + 230);
    REQUIRE(output->width() >= outputWidth + 230);
    REQUIRE(trackers->width() >= trackerSize.width() + 230);
    REQUIRE(trackers->height() >= trackerSize.height() + 150);
    dialog.resize(dialog.minimumSizeHint());
    QTest::qWait(20);
    auto *privacy = control<QCheckBox>(dialog, "torrentCreatePrivate");
    INFO("dialog height=" << dialog.height() << " minimum hint=" << dialog.minimumSizeHint().height()
         << " layout minimum=" << dialog.layout()->minimumSize().height()
         << " inputs height=" << trackers->parentWidget()->height()
         << " inputs minimum=" << trackers->parentWidget()->minimumSizeHint().height());
    REQUIRE(trackers->geometry().bottom() < privacy->geometry().top());
    REQUIRE(trackers->parentWidget()->rect().contains(trackers->geometry()));
    for (const auto *name : {"torrentCreateBrowseFile", "torrentCreateBrowseFolder",
                             "torrentCreateBrowseOutput", "torrentCreateStart", "torrentCreateCancel"}) {
        auto *button = control<QPushButton>(dialog, name);
        REQUIRE(button->width() >= button->minimumSizeHint().width());
        REQUIRE(dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint()), button->size())));
    }
}

TEST_CASE("Creator populates editable shipped defaults", "[qt][torrent-create-dialog]")
{
    TorrentCreateDialog dialog(nullptr);
    auto *trackers = control<QPlainTextEdit>(dialog, "torrentCreateTrackers");
    const auto defaults = eiskalt::torrent::defaultTrackers();
    REQUIRE_FALSE(defaults.isEmpty());
    REQUIRE(trackers->toPlainText().split('\n', Qt::SkipEmptyParts) == defaults);
    REQUIRE_FALSE(trackers->isReadOnly());
}

TEST_CASE("Private toggles preserve custom trackers and restore only removed defaults", "[qt][torrent-create-dialog]")
{
    TorrentCreateDialog dialog(nullptr);
    auto *trackers = control<QPlainTextEdit>(dialog, "torrentCreateTrackers");
    auto *privacy = control<QCheckBox>(dialog, "torrentCreatePrivate");
    const auto defaults = eiskalt::torrent::defaultTrackers();
    REQUIRE_FALSE(defaults.isEmpty());
    trackers->setPlainText(defaults.join('\n'));
    const QString custom = QStringLiteral("https://private.example/announce?passkey=local-test");
    SECTION("toggle removes shipped URLs but retains custom ones and restores removed defaults") {
        trackers->appendPlainText(custom);
        privacy->setChecked(true);
        REQUIRE(trackers->toPlainText() == custom);
        const QString second = QStringLiteral("https://second.example/announce");
        trackers->appendPlainText(second);
        privacy->setChecked(false);
        auto expected = defaults;
        expected << custom << second;
        REQUIRE(trackers->toPlainText().split('\n', Qt::SkipEmptyParts) == expected);
        privacy->setChecked(true);
        REQUIRE(trackers->toPlainText() == custom + '\n' + second);
    }
    SECTION("manually removed defaults never reappear") {
        trackers->setPlainText(custom);
        privacy->setChecked(true);
        privacy->setChecked(false);
        REQUIRE(trackers->toPlainText() == custom);
    }
    SECTION("only defaults still present are restored without duplicates") {
        trackers->setPlainText("  " + defaults.first() + "  \n" + custom);
        privacy->setChecked(true);
        REQUIRE(trackers->toPlainText() == custom);
        trackers->appendPlainText(defaults.first());
        privacy->setChecked(false);
        REQUIRE(trackers->toPlainText().split('\n').count(defaults.first()) == 1);
        for (const auto &url : defaults.mid(1)) REQUIRE_FALSE(trackers->toPlainText().contains(url));
    }
}

TEST_CASE("Private toggle restores tracker priority while retaining private edits", "[qt][torrent-create-dialog]")
{
    TorrentCreateDialog dialog(nullptr);
    auto *trackers = control<QPlainTextEdit>(dialog, "torrentCreateTrackers");
    auto *privacy = control<QCheckBox>(dialog, "torrentCreatePrivate");
    const auto defaults = eiskalt::torrent::defaultTrackers();
    REQUIRE(defaults.size() >= 2);
    const QString first = QStringLiteral("https://first.example/announce");
    const QString second = QStringLiteral("https://second.example/announce");
    const QString edited = QStringLiteral("https://edited.example/announce?passkey=new");
    const QString added = QStringLiteral("https://added.example/announce");
    QStringList original, expected;
    SECTION("custom-first priority survives an unchanged round trip") {
        original = QStringList{first} + defaults;
        expected = original;
    }
    SECTION("interleaved priorities survive an unchanged round trip") {
        original = {first, defaults[0], second, defaults[1]};
        expected = original;
    }
    SECTION("private edits replace and append custom trackers without resetting priorities") {
        original = {first, defaults[0], second, defaults[1]};
        expected = {edited, defaults[0], second, defaults[1], added};
    }
    SECTION("private deletions clamp restoration positions without resurrecting custom URLs") {
        original = {first, defaults[0], second, defaults[1]};
        expected = {edited, defaults[0], defaults[1]};
    }
    SECTION("private reordering retains the user's current custom order") {
        original = {first, defaults[0], second, defaults[1]};
        expected = {second, defaults[0], first, defaults[1]};
    }
    trackers->setPlainText(original.join('\n'));
    privacy->setChecked(true);
    if (expected.contains(added)) trackers->setPlainText(QStringList{edited, second, added}.join('\n'));
    else if (expected.contains(edited)) trackers->setPlainText(edited);
    else if (expected.first() == second) trackers->setPlainText(QStringList{second, first}.join('\n'));
    const auto privateText = trackers->toPlainText();
    privacy->setChecked(false);
    REQUIRE(trackers->toPlainText().split('\n', Qt::SkipEmptyParts) == expected);
    // Defaults absent before the toggle must remain absent, even after edits.
    for (const auto &url : defaults) {
        if (!original.contains(url)) REQUIRE_FALSE(trackers->toPlainText().contains(url));
    }
    privacy->setChecked(true);
    REQUIRE(trackers->toPlainText() == privateText);
    privacy->setChecked(false);
    REQUIRE(trackers->toPlainText().split('\n', Qt::SkipEmptyParts) == expected);
}

bool waitFor(const std::function<bool()> &ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 10000) QTest::qWait(5);
    return ready();
}

void paths(TorrentCreateDialog &dialog, const QString &root)
{
    QFile source(root + "/payload.bin");
    REQUIRE(source.open(QIODevice::WriteOnly));
    REQUIRE(source.write(QByteArray(70001, 'x')) == 70001);
    source.close();
    control<QLineEdit>(dialog, "torrentCreateSource")->setText(source.fileName());
    control<QLineEdit>(dialog, "torrentCreateOutput")->setText(root + "/created.torrent");
}
}

TEST_CASE("Create Torrent action opens an internal modal dialog without an engine", "[qt][torrent-create-dialog]")
{
    QTemporaryDir dir;
    TorrentWindow window(nullptr, dir.filePath("layout.ini"));
    auto *action = control<QAction>(window, "torrentCreate");
#ifndef Q_OS_UNIX
    REQUIRE_FALSE(action->isEnabled());
    REQUIRE_FALSE(action->toolTip().isEmpty());
#else
    REQUIRE(action->isEnabled());
    action->trigger();
    auto *dialog = window.findChild<TorrentCreateDialog *>();
    REQUIRE(dialog);
    REQUIRE(dialog->isModal());
    REQUIRE(dialog->isVisible());
    REQUIRE(dialog->parentWidget() == &window);
    dialog->reject();
#endif
}

#ifndef Q_OS_UNIX
TEST_CASE("Creator dialog explains unsupported source access", "[qt][torrent-create-dialog]")
{
    TorrentCreateDialog dialog(nullptr);
    REQUIRE_FALSE(control<QPushButton>(dialog, "torrentCreateStart")->isEnabled());
    REQUIRE_FALSE(control<QPushButton>(dialog, "torrentCreateBrowseFile")->isEnabled());
    REQUIRE_FALSE(control<QLabel>(dialog, "torrentCreateStatus")->text().isEmpty());
}
#else
TEST_CASE("Creator writes selected public or private announce metadata", "[qt][torrent-create-dialog]")
{
    QTemporaryDir dir;
    const auto root = QDir(dir.path()).canonicalPath();
    TorrentCreateDialog dialog(nullptr);
    paths(dialog, root);
    auto *trackers = control<QPlainTextEdit>(dialog, "torrentCreateTrackers");
    auto *privacy = control<QCheckBox>(dialog, "torrentCreatePrivate");
    const auto defaults = eiskalt::torrent::defaultTrackers();
    const QString custom = QStringLiteral("https://private.example/announce?passkey=local-test");
    QStringList expected;
    SECTION("public metadata carries the editable defaults") { expected = defaults; }
    SECTION("private metadata retains custom tracker only") {
        trackers->appendPlainText(custom);
        privacy->setChecked(true);
        expected << custom;
    }
    SECTION("private metadata filters defaults pasted after toggling") {
        privacy->setChecked(true);
        trackers->setPlainText(defaults.join('\n') + '\n' + custom);
        expected << custom;
    }
    SECTION("private metadata never silently restores public defaults") {
        privacy->setChecked(true);
    }
    SECTION("public metadata honors an intentionally empty editor") { trackers->clear(); }
    QSignalSpy created(&dialog, &TorrentCreateDialog::created);
    control<QPushButton>(dialog, "torrentCreateStart")->click();
    REQUIRE(waitFor([&] { return created.count() == 1; }));
    QFile metadata(root + "/created.torrent");
    REQUIRE(metadata.open(QIODevice::ReadOnly));
    const auto bytes = metadata.readAll();
    if (expected.isEmpty()) {
        REQUIRE_FALSE(bytes.contains("8:announce"));
        REQUIRE_FALSE(bytes.contains("13:announce-list"));
    } else {
        const auto first = expected.first().toUtf8();
        REQUIRE(bytes.contains("8:announce" + QByteArray::number(first.size()) + ':' + first));
        for (const auto &url : expected) {
            const auto utf8 = url.toUtf8();
            REQUIRE(bytes.contains(QByteArray::number(utf8.size()) + ':' + utf8));
        }
        if (expected.size() > 1) REQUIRE(bytes.contains("13:announce-list"));
    }
    if (privacy->isChecked()) {
        REQUIRE(bytes.contains("7:privatei1e"));
        for (const auto &url : defaults) REQUIRE_FALSE(bytes.contains(url.toUtf8()));
    }
}

TEST_CASE("Creation controls validate paths and default to no seeding", "[qt][torrent-create-dialog]")
{
    TorrentCreateDialog dialog(nullptr);
    auto *create = control<QPushButton>(dialog, "torrentCreateStart");
    REQUIRE_FALSE(create->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(dialog, "torrentCreateSeed")->isChecked());
    REQUIRE_FALSE(control<QCheckBox>(dialog, "torrentCreateSeed")->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(dialog, "torrentCreatePrivate")->isChecked());
    REQUIRE(control<QPlainTextEdit>(dialog, "torrentCreateTrackers"));
    REQUIRE(control<QPushButton>(dialog, "torrentCreateBrowseFile")->isEnabled());
    REQUIRE(control<QPushButton>(dialog, "torrentCreateBrowseFolder")->isEnabled());
    QTemporaryDir dir;
    paths(dialog, QDir(dir.path()).canonicalPath());
    REQUIRE(create->isEnabled());
    control<QLineEdit>(dialog, "torrentCreateOutput")->setText("relative.torrent");
    REQUIRE_FALSE(create->isEnabled());
}

TEST_CASE("Dialog disables editing during work and reports created metadata without deleting sources", "[qt][torrent-create-dialog]")
{
    QTemporaryDir dir;
    const auto root = QDir(dir.path()).canonicalPath();
    TorrentCreateDialog dialog(nullptr);
    paths(dialog, root);
    QSignalSpy created(&dialog, &TorrentCreateDialog::created);
    auto *create = control<QPushButton>(dialog, "torrentCreateStart");
    create->click();
    REQUIRE_FALSE(create->isEnabled());
    REQUIRE_FALSE(control<QLineEdit>(dialog, "torrentCreateSource")->isEnabled());
    REQUIRE_FALSE(control<QCheckBox>(dialog, "torrentCreatePrivate")->isEnabled());
    REQUIRE(control<QPushButton>(dialog, "torrentCreateCancel")->isEnabled());
    REQUIRE(waitFor([&] { return created.count() == 1; }));
    REQUIRE(created.at(0).at(0).toString() == root + "/created.torrent");
    REQUIRE(created.at(0).at(1).toString() == root);
    REQUIRE(control<QProgressBar>(dialog, "torrentCreateProgress")->value() == 1000);
    REQUIRE(control<QLineEdit>(dialog, "torrentCreateSource")->isEnabled());
    QFile source(root + "/payload.bin");
    REQUIRE(source.open(QIODevice::ReadOnly));
    REQUIRE(source.readAll() == QByteArray(70001, 'x'));
    REQUIRE(QFile::exists(root + "/created.torrent"));
}

TEST_CASE("Dialog cancellation and errors restore editable state without partial output", "[qt][torrent-create-dialog]")
{
    QTemporaryDir dir;
    const auto root = QDir(dir.path()).canonicalPath();
    TorrentCreateDialog dialog(nullptr);
    paths(dialog, root);
    QSignalSpy created(&dialog, &TorrentCreateDialog::created);
    auto *create = control<QPushButton>(dialog, "torrentCreateStart");
    SECTION("cancel using close path") {
        auto entered = std::make_shared<std::atomic<bool>>(false);
        std::promise<void> release;
        const auto resume = release.get_future().share();
        auto *creator = dialog.findChild<TorrentCreator *>();
        REQUIRE(creator);
        // Hold the hashing worker before publication so this exercises reject(),
        // not a race between a tiny file finishing and the GUI receiving input.
        QObject::connect(creator, &TorrentCreator::progress, creator,
            [entered, resume](qint64, qint64) { entered->store(true); resume.wait(); },
            Qt::DirectConnection);
        create->click();
        const bool hashingStarted = waitFor([&] { return entered->load(); });
        dialog.reject();
        release.set_value();
        REQUIRE(hashingStarted);
    }
    SECTION("failure") {
        control<QPlainTextEdit>(dialog, "torrentCreateTrackers")->setPlainText("file:///not-a-tracker");
        create->click();
    }
    REQUIRE(waitFor([&] { return create->isEnabled(); }));
    REQUIRE_FALSE(control<QLabel>(dialog, "torrentCreateStatus")->text().isEmpty());
    REQUIRE(created.isEmpty());
    REQUIRE_FALSE(QFile::exists(root + "/created.torrent"));
    REQUIRE(QFile::exists(root + "/payload.bin"));
}

TEST_CASE("Dialog seeds only when requested and never relocates creator sources", "[qt][torrent-create-dialog]")
{
    using namespace eiskalt::torrent;
    QTemporaryDir dir;
    const auto root = QDir(dir.path()).canonicalPath();
    TorrentEngine engine(root + "/state");
    Settings settings;
    settings.bindAddress = "127.0.0.1";
    settings.bindAddress6.clear();
    settings.dht = settings.pex = settings.localDiscovery = settings.portMapping = settings.utp = false;
    settings.bootstrapNodes.clear();
    settings.proxyMode = ProxyMode::Direct;
    settings.downloadPath = root + "/downloads";
    settings.completedPath = root + "/archive";
    settings.shareCompleted = false;
    engine.configure(settings, {});
    TorrentCreateDialog dialog(&engine);
    paths(dialog, root);
    auto *seed = control<QCheckBox>(dialog, "torrentCreateSeed");
    REQUIRE(seed->isEnabled());
    REQUIRE_FALSE(seed->isChecked());
    bool seedRequested = false;
    SECTION("default creates no Torrent job") {}
    SECTION("requested seed retains originals despite completed directory preference") {
        seedRequested = true;
        seed->setChecked(true);
    }
    control<QCheckBox>(dialog, "torrentCreatePrivate")->setChecked(true);
    QSignalSpy created(&dialog, &TorrentCreateDialog::created);
    control<QPushButton>(dialog, "torrentCreateStart")->click();
    REQUIRE(waitFor([&] { return created.count() == 1; }));
    if (seedRequested) {
        REQUIRE(waitFor([&] {
            const auto jobs = engine.jobs();
            return jobs.size() == 1 && (jobs.first().complete || !jobs.first().error.isEmpty());
        }));
        const auto job = engine.jobs().first();
        INFO(job.error.toStdString());
        REQUIRE(job.error.isEmpty());
        REQUIRE(job.privateTorrent);
        REQUIRE(job.complete);
        REQUIRE(job.savePath == root);
    } else {
        REQUIRE(engine.jobs().isEmpty());
    }
    engine.shutdown();
    QFile source(root + "/payload.bin");
    REQUIRE(source.open(QIODevice::ReadOnly));
    REQUIRE(source.readAll() == QByteArray(70001, 'x'));
    REQUIRE_FALSE(QFile::exists(root + "/archive/payload.bin"));
}
#endif
#endif
