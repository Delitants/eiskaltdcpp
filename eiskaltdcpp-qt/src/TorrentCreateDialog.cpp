#include "TorrentCreateDialog.h"
#ifdef USE_TORRENT
#include "torrent/TorrentCreator.h"
#include "torrent/TorrentEngine.h"
#include "torrent/TorrentTrackers.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

using namespace eiskalt::torrent;

struct TorrentCreateDialog::Controls {
    QWidget *inputs;
    QLineEdit *source, *output;
    QPlainTextEdit *trackers;
    QCheckBox *privateTorrent, *seed;
    QPushButton *start, *cancel;
    QProgressBar *progress;
    QLabel *status;
    struct RemovedDefaultTracker {
        qsizetype line;
        QString url;
    };
    QList<RemovedDefaultTracker> removedDefaultTrackers;
    bool seedRequested = false;
};

TorrentCreateDialog::TorrentCreateDialog(TorrentEngine *torrentEngine, QWidget *parent)
    : QDialog(parent), ui(new Controls), engine(torrentEngine), creator(new TorrentCreator(this))
{
    setObjectName(QStringLiteral("torrentCreateDialog"));
    setWindowTitle(tr("Create Torrent"));
    setWindowModality(Qt::WindowModal);
    setModal(true);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("Create hybrid v1/v2 metadata from a file or folder. Sources are read-only; symlinks are not allowed. Choose a new output filename outside the source folder. Existing files are never overwritten."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    ui->inputs = new QWidget(this);
    // Stacked labels avoid Cocoa's restrictive QFormLayout field growth policy.
    auto *fieldsLayout = new QVBoxLayout(ui->inputs);
    fieldsLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(ui->inputs, 1);
    auto *sourceRow = new QWidget(ui->inputs);
    auto *sourceLayout = new QHBoxLayout(sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    ui->source = new QLineEdit(sourceRow);
    ui->source->setObjectName(QStringLiteral("torrentCreateSource"));
    ui->source->setMaxLength(4096);
    ui->source->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *file = new QPushButton(tr("File..."), sourceRow);
    file->setObjectName(QStringLiteral("torrentCreateBrowseFile"));
    auto *folder = new QPushButton(tr("Folder..."), sourceRow);
    folder->setObjectName(QStringLiteral("torrentCreateBrowseFolder"));
    sourceLayout->addWidget(ui->source, 1);
    sourceLayout->addWidget(file);
    sourceLayout->addWidget(folder);
    auto *sourceLabel = new QLabel(tr("Source:"), ui->inputs);
    sourceLabel->setBuddy(ui->source);
    fieldsLayout->addWidget(sourceLabel);
    fieldsLayout->addWidget(sourceRow);
    auto *outputRow = new QWidget(ui->inputs);
    auto *outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    ui->output = new QLineEdit(outputRow);
    ui->output->setObjectName(QStringLiteral("torrentCreateOutput"));
    ui->output->setMaxLength(4096);
    ui->output->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *outputBrowse = new QPushButton(tr("Browse..."), outputRow);
    outputBrowse->setObjectName(QStringLiteral("torrentCreateBrowseOutput"));
    outputLayout->addWidget(ui->output, 1);
    outputLayout->addWidget(outputBrowse);
    auto *outputLabel = new QLabel(tr("Output .torrent:"), ui->inputs);
    outputLabel->setBuddy(ui->output);
    fieldsLayout->addWidget(outputLabel);
    fieldsLayout->addWidget(outputRow);
    ui->trackers = new QPlainTextEdit(ui->inputs);
    ui->trackers->setObjectName(QStringLiteral("torrentCreateTrackers"));
    ui->trackers->setPlaceholderText(tr("Optional tracker URLs, one per line (HTTP, HTTPS or UDP)"));
    ui->trackers->setToolTip(ui->trackers->placeholderText());
    ui->trackers->setPlainText(defaultTrackers().join('\n'));
    ui->trackers->setMinimumHeight(std::max(160, ui->trackers->fontMetrics().lineSpacing() * 10));
    ui->trackers->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *trackersLabel = new QLabel(tr("Trackers:"), ui->inputs);
    trackersLabel->setBuddy(ui->trackers);
    fieldsLayout->addWidget(trackersLabel);
    fieldsLayout->addWidget(ui->trackers, 1);
    ui->privateTorrent = new QCheckBox(tr("Private Torrent"), ui->inputs);
    ui->privateTorrent->setObjectName(QStringLiteral("torrentCreatePrivate"));
    ui->privateTorrent->setToolTip(tr("Sets the private flag. Private Torrents remain excluded from managed DC sharing and peer discovery."));
    fieldsLayout->addWidget(ui->privateTorrent);
    ui->seed = new QCheckBox(tr("Seed after creation (read-only sources, verify first)"), ui->inputs);
    ui->seed->setObjectName(QStringLiteral("torrentCreateSeed"));
    ui->seed->setChecked(false);
    fieldsLayout->addWidget(ui->seed);
    auto *privacy = new QLabel(tr("Creation itself uses no network. Seeding uses the existing Torrent preferences and private-Torrent exclusions. Existing manual DC shares are not changed or protected by the private flag."), this);
    privacy->setWordWrap(true);
    layout->addWidget(privacy);
    ui->progress = new QProgressBar(this);
    ui->progress->setObjectName(QStringLiteral("torrentCreateProgress"));
    ui->progress->setRange(0, 1000);
    ui->progress->setValue(0);
    layout->addWidget(ui->progress);
    ui->status = new QLabel(this);
    ui->status->setObjectName(QStringLiteral("torrentCreateStatus"));
    ui->status->setTextFormat(Qt::PlainText);
    ui->status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui->status->setWordWrap(true);
    layout->addWidget(ui->status);
    auto *buttons = new QDialogButtonBox(this);
    ui->start = new QPushButton(tr("Create"), this);
    ui->start->setObjectName(QStringLiteral("torrentCreateStart"));
    ui->start->setDefault(true);
    ui->cancel = new QPushButton(tr("Close"), this);
    ui->cancel->setObjectName(QStringLiteral("torrentCreateCancel"));
    buttons->addButton(ui->start, QDialogButtonBox::AcceptRole);
    buttons->addButton(ui->cancel, QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    auto selectSource = [this](const QString &path) {
        if (path.isEmpty()) return;
        ui->source->setText(path);
        if (ui->output->text().isEmpty()) ui->output->setText(path + QStringLiteral(".torrent"));
    };
    connect(file, &QPushButton::clicked, this, [this, selectSource] {
        selectSource(QFileDialog::getOpenFileName(this, tr("Select Torrent source file"), ui->source->text(),
            tr("All files (*)"), nullptr, QFileDialog::DontResolveSymlinks));
    });
    connect(folder, &QPushButton::clicked, this, [this, selectSource] {
        selectSource(QFileDialog::getExistingDirectory(this, tr("Select Torrent source folder"), ui->source->text(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks));
    });
    connect(outputBrowse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, tr("Choose a new Torrent output file"), ui->output->text(),
            tr("Torrent files (*.torrent)"), nullptr, QFileDialog::DontConfirmOverwrite | QFileDialog::DontResolveSymlinks);
        if (!path.isEmpty()) ui->output->setText(path);
    });
    connect(ui->source, &QLineEdit::textChanged, this, &TorrentCreateDialog::updateActions);
    connect(ui->output, &QLineEdit::textChanged, this, &TorrentCreateDialog::updateActions);
    connect(ui->privateTorrent, &QCheckBox::toggled, this, &TorrentCreateDialog::updatePrivateTrackers);
    connect(ui->start, &QPushButton::clicked, this, &TorrentCreateDialog::startCreation);
    connect(ui->cancel, &QPushButton::clicked, this, &TorrentCreateDialog::reject);
    connect(creator, &TorrentCreator::progress, this, [this](qint64 completed, qint64 total) {
        if (!busy) return;
        ui->progress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) ui->progress->setValue(int(std::clamp(completed * 1000 / total, qint64(0), qint64(1000))));
    });
    connect(creator, &TorrentCreator::failed, this, [this](const QString &message) {
        setBusy(false);
        ui->progress->setRange(0, 1000);
        ui->progress->setValue(0);
        ui->status->setText(tr("Creation failed: %1").arg(message));
    });
    connect(creator, &TorrentCreator::cancelled, this, [this] {
        setBusy(false);
        ui->progress->setRange(0, 1000);
        ui->progress->setValue(0);
        ui->status->setText(tr("Creation cancelled. No output was published; source files were not changed."));
    });
    connect(creator, &TorrentCreator::finished, this, [this](const QString &metadata, const QString &parentDirectory) {
        setBusy(false);
        ui->progress->setRange(0, 1000);
        ui->progress->setValue(1000);
        QString status = tr("Created: %1").arg(metadata);
        if (ui->seedRequested) {
            // Normal add() may relocate completed data or repair missing files.
            // The protected seed path preserves originals and still hash-checks
            // them, with the engine's existing private/public exclusions intact.
            const auto id = engine && engine->settings().enabled ? engine->seedCreated(metadata, parentDirectory) : QString();
            status += id.isEmpty() ? tr("\nMetadata was saved, but read-only seeding could not be started. Check Torrent preferences.") :
                                    tr("\nAdded for read-only seeding. Source files will be hash-checked, not moved or downloaded.");
        }
        ui->status->setText(status);
        emit created(metadata, parentDirectory);
    });
    if (engine) {
        connect(engine, &TorrentEngine::changed, this, &TorrentCreateDialog::updateActions);
        connect(engine, &QObject::destroyed, this, &TorrentCreateDialog::updateActions);
    }
    updateActions();
    // Account for wrapped descriptions at the narrowest allowed width, not
    // just the one-line label minima reported by minimumSizeHint().
    const int minimumWidth = std::max(560, minimumSizeHint().width());
    setMinimumSize(minimumWidth, std::max(minimumSizeHint().height(), layout->totalHeightForWidth(minimumWidth)));
    resize(740, std::max(600, minimumSizeHint().height()));
}

TorrentCreateDialog::~TorrentCreateDialog()
{
    // Join while the dialog's controls still exist; queued GUI callbacks cannot
    // outlive the sender or the dialog.
    delete creator;
}

void TorrentCreateDialog::reject()
{
    if (!busy) { QDialog::reject(); return; }
    creator->cancel();
    ui->status->setText(tr("Cancelling creation..."));
    ui->cancel->setEnabled(false);
}

void TorrentCreateDialog::startCreation()
{
    if (busy || !ui->start->isEnabled()) return;
    CreateTorrentOptions options;
    options.sourcePath = ui->source->text();
    options.outputPath = ui->output->text();
    options.trackers = ui->trackers->toPlainText().split('\n', Qt::SkipEmptyParts);
    options.privateTorrent = ui->privateTorrent->isChecked();
    if (options.privateTorrent) {
        // Also filter at submission: shipped public URLs can be pasted back
        // into the editable list after the private checkbox was selected.
        const auto defaults = defaultTrackers();
        options.trackers.removeIf([&defaults](const QString &url) { return defaults.contains(url.trimmed()); });
    }
    ui->seedRequested = ui->seed->isChecked();
    setBusy(true);
    ui->progress->setRange(0, 0);
    ui->status->setText(tr("Scanning and hashing source files..."));
    if (!creator->start(options)) {
        setBusy(false);
        ui->status->setText(tr("A creation operation is already running."));
    }
}

void TorrentCreateDialog::updatePrivateTrackers(bool privateTorrent)
{
    auto lines = ui->trackers->toPlainText().split('\n');
    if (privateTorrent) {
        const auto defaults = defaultTrackers();
        ui->removedDefaultTrackers.clear();
        QStringList retained;
        for (qsizetype index = 0; index < lines.size(); ++index) {
            const auto &line = lines[index];
            const auto url = line.trimmed();
            if (defaults.contains(url)) ui->removedDefaultTrackers.append({index, url});
            else retained.append(line);
        }
        lines = retained;
    } else {
        // Keep original priority slots, clamped if private edits shortened the
        // list. Never resurrect custom lines or defaults deleted before toggling.
        for (const auto &tracker : ui->removedDefaultTrackers) {
            if (std::none_of(lines.cbegin(), lines.cend(), [&tracker](const QString &line) { return line.trimmed() == tracker.url; }))
                lines.insert(std::min(tracker.line, lines.size()), tracker.url);
        }
        ui->removedDefaultTrackers.clear();
    }
    const auto text = lines.join('\n');
    if (text != ui->trackers->toPlainText()) ui->trackers->setPlainText(text);
}

void TorrentCreateDialog::updateActions()
{
#ifndef Q_OS_UNIX
    ui->inputs->setEnabled(false);
    ui->start->setEnabled(false);
    ui->status->setText(tr("Torrent creation is unavailable on this platform: secure no-follow source access has not been implemented. Existing Torrent management remains available."));
    return;
#endif
    const QFileInfo source(ui->source->text()), output(ui->output->text());
    ui->start->setEnabled(!busy && QDir::isAbsolutePath(ui->source->text()) &&
        QDir::isAbsolutePath(ui->output->text()) && source.exists() &&
        !source.isSymLink() && (source.isFile() || source.isDir()) &&
        !output.exists() && !output.isSymLink() && QFileInfo(output.absolutePath()).isDir());
    const bool seedAvailable = engine && engine->settings().enabled;
    ui->seed->setEnabled(!busy && seedAvailable);
    if (!seedAvailable && !busy) ui->seed->setChecked(false);
}

void TorrentCreateDialog::setBusy(bool value)
{
    busy = value;
    ui->inputs->setEnabled(!busy);
    ui->cancel->setEnabled(true);
    ui->cancel->setText(busy ? tr("Cancel") : tr("Close"));
    updateActions();
}
#endif
