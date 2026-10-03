#include "SettingsUpdates.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace updates {
namespace {
// Keep the native indicator, shortcuts and accessibility while wrapping long captions.
class WrappingCheckBox final : public QCheckBox {
public:
    WrappingCheckBox(const QString& text, QWidget* parent) : QCheckBox(text, parent) {
        auto policy = sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Preferred);
        policy.setVerticalPolicy(QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    QSize minimumSizeHint() const override {
        const auto native = QCheckBox::minimumSizeHint();
        auto caption = text();
        caption.remove('&');
        int longest = 0;
        for(const auto& word : caption.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
            longest = qMax(longest, fontMetrics().horizontalAdvance(word));
        const auto padding = native.width() - fontMetrics().horizontalAdvance(caption);
        return {qMax(0, padding) + longest, native.height()};
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        const auto native = QCheckBox::sizeHint();
        QStyleOptionButton option;
        initStyleOption(&option);
        option.rect = QRect(0, 0, width, native.height());
        const auto contents = style()->subElementRect(QStyle::SE_CheckBoxContents, &option, this);
        const auto wrapped = fontMetrics().boundingRect(QRect(0, 0, qMax(1, contents.width()), 100000),
            Qt::TextWordWrap | Qt::TextHideMnemonic, text());
        return native.height() + qMax(0, wrapped.height() - fontMetrics().height());
    }
protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent* event) override {
        const auto native = QCheckBox::sizeHint();
        if(heightForWidth(width()) <= native.height()) {
            QCheckBox::paintEvent(event);
            return;
        }
        QStyleOptionButton option;
        initStyleOption(&option);
        const auto contents = style()->subElementRect(QStyle::SE_CheckBoxContents, &option, this);
        option.text.clear();
        option.rect.setHeight(native.height());
        QStylePainter painter(this);
        painter.drawControl(QStyle::CE_CheckBox, option);
        const auto mnemonic = style()->styleHint(QStyle::SH_UnderlineShortcut, &option, this)
            ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
        painter.drawItemText(contents, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextWordWrap | mnemonic,
            option.palette, isEnabled(), text(), QPalette::WindowText);
    }
};

template<class T> T* named(T* widget, const char* name, const QString& accessible) {
    widget->setObjectName(QString::fromLatin1(name));
    widget->setAccessibleName(accessible);
    return widget;
}
QLabel* plainLabel(const char* name, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(QString::fromLatin1(name));
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    return label;
}
}

SettingsUpdates::SettingsUpdates(UpdateController& owner, QWidget* parent)
    : QWidget(parent), controller(&owner) {
    auto* layout = new QVBoxLayout(this);
    auto* preferences = new QGroupBox(tr("Update preferences"), this);
    auto* preferenceLayout = new QVBoxLayout(preferences);
    checks = named(new WrappingCheckBox(tr("&Check for updates automatically"), preferences),
        "updateChecks", tr("Check for updates automatically"));
    notifications = named(new WrappingCheckBox(tr("N&otify me when updates are available"), preferences),
        "updateNotifications", tr("Notify me when updates are available"));
    autoInstall = named(new WrappingCheckBox(tr("Download and &install on next normal quit"), preferences),
        "updateAutoInstall", tr("Download and install on next normal quit"));
    preferenceLayout->addWidget(checks);
    preferenceLayout->addWidget(notifications);
    preferenceLayout->addWidget(autoInstall);
    auto* channelLayout = new QFormLayout;
    channel = named(new QComboBox(preferences), "updateChannel", tr("Release channel"));
    channel->addItem(tr("Stable"), static_cast<int>(Channel::Stable));
    channel->addItem(tr("Preview"), static_cast<int>(Channel::Preview));
    channelLayout->addRow(tr("Release c&hannel:"), channel);
    preferenceLayout->addLayout(channelLayout);
    installSupport = plainLabel("updateInstallSupport", preferences);
    preferenceLayout->addWidget(installSupport);
    layout->addWidget(preferences);

    auto* activity = new QGroupBox(tr("Update status"), this);
    auto* activityLayout = new QVBoxLayout(activity);
    lastSuccess = plainLabel("updateLastSuccess", activity);
    version = plainLabel("updateVersion", activity);
    result = plainLabel("updateResult", activity);
    activityLayout->addWidget(lastSuccess);
    activityLayout->addWidget(version);
    activityLayout->addWidget(result);
    notes = named(new QTextBrowser(activity), "updateNotes", tr("Release notes"));
    notes->setOpenExternalLinks(false);
    notes->setOpenLinks(false);
    notes->setMinimumWidth(0);
    notes->setMinimumHeight(100);
    activityLayout->addWidget(notes);
    progress = named(new QProgressBar(activity), "updateProgress", tr("Update download progress"));
    activityLayout->addWidget(progress);
    auto* actions = new QFormLayout;
    actions->setRowWrapPolicy(QFormLayout::WrapLongRows);
    actions->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto button = [&](const QString& text, const char* name) {
        auto accessible = text;
        accessible.remove('&');
        auto* widget = named(new QPushButton(text, activity), name, accessible);
        return widget;
    };
    checkNow = button(tr("Check &Now"), "updateCheckNow");
    cancel = button(tr("Cance&l"), "updateCancel");
    download = button(tr("&Download"), "updateDownload");
    installOnQuit = button(tr("Install on Next &Quit"), "updateInstallOnQuit");
    later = button(tr("La&ter"), "updateLater");
    skip = button(tr("&Skip This Version"), "updateSkip");
    actions->addRow(checkNow, cancel);
    actions->addRow(download, installOnQuit);
    actions->addRow(later, skip);
    activityLayout->addLayout(actions);
    layout->addWidget(activity);
    layout->addStretch();

    const auto saved = owner.preferences();
    checks->setChecked(saved.checks);
    notifications->setChecked(saved.notifications);
    autoInstall->setChecked(saved.autoInstall);
    channel->setCurrentIndex(channel->findData(static_cast<int>(saved.channel)));
    connect(checks, &QCheckBox::toggled, this, &SettingsUpdates::refreshAutoInstall);
    connect(checkNow, &QPushButton::clicked, this, [this] {
        if(controller) { result->clear(); controller->checkNow(); refresh(); }
    });
    connect(download, &QPushButton::clicked, this, [this] {
        if(controller && controller->supportsInstallation()) { result->clear(); controller->download(); refresh(); }
    });
    connect(installOnQuit, &QPushButton::clicked, this, [this] {
        if(!controller || !controller->supportsInstallation() || controller->state() != UpdateController::State::Ready) return;
        controller->installOnNextQuit();
        result->setText(tr("This version is selected for installation after a successful normal quit. No restart is forced."));
        refresh();
    });
    connect(later, &QPushButton::clicked, this, [this] {
        if(controller) {
            controller->later();
            result->setText(tr("Installation deferred. Your downloaded update is not installed on quit."));
            refresh();
        }
    });
    connect(skip, &QPushButton::clicked, this, [this] {
        if(controller) { controller->skipVersion(); result->setText(tr("This version will be skipped.")); refresh(); }
    });
    connect(cancel, &QPushButton::clicked, this, [this] {
        if(controller) { controller->cancel(); result->setText(tr("Update operation canceled.")); refresh(); }
    });
    connect(&owner, &UpdateController::stateChanged, this, [this] { refresh(); });
    connect(&owner, &UpdateController::checkFinished, this, [this](bool available, CheckKind kind) {
        if(controller && (kind == CheckKind::Manual || controller->preferences().notifications))
            result->setText(available ? tr("A newer compatible version is available.") : tr("No newer compatible version is available."));
        refresh();
    });
    connect(&owner, &UpdateController::errorReported, this, [this](const QString& message, bool manual) {
        if(controller && (manual || controller->preferences().notifications)) result->setText(message);
        refresh();
    });
    connect(&owner, &UpdateController::progress, this, [this](qint64 received, qint64 total) {
        if(!controller || controller->state() != UpdateController::State::Downloading) return;
        if(total <= 0) { progress->setRange(0, 0); return; }
        progress->setRange(0, 100);
        progress->setValue(static_cast<int>(100.0 * static_cast<double>(received) / static_cast<double>(total)));
    });
    connect(&owner, &QObject::destroyed, this, [this] { controller.clear(); setEnabled(false); });
    refresh();
}

void SettingsUpdates::apply() {
    if(!controller) return;
    UpdatePreferences selected{checks->isChecked(), notifications->isChecked(), autoInstall->isChecked(),
        static_cast<Channel>(channel->currentData().toInt())};
    controller->applyPreferences(selected);
    refresh();
}

void SettingsUpdates::refreshAutoInstall() {
    autoInstall->setEnabled(controller && controller->supportsInstallation() && checks->isChecked());
}

void SettingsUpdates::refresh() {
    if(!controller) { setEnabled(false); return; }
    using State = UpdateController::State;
    const auto state = controller->state();
    const bool terminal = state == State::Stopped || state == State::Handoff;
    const bool busy = state == State::Checking || state == State::Downloading;
    const bool offered = state == State::Available || state == State::Downloading || state == State::Ready;
    const bool supported = controller->supportsInstallation();
    setEnabled(!terminal);
    refreshAutoInstall();
    installSupport->setText(supported
        ? tr("Updates are staged first. Installation requires a successful normal quit and verified shutdown; crashes never authorize installation.")
        : tr("Update installation is unavailable in this build. You can still check for updates manually."));
    const auto success = controller->lastSuccess();
    lastSuccess->setText(success.isValid()
        ? tr("Last successful check: %1").arg(QLocale().toString(success.toLocalTime(), QLocale::ShortFormat))
        : tr("Last successful check: never"));
    const auto selected = controller->offer();
    version->setText(offered ? tr("Available version: %1").arg(selected.version) : QString());
    notes->setVisible(offered && !selected.notes.isEmpty());
    // Signed metadata is still untrusted display content. Never load HTML,
    // links or remote resources from a release's notes.
    notes->setPlainText(offered ? selected.notes : QString());
    progress->setVisible(busy);
    if(state == State::Checking) progress->setRange(0, 0);
    else if(state == State::Downloading && progress->maximum() != 100) { progress->setRange(0, 100); progress->setValue(0); }
    else if(!busy) { progress->setRange(0, 100); progress->setValue(0); }
    checkNow->setEnabled(!busy && !terminal);
    cancel->setEnabled(busy && !terminal);
    download->setEnabled(supported && state == State::Available);
    installOnQuit->setEnabled(supported && state == State::Ready);
    later->setEnabled(offered);
    skip->setEnabled(offered);
}
}
