#include "TorrentWindow.h"
#include "TorrentCheckDelegate.h"

#ifdef USE_TORRENT
#include "TorrentCreateDialog.h"
#include "TorrentToolbar.h"
#include "TorrentActionMenu.h"
#include "FileExtensionIcons.h"
#include "AppIconTheme.h"
#include "CountryNames.h"
#ifndef QT_CONTEXT_MINIMAL
#include "QtContext.h"
#include "QtContextAware.h"
#include "WulforUtil.h"
#endif
#include "torrent/TorrentEngine.h"
#include <QAction>
#include <QMenu>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

using namespace eiskalt::torrent;

namespace {
enum Column { Name, Status, Progress, Size, DownloadSpeed, UploadSpeed, Peers, Seeds, Sharing, Count };
enum PeerColumn { PeerAddress, PeerCountry, PeerClient, PeerProgress, PeerDownload, PeerUpload, PeerState, PeerCount };
constexpr int PeerSortRole = Qt::UserRole + 1;
constexpr int FilePriorityRole = Qt::UserRole + 1;

class FilePriorityDelegate final : public QStyledItemDelegate {
public:
    explicit FilePriorityDelegate(QTreeWidget *view)
        : QStyledItemDelegate(view), sizingCombo(new QComboBox(view->viewport()))
    {
        sizingCombo->setObjectName(QStringLiteral("torrentFilePrioritySizing"));
        populate(sizingCombo);
        sizingCombo->hide();
    }
    ~FilePriorityDelegate() override { delete sizingCombo.data(); }

    QSize editorCellSizeHint(const QFont &font) const
    {
        // Include all translated choices, inherited QSS, and the editor's cell insets.
        sizingCombo->setFont(font);
        sizingCombo->ensurePolished();
        return sizingCombo->sizeHint() + QSize(4, 2);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (index.column() == 2) {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            // One hidden real combo measures inherited QSS/font metrics for all rows.
            size = size.expandedTo(editorCellSizeHint(opt.font));
        }
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.column() != 2) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();
        auto *style = opt.widget ? opt.widget->style() : QApplication::style();
        painter->save();
        painter->setClipRect(opt.rect);
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const auto group = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
            : (opt.state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
        const auto role = (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
        painter->setPen(opt.palette.color(group, role));
        painter->setFont(opt.font);
        const auto textRect = QStyle::visualRect(opt.direction, opt.rect, opt.rect.adjusted(6, 0, -24, 0));
        painter->drawText(textRect, QStyle::visualAlignment(opt.direction, Qt::AlignLeft | Qt::AlignVCenter),
                         opt.fontMetrics.elidedText(text, opt.textElideMode, qMax(0, textRect.width())));
        const auto arrow = QStyle::visualRect(opt.direction, opt.rect,
            QRect(opt.rect.right() - 21, opt.rect.center().y() - 8, 16, 16));
        const QPointF center = arrow.center();
        QPainterPath chevron;
        chevron.moveTo(center + QPointF(-3.5, -1.5));
        chevron.lineTo(center + QPointF(0, 2));
        chevron.lineTo(center + QPointF(3.5, -1.5));
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(opt.palette.color(group, role), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(chevron);
        painter->restore();
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.column() != 2) return nullptr;
        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("torrentFilePriorityEditor"));
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        combo->setFont(opt.font);
        populate(combo);
        auto *self = const_cast<FilePriorityDelegate *>(this);
        QObject::connect(combo, &QComboBox::activated, self, [self, combo] {
            emit self->commitData(combo);
            emit self->closeEditor(combo);
        });
        QTimer::singleShot(0, combo, &QComboBox::showPopup);
        return combo;
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        editor->setGeometry(option.rect.adjusted(2, 1, -2, -1));
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        combo->setCurrentIndex(combo->findData(index.data(FilePriorityRole).toInt()));
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        model->setData(index, combo->currentData(), FilePriorityRole);
        model->setData(index, combo->currentText(), Qt::DisplayRole);
    }
private:
    static void populate(QComboBox *combo)
    {
        combo->addItem(TorrentWindow::tr("Low"), 1);
        combo->addItem(TorrentWindow::tr("Normal"), 4);
        combo->addItem(TorrentWindow::tr("High"), 7);
    }
    QPointer<QComboBox> sizingCombo;
};

class PeerItem final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget()->sortColumn();
        int comparison = 0;
        if (column == PeerAddress) {
            comparison = data(column, PeerSortRole).toByteArray().compare(other.data(column, PeerSortRole).toByteArray());
        } else if (column == PeerProgress) {
            const double a = data(column, PeerSortRole).toDouble(), b = other.data(column, PeerSortRole).toDouble();
            comparison = a < b ? -1 : a > b ? 1 : 0;
        } else if (column == PeerDownload || column == PeerUpload) {
            const auto a = data(column, PeerSortRole).toLongLong(), b = other.data(column, PeerSortRole).toLongLong();
            comparison = a < b ? -1 : a > b ? 1 : 0;
        } else {
            comparison = QString::localeAwareCompare(text(column), other.text(column));
        }
        return comparison ? comparison < 0 :
            data(PeerAddress, Qt::UserRole).toString() < other.data(PeerAddress, Qt::UserRole).toString();
    }
};

QByteArray endpointSortKey(const Peer &peer)
{
    const QHostAddress address(peer.ip);
    QByteArray key;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        key.append(char(4));
        const auto ip = address.toIPv4Address();
        for (int shift = 24; shift >= 0; shift -= 8)
            key.append(char((ip >> shift) & 0xff));
    } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        key.append(char(6));
        const auto ip = address.toIPv6Address();
        key.append(reinterpret_cast<const char *>(ip.c), 16);
    } else {
        key.append(char(0));
        key.append(peer.ip.toUtf8());
        key.append(char(0));
    }
    key.append(char(peer.port >> 8));
    key.append(char(peer.port & 0xff));
    return key;
}

QStringList normalizedCountries(const QStringList &countries)
{
    QStringList result;
    for (const auto &country : countries) {
        const auto code = country.trimmed().toUpper();
        if (!country_names::fromCode(code).isEmpty() && !result.contains(code))
            result.append(code);
    }
    result.sort();
    return result;
}

QIcon countryFlag(const QString &code, qreal scale)
{
    if (country_names::fromCode(code).isEmpty())
        return {};
    // Regional indicators use the platform's flag glyphs; no IP lookup or disk I/O.
    QString flag;
    for (const auto character : code) {
        const uint point = 0x1f1e6u + character.unicode() - 'A';
        flag.append(QChar::highSurrogate(point));
        flag.append(QChar::lowSurrogate(point));
    }
    QPixmap pixmap(QSize(qRound(24 * scale), qRound(18 * scale)));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QFont font = QApplication::font();
    font.setPixelSize(16);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, 24, 18), Qt::AlignCenter, flag);
    painter.end();
    return QIcon(pixmap);
}

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(qMax<qint64>(0, bytes));
}

// Resolve existing ancestors too, so a new subdirectory under a symlink cannot
// evade the warning about an existing manual DC directory share.
QString resolvedPath(const QString &path)
{
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList missing;
    while (!info.exists() && !info.isSymLink()) {
        missing.prepend(info.fileName());
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            break;
        info.setFile(parent);
    }
    QString base = info.canonicalFilePath();
    if (base.isEmpty())
        base = info.absoluteFilePath();
    for (const auto &part : missing)
        base = QDir(base).filePath(part);
    return QDir::cleanPath(base);
}

bool insideDirectory(const QString &path, const QString &directory)
{
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    const auto parent = resolvedPath(directory);
    const auto child = resolvedPath(path);
    return child.compare(parent, sensitivity) == 0 ||
        child.startsWith(parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/'), sensitivity);
}
}

struct TorrentWindow::Controls {
    QTreeWidget *jobs, *files, *peers;
    QTabWidget *details;
    QSplitter *splitter;
    QLabel *notice, *fileNotice, *peerNotice, *sharing;
    QPushButton *applyFiles;
    QAction *addFile, *addMagnet, *pause, *resume, *stop, *remove, *deleteFiles, *recheck;
    QAction *shareDc, *shareTorrent;
    QAction *dcShareFollowGlobal, *dcShareExclude;
    QMenu *dcSharing;
    FileExtensionIcons fileIcons;
    QTimer *refreshTimer, *layoutTimer;
    QPointer<QObject> dragTarget;
    bool pendingRefresh = false, pendingPeers = false;
    QString pendingPeerJobId;
    QList<Peer> pendingPeerSnapshot;
    QHash<QString, QIcon> countryFlags;
    QComboBox *encryption;
    QListWidget *countries;
    QLineEdit *countrySearch;
    QCheckBox *blockUnknown;
    QPushButton *applySecurity;
    EncryptionMode savedEncryption = EncryptionMode::Optional;
    QStringList savedCountries;
    bool savedBlockUnknown = false, securityInitialized = false;
    QList<FileEntry> displayedFiles;
    bool filesInitialized = false;
    bool errorVisible = false;
    QHash<QString, QString> sharingStatuses;
    QString restoreJobId, peerJobId;
};

TorrentWindow::TorrentWindow(TorrentEngine *torrentEngine, const QString &layoutPath, QWidget *parent)
    : QWidget(parent), ui(new Controls), engine(torrentEngine), layoutPath(layoutPath),
      tabPixmap(torrent_toolbar::icon().pixmap(16, 16))
{
    setState(ArenaWidget::Singleton);
    setObjectName(QStringLiteral("TorrentWindow"));
    setWindowTitle(tr("Torrents"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    auto *toolbar = new QToolBar(tr("Torrent actions"), this);
    rootLayout->addWidget(toolbar);
    toolbar->setObjectName(QStringLiteral("torrentToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto action = [this, toolbar](const QString &text, const char *name, QStyle::StandardPixmap icon) {
        auto *result = toolbar->addAction(style()->standardIcon(icon), text);
        result->setObjectName(QString::fromLatin1(name));
        return result;
    };
    ui->addFile = action(tr("Add file..."), "torrentAddFile", QStyle::SP_DialogOpenButton);
    ui->addMagnet = action(tr("Add magnet..."), "torrentAddMagnet", QStyle::SP_FileLinkIcon);
    auto *create = action(tr("Create Torrent..."), "torrentCreate", QStyle::SP_FileIcon);
#ifndef Q_OS_UNIX
    create->setEnabled(false);
    create->setToolTip(tr("Torrent creation is unavailable on this platform because secure no-follow source access is not implemented."));
#endif
    connect(create, &QAction::triggered, this, &TorrentWindow::createTorrent);
    toolbar->addSeparator();
    const auto controls = torrent_action_menu::create(toolbar, this);
    ui->pause = controls.pause;
    ui->resume = controls.resume;
    ui->stop = controls.stop;
    toolbar->addActions({ui->pause, ui->resume, ui->stop});
    ui->stop->setToolTip(tr("Stop all Torrent activity until Resume. Keep the job and downloaded files."));
    ui->recheck = action(tr("Recheck..."), "torrentRecheck", QStyle::SP_BrowserReload);
    ui->remove = action(tr("Remove..."), "torrentRemove", QStyle::SP_DialogCloseButton);
    ui->deleteFiles = action(tr("Delete data..."), "torrentDelete", QStyle::SP_TrashIcon);
    toolbar->addSeparator();
    auto *preferences = action(tr("Preferences..."), "torrentSettings", QStyle::SP_FileDialogDetailedView);
    connect(preferences, &QAction::triggered, this, &TorrentWindow::settingsRequested);
    reloadIcons();
#ifndef QT_CONTEXT_MINIMAL
    connect(qtCtx()->wulforUtil(), &WulforUtil::iconsReloaded, this, &TorrentWindow::reloadIcons);
#endif

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    rootLayout->addWidget(central, 1);
    ui->notice = new QLabel(central);
    ui->notice->setObjectName(QStringLiteral("torrentNotice"));
    ui->notice->setTextFormat(Qt::PlainText);
    ui->notice->setWordWrap(true);
    ui->notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(ui->notice);
    auto *splitter = ui->splitter = new QSplitter(Qt::Vertical, central);
    splitter->setHandleWidth(9);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);
    ui->jobs = new QTreeWidget(splitter);
    ui->jobs->setObjectName(QStringLiteral("torrentJobs"));
    ui->jobs->setRootIsDecorated(false);
    ui->jobs->setAlternatingRowColors(true);
    ui->jobs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->jobs->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->jobs->addActions({ui->pause, ui->resume, ui->stop, ui->recheck, ui->remove});
    ui->shareDc = new QAction(tr("Paste DC++ magnet(s) in chat"), ui->jobs);
    ui->shareTorrent = new QAction(tr("Paste Torrent Magnet(s) in chat"), ui->jobs);
    ui->shareDc->setObjectName(QStringLiteral("torrentShareDc"));
    ui->shareTorrent->setObjectName(QStringLiteral("torrentShareTorrent"));
    ui->dcSharing = new QMenu(tr("DC++ sharing"), ui->jobs);
    ui->dcShareFollowGlobal = ui->dcSharing->addAction(tr("Follow global setting"));
    ui->dcShareExclude = ui->dcSharing->addAction(tr("Exclude"));
    ui->dcShareFollowGlobal->setObjectName(QStringLiteral("torrentDcShareFollowGlobal"));
    ui->dcShareExclude->setObjectName(QStringLiteral("torrentDcShareExclude"));
    ui->dcSharing->setToolTipsVisible(true);
    for (auto *choice : {ui->dcShareFollowGlobal, ui->dcShareExclude}) {
        choice->setCheckable(true);
        choice->setToolTip(tr("Controls managed Torrent sharing only. Manual DC++ shares are unchanged; private Torrents remain excluded."));
        connect(choice, &QAction::triggered, this, [this, choice] {
            const auto ids = selectedIds();
            for (const auto &id : ids)
                if (engine) engine->setDcShareExcluded(id, choice == ui->dcShareExclude);
            refresh();
        });
    }
    auto *separator = new QAction(ui->jobs);
    separator->setSeparator(true);
    ui->jobs->addActions({separator, ui->dcSharing->menuAction(), ui->shareDc, ui->shareTorrent});
    for (auto *share : {ui->shareDc, ui->shareTorrent}) {
        connect(share, &QAction::triggered, this, [this, share] {
            const auto ids = selectedIds();
            if (engine && !ids.isEmpty())
                emit magnetShareRequested(ids, share == ui->shareDc);
        });
    }
    ui->jobs->setHeaderLabels({tr("Name"), tr("Status"), tr("Progress"), tr("Size"), tr("Download speed"), tr("Upload speed"), tr("Peers"), tr("Seeds"), tr("DC sharing")});
    ui->jobs->header()->setStretchLastSection(false);
    const int widths[] = {220, 145, 90, 90, 110, 110, 65, 65, 190};
    for (int column = 0; column < Count; ++column)
        ui->jobs->setColumnWidth(column, widths[column]);
    ui->jobs->header()->moveSection(Sharing, 2);

    ui->details = new QTabWidget(splitter);
    ui->details->setObjectName(QStringLiteral("torrentDetails"));
    auto *filesPanel = new QWidget(ui->details);
    ui->details->addTab(filesPanel, tr("Files"));
    auto *filesLayout = new QVBoxLayout(filesPanel);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    ui->fileNotice = new QLabel(filesPanel);
    ui->fileNotice->setTextFormat(Qt::PlainText);
    ui->fileNotice->setWordWrap(true);
    filesLayout->addWidget(ui->fileNotice);
    ui->files = new QTreeWidget(filesPanel);
    ui->files->setObjectName(QStringLiteral("torrentFiles"));
    auto *priorityDelegate = new FilePriorityDelegate(ui->files);
    ui->files->setItemDelegate(priorityDelegate);
    ui->files->setItemDelegateForColumn(0, new TorrentCheckDelegate(ui->files));
    ui->files->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->files->setRootIsDecorated(false);
    ui->files->setAlternatingRowColors(true);
    ui->files->setHeaderLabels({tr("Download selected files"), tr("Size"), tr("Priority")});
    ui->files->header()->setStretchLastSection(false);
    ui->files->setColumnWidth(0, 650);
    ui->files->setColumnWidth(1, 130);
    // Set only the fresh default; saved header state below and later user resizes win.
    ui->files->setColumnWidth(2, qMax(110, priorityDelegate->editorCellSizeHint(ui->files->font()).width()));
    filesLayout->addWidget(ui->files, 1);
    connect(ui->files, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (column != 2) return;
        const QPersistentModelIndex index(ui->files->indexFromItem(item, column));
        // Finish the activating mouse release before installing the combo editor.
        QTimer::singleShot(0, ui->files, [tree = ui->files, index] {
            if (index.isValid()) tree->edit(index);
        });
    });
    ui->applyFiles = new QPushButton(tr("Apply file selection"), filesPanel);
    ui->applyFiles->setObjectName(QStringLiteral("torrentApplyFiles"));
    filesLayout->addWidget(ui->applyFiles, 0, Qt::AlignRight);
    auto *peersPanel = new QWidget(ui->details);
    ui->details->addTab(peersPanel, tr("Peers"));
    auto *peersLayout = new QVBoxLayout(peersPanel);
    peersLayout->setContentsMargins(0, 0, 0, 0);
    ui->peerNotice = new QLabel(peersPanel);
    ui->peerNotice->setObjectName(QStringLiteral("torrentPeerNotice"));
    ui->peerNotice->setTextFormat(Qt::PlainText);
    ui->peerNotice->setWordWrap(true);
    peersLayout->addWidget(ui->peerNotice);
    ui->peers = new QTreeWidget(peersPanel);
    ui->peers->setObjectName(QStringLiteral("torrentPeers"));
    ui->peers->setRootIsDecorated(false);
    ui->peers->setAlternatingRowColors(true);
    ui->peers->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->peers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->peers->setHeaderLabels({tr("IP / port"), tr("Country"), tr("Client"), tr("Progress"), tr("Down"), tr("Up"), tr("State")});
    ui->peers->header()->setStretchLastSection(false);
    const int peerWidths[] = {190, 110, 180, 85, 100, 100, 300};
    for (int column = 0; column < PeerCount; ++column)
        ui->peers->setColumnWidth(column, peerWidths[column]);
    ui->peers->setSortingEnabled(true);
    ui->peers->sortItems(PeerAddress, Qt::AscendingOrder);
    peersLayout->addWidget(ui->peers, 1);

    auto *securityScroll = new QScrollArea(ui->details);
    securityScroll->setObjectName(QStringLiteral("torrentSecurityScroll"));
    securityScroll->setWidgetResizable(true);
    securityScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    securityScroll->setFrameShape(QFrame::NoFrame);
    securityScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->details->addTab(securityScroll, tr("Security"));
    auto *securityPanel = new QWidget(securityScroll);
    auto *securityLayout = new QVBoxLayout(securityPanel);
    // Preserve content minimums inside the scroll viewport, not on every detail tab.
    // Height-for-width labels must scroll rather than overlap the country list.
    securityLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    auto *securityNotice = new QLabel(tr("These settings apply globally to all Torrents, not just the selected job. Changes take effect only after Apply security. Applying reconnects Torrent sessions; HTTP web seeds are disabled while either peer block rule is enabled because web seeds have no BitTorrent peer handshake."), securityPanel);
    securityNotice->setTextFormat(Qt::PlainText);
    securityNotice->setWordWrap(true);
    securityLayout->addWidget(securityNotice);
    auto *securityForm = new QFormLayout;
    ui->encryption = new QComboBox(securityPanel);
    ui->encryption->setObjectName(QStringLiteral("torrentSecurityEncryption"));
    ui->encryption->addItem(tr("Disabled"), static_cast<int>(EncryptionMode::Disabled));
    ui->encryption->addItem(tr("Optional"), static_cast<int>(EncryptionMode::Optional));
    ui->encryption->addItem(tr("Required"), static_cast<int>(EncryptionMode::Required));
    ui->encryption->setCurrentIndex(1);
    ui->encryption->setToolTip(tr("BitTorrent protocol encryption is not peer authentication, anonymity, or a VPN."));
    securityForm->addRow(tr("Peer encryption:"), ui->encryption);
    securityLayout->addLayout(securityForm);
    ui->countrySearch = new QLineEdit(securityPanel);
    ui->countrySearch->setObjectName(QStringLiteral("torrentSecuritySearch"));
    ui->countrySearch->setPlaceholderText(tr("Search countries by name or code"));
    ui->countrySearch->setClearButtonEnabled(true);
    auto *countryLabel = new QLabel(tr("Blocked countries (one global blocklist):"), securityPanel);
    countryLabel->setBuddy(ui->countrySearch);
    securityLayout->addWidget(countryLabel);
    securityLayout->addWidget(ui->countrySearch);
    ui->countries = new QListWidget(securityPanel);
    ui->countries->setObjectName(QStringLiteral("torrentSecurityCountries"));
    ui->countries->setItemDelegate(new TorrentCheckDelegate(ui->countries));
    ui->countries->setAccessibleName(tr("Blocked countries"));
    ui->countries->setMinimumHeight(120);
    for (char first = 'A'; first <= 'Z'; ++first) {
        for (char second = 'A'; second <= 'Z'; ++second) {
            const QString code = QString(QChar::fromLatin1(first)) + QChar::fromLatin1(second);
            const auto name = country_names::fromCode(code);
            if (name.isEmpty()) continue;
            auto *item = new QListWidgetItem(tr("%1 (%2)").arg(name, code), ui->countries);
            item->setData(Qt::UserRole, code);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
    }
    ui->countries->sortItems();
    securityLayout->addWidget(ui->countries, 1);
    auto *geoNotice = new QLabel(tr("GeoIP is approximate and may be missing or outdated. Country bans block only peers with a known IP country in this list; unmapped IPs remain allowed. A proxy or VPN can hide the actual location."), securityPanel);
    geoNotice->setTextFormat(Qt::PlainText);
    geoNotice->setWordWrap(true);
    securityLayout->addWidget(geoNotice);
    ui->blockUnknown = new QCheckBox(tr("Block unrecognized initial BitTorrent peer IDs (optional)"), securityPanel);
    ui->blockUnknown->setObjectName(QStringLiteral("torrentSecurityUnknown"));
    ui->blockUnknown->setChecked(false);
    securityLayout->addWidget(ui->blockUnknown);
    auto *unknownNotice = new QLabel(tr("Checked before payload transfer using the initial peer ID, not trust or authentication. An unknown client is not necessarily malicious. Legitimate clients can be blocked even if a later extension name would identify them."), securityPanel);
    unknownNotice->setTextFormat(Qt::PlainText);
    unknownNotice->setWordWrap(true);
    securityLayout->addWidget(unknownNotice);
    ui->applySecurity = new QPushButton(tr("Apply security"), securityPanel);
    ui->applySecurity->setObjectName(QStringLiteral("torrentApplySecurity"));
    ui->applySecurity->setEnabled(false);
    securityLayout->addWidget(ui->applySecurity, 0, Qt::AlignRight);
    securityScroll->setWidget(securityPanel);
    connect(ui->countrySearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < ui->countries->count(); ++i) {
            auto *item = ui->countries->item(i);
            item->setHidden(!item->text().contains(text.trimmed(), Qt::CaseInsensitive));
        }
    });
    connect(ui->encryption, &QComboBox::currentIndexChanged, this, &TorrentWindow::updateSecurityActions);
    connect(ui->countries, &QListWidget::itemChanged, this, &TorrentWindow::updateSecurityActions);
    connect(ui->blockUnknown, &QCheckBox::toggled, this, &TorrentWindow::updateSecurityActions);
    connect(ui->applySecurity, &QPushButton::clicked, this, [this] {
        emit securitySettingsRequested(static_cast<EncryptionMode>(ui->encryption->currentData().toInt()),
                                       checkedCountries(), ui->blockUnknown->isChecked());
    });
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    ui->sharing = new QLabel(tr("DC sharing follows Preferences unless excluded for a Torrent in the DC++ sharing menu. Only eligible completed, selected files are published after hash verification. Private Torrents remain excluded. Manual DC++ shares are unchanged."), central);
    ui->sharing->setObjectName(QStringLiteral("torrentSharingStatus"));
    ui->sharing->setWordWrap(true);
    layout->addWidget(ui->sharing);

    QSettings saved(layoutPath, QSettings::IniFormat);
    const bool restoredJobs = ui->jobs->header()->restoreState(saved.value(QStringLiteral("jobs/header")).toByteArray());
    // Migrate old window layouts once, retaining widths/order and later user choices.
    if (!restoredJobs || !saved.value(QStringLiteral("jobs/unifiedTransfersLayout"), false).toBool()) {
        ui->jobs->setColumnHidden(DownloadSpeed, true);
        ui->jobs->setColumnHidden(UploadSpeed, true);
    }
    ui->files->header()->restoreState(saved.value(QStringLiteral("files/header")).toByteArray());
    ui->peers->header()->restoreState(saved.value(QStringLiteral("peers/header")).toByteArray());
    ui->details->setCurrentIndex(std::clamp(saved.value(QStringLiteral("window/detailTab"), 0).toInt(), 0, 2));
    ui->restoreJobId = saved.value(QStringLiteral("jobs/selected")).toString();
    if (saved.contains(QStringLiteral("window/splitter")))
        splitter->restoreState(saved.value(QStringLiteral("window/splitter")).toByteArray());
    // Old splitter states can include a too-narrow handle width.
    splitter->setHandleWidth(9);
    splitter->handle(1)->installEventFilter(this);
    ui->layoutTimer = new QTimer(this);
    ui->layoutTimer->setSingleShot(true);
    ui->layoutTimer->setInterval(350);
    connect(ui->layoutTimer, &QTimer::timeout, this, &TorrentWindow::saveLayout);
    connect(splitter, &QSplitter::splitterMoved, this, &TorrentWindow::scheduleLayoutSave);
    for (auto *tree : {ui->jobs, ui->files, ui->peers}) {
        tree->header()->viewport()->installEventFilter(this);
        connect(tree->header(), &QHeaderView::sectionResized, this, &TorrentWindow::scheduleLayoutSave);
        connect(tree->header(), &QHeaderView::sectionMoved, this, &TorrentWindow::scheduleLayoutSave);
    }
    connect(ui->peers->header(), &QHeaderView::sortIndicatorChanged, this, &TorrentWindow::scheduleLayoutSave);
    connect(ui->details, &QTabWidget::currentChanged, this, &TorrentWindow::scheduleLayoutSave);

    connect(ui->addFile, &QAction::triggered, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, tr("Add Torrent file"), QString(), tr("Torrent files (*.torrent);;All files (*)"));
        if (!file.isEmpty())
            addSource(file);
    });
    connect(ui->addMagnet, &QAction::triggered, this, [this] {
        bool ok = false;
        const auto magnet = QInputDialog::getText(this, tr("Add Torrent magnet"), tr("BitTorrent magnet link:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !magnet.trimmed().isEmpty())
            addSource(magnet.trimmed());
    });
    connect(ui->pause, &QAction::triggered, this, [this] {
        const auto id = selectedId();
        // Actions can outlive their snapshot while the 100ms refresh is pending.
        if (engine && engine->settings().enabled) {
            for (const auto &job : engine->jobs()) {
                if (job.id == id && !job.paused && !job.stopped) {
                    engine->pause(id, true);
                    break;
                }
            }
        }
        updateActions();
    });
    connect(ui->stop, &QAction::triggered, this, [this] {
        const auto id = selectedId();
        if (engine) {
            for (const auto &job : engine->jobs()) {
                if (job.id == id && !job.stopped) {
                    engine->stop(id);
                    break;
                }
            }
        }
        updateActions();
    });
    connect(ui->resume, &QAction::triggered, this, [this] {
        const auto id = selectedId();
        if (engine && engine->settings().enabled) {
            for (const auto &job : engine->jobs()) {
                if (job.id == id && (job.paused || job.stopped)) {
                    engine->pause(id, false);
                    break;
                }
            }
        }
        updateActions();
    });
    connect(ui->remove, &QAction::triggered, this, [this] { removeSelected(false); });
    connect(ui->deleteFiles, &QAction::triggered, this, [this] { removeSelected(true); });
    connect(ui->recheck, &QAction::triggered, this, &TorrentWindow::recheckSelected);
    connect(ui->jobs, &QTreeWidget::itemSelectionChanged, this, [this] {
        selectionDirty = false;
        ui->restoreJobId.clear();
        if (engine) engine->selectObservedJob(selectedId());
        refreshFiles();
        refreshPeers();
        updateActions();
        scheduleLayoutSave();
    });
    connect(ui->files, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *, int column) {
        if (column == 0 || column == 2) {
            selectionDirty = true;
            updateActions();
        }
    });
    connect(ui->applyFiles, &QPushButton::clicked, this, &TorrentWindow::applyFileSelection);
    ui->refreshTimer = new QTimer(this);
    ui->refreshTimer->setSingleShot(true);
    ui->refreshTimer->setInterval(100);
    connect(ui->refreshTimer, &QTimer::timeout, this, &TorrentWindow::refresh);
    if (engine) {
        // Coalesce notifications; jobs()/files() are cached snapshots, not disk scans.
        connect(engine, &TorrentEngine::changed, this, [this] {
            if (!ui->refreshTimer->isActive())
                ui->refreshTimer->start();
        }, Qt::QueuedConnection);
        connect(engine, &TorrentEngine::error, this, &TorrentWindow::showError);
        connect(engine, &TorrentEngine::peersChanged, this, &TorrentWindow::refreshPeers);
        connect(engine, &QObject::destroyed, this, [this] { refresh(); });
    }
    refresh();
}

TorrentWindow::~TorrentWindow()
{
    saveLayout();
    if (engine) {
        disconnect(engine, nullptr, this, nullptr);
        if (engine->observedJob() == selectedId()) engine->selectObservedJob({});
    }
}

void TorrentWindow::reloadIcons()
{
    const auto icon = torrent_toolbar::icon();
    tabPixmap = icon.pixmap(QSize(16, 16), devicePixelRatioF());
    setWindowIcon(icon);
    const struct { const char *action; const char *stem; QStyle::StandardPixmap fallback; } icons[] = {
        {"torrentAddFile", "openlist", QStyle::SP_DialogOpenButton},
        {"torrentAddMagnet", "magnet", QStyle::SP_FileLinkIcon},
        {"torrentCreate", "document-new", QStyle::SP_FileIcon},
        {"torrentPause", "media-pause", QStyle::SP_MediaPause},
        {"torrentResume", "media-play", QStyle::SP_MediaPlay},
        {"torrentStop", "media-stop", QStyle::SP_MediaStop},
        {"torrentRecheck", "reload", QStyle::SP_BrowserReload},
        {"torrentRemove", "dialog-close", QStyle::SP_DialogCloseButton},
        {"torrentDelete", "edit-delete", QStyle::SP_TrashIcon},
        {"torrentSettings", "configure", QStyle::SP_FileDialogDetailedView}
    };
    for (const auto &entry : icons) {
        if (auto *action = findChild<QAction*>(QString::fromLatin1(entry.action)))
            action->setIcon(app_icon_theme::icon(app_icon_theme::activePath(),
                QString::fromLatin1(entry.stem), qApp->palette(), style()->standardIcon(entry.fallback)));
    }
}

void TorrentWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        reloadIcons();
}

bool TorrentWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        ui->dragTarget = watched;
        ui->layoutTimer->stop();
    } else if (ui->dragTarget == watched &&
               ((event->type() == QEvent::MouseButtonRelease &&
                 static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) ||
                event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)) {
        ui->dragTarget.clear();
        // Let the header/splitter finish handling the release before reordering rows.
        QTimer::singleShot(0, this, [this] {
            if (ui->dragTarget) return;
            if (ui->pendingRefresh) {
                ui->pendingRefresh = false;
                ui->pendingPeers = false;
                ui->pendingPeerSnapshot.clear();
                refresh();
            } else if (ui->pendingPeers) {
                ui->pendingPeers = false;
                const auto id = ui->pendingPeerJobId;
                const auto peers = std::move(ui->pendingPeerSnapshot);
                renderPeers(id, peers);
            }
            scheduleLayoutSave();
        });
    }
    return QWidget::eventFilter(watched, event);
}

QStringList TorrentWindow::checkedCountries() const
{
    QStringList countries;
    for (int i = 0; i < ui->countries->count(); ++i) {
        const auto *item = ui->countries->item(i);
        if (item->checkState() == Qt::Checked)
            countries.append(item->data(Qt::UserRole).toString());
    }
    countries.sort();
    return countries;
}

void TorrentWindow::updateSecurityActions()
{
    ui->applySecurity->setEnabled(
        ui->encryption->currentData().toInt() != static_cast<int>(ui->savedEncryption) ||
        checkedCountries() != ui->savedCountries || ui->blockUnknown->isChecked() != ui->savedBlockUnknown);
}

void TorrentWindow::setSecuritySettings(const Settings &settings)
{
    const auto countries = normalizedCountries(settings.blockedCountries);
    // Repeated snapshots with unrelated settings changes must not erase edits.
    if (ui->securityInitialized && ui->savedEncryption == settings.encryptionMode &&
        ui->savedCountries == countries && ui->savedBlockUnknown == settings.blockUnknownClients)
        return;
    ui->securityInitialized = true;
    ui->savedEncryption = settings.encryptionMode;
    ui->savedCountries = countries;
    ui->savedBlockUnknown = settings.blockUnknownClients;
    const QSignalBlocker encryptionBlocked(ui->encryption), countriesBlocked(ui->countries), unknownBlocked(ui->blockUnknown);
    ui->encryption->setCurrentIndex(ui->encryption->findData(static_cast<int>(settings.encryptionMode)));
    ui->blockUnknown->setChecked(settings.blockUnknownClients);
    for (int i = 0; i < ui->countries->count(); ++i) {
        auto *item = ui->countries->item(i);
        item->setCheckState(countries.contains(item->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    updateSecurityActions();
}

void TorrentWindow::selectJob(const QString &id)
{
    if (id.isEmpty())
        return;
    refresh();
    for (int i = 0; i < ui->jobs->topLevelItemCount(); ++i) {
        auto *item = ui->jobs->topLevelItem(i);
        if (item->data(Name, Qt::UserRole).toString() == id) {
            if (ui->jobs->currentItem() != item || !item->isSelected() || ui->jobs->selectedItems().size() != 1)
                ui->jobs->setCurrentItem(item, Name, QItemSelectionModel::ClearAndSelect);
            ui->jobs->scrollToItem(item);
            ui->jobs->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void TorrentWindow::setManuallySharedDirectories(const QStringList &directories)
{
    manuallySharedDirectories = directories;
}

QString TorrentWindow::selectedId() const
{
    const auto items = ui->jobs->selectedItems();
    return items.size() == 1 ? items.first()->data(0, Qt::UserRole).toString() : QString();
}

QStringList TorrentWindow::selectedIds() const
{
    QStringList ids;
    for (const auto *item : ui->jobs->selectedItems())
        ids.append(item->data(0, Qt::UserRole).toString());
    return ids;
}

void TorrentWindow::showError(const QString &message)
{
    ui->errorVisible = !message.isEmpty();
    ui->notice->setText(message);
    if (!ui->errorVisible)
        refresh();
}

void TorrentWindow::setSharingStatus(const QString &id, const QString &status)
{
    if (id.isEmpty())
        return;
    const auto text = status.isEmpty() ? tr("Not shared") : status;
    ui->sharingStatuses.insert(id, text);
    for (int i = 0; i < ui->jobs->topLevelItemCount(); ++i) {
        auto *item = ui->jobs->topLevelItem(i);
        if (item->data(Name, Qt::UserRole).toString() == id) {
            item->setText(Sharing, text);
            item->setToolTip(Sharing, text);
            break;
        }
    }
}

void TorrentWindow::refresh()
{
    if (ui->dragTarget) {
        ui->pendingRefresh = true;
        return;
    }
    auto selected = selectedIds();
    if (selected.isEmpty() && !ui->restoreJobId.isEmpty()) selected.append(ui->restoreJobId);
    const int scroll = ui->jobs->verticalScrollBar()->value();
    const QSignalBlocker blocked(ui->jobs);
    const auto jobs = engine ? engine->jobs() : QList<Job>{};
    // Update existing rows instead of replacing the model/header on each alert.
    QHash<QString, QTreeWidgetItem *> existing;
    for (int i = 0; i < ui->jobs->topLevelItemCount(); ++i) {
        auto *item = ui->jobs->topLevelItem(i);
        existing.insert(item->data(0, Qt::UserRole).toString(), item);
    }
    for (const auto &job : jobs) {
        auto *item = existing.take(job.id);
        if (!item) {
            item = new QTreeWidgetItem(ui->jobs);
            item->setData(0, Qt::UserRole, job.id);
            auto *progress = new QProgressBar(ui->jobs);
            progress->setRange(0, 1000);
            progress->setFormat(tr("%p%"));
            ui->jobs->setItemWidget(item, Progress, progress);
        }
        item->setText(Name, job.name);
        item->setText(Status, !job.error.isEmpty() ? tr("Error: %1").arg(job.error) :
                      (!job.state.isEmpty() ? job.state : (job.paused ? tr("Paused") : tr("Waiting"))));
        item->setToolTip(Status, item->text(Status));
        auto *progress = qobject_cast<QProgressBar *>(ui->jobs->itemWidget(item, Progress));
        const QColor color = job.stopped ? QColor(150, 150, 150)
            : job.paused ? QColor(255, 193, 120)
            : job.complete ? QColor(64, 145, 225) : QColor(55, 166, 80);
        for (int column = 0; column < Count; ++column)
            item->setForeground(column, color);
        if (progress) {
            auto palette = ui->jobs->palette();
            palette.setColor(QPalette::Highlight, color);
            progress->setPalette(palette);
            progress->setValue(std::isfinite(job.progress) ? qRound(std::clamp(job.progress, 0.0, 1.0) * 1000) : 0);
        }
        item->setText(Size, sizeText(job.size));
        item->setText(DownloadSpeed, tr("%1/s").arg(sizeText(job.downloadRate)));
        item->setText(UploadSpeed, tr("%1/s").arg(sizeText(job.uploadRate)));
        item->setText(Peers, QLocale().toString(job.peers));
        item->setText(Seeds, QLocale().toString(job.seeds));
        item->setText(Sharing, job.dcShareExcluded ? tr("Not shared: excluded for this Torrent") :
            ui->sharingStatuses.value(job.id, tr("Not shared")));
        item->setToolTip(Sharing, item->text(Sharing));
        item->setData(Status, Qt::UserRole, job.paused);
        if (selected.contains(job.id)) {
            item->setSelected(true);
            if (selected.size() == 1) ui->jobs->setCurrentItem(item, Name, QItemSelectionModel::NoUpdate);
            ui->restoreJobId.clear();
        }
    }
    for (auto *item : existing) {
        ui->sharingStatuses.remove(item->data(Name, Qt::UserRole).toString());
        delete item;
    }
    ui->jobs->verticalScrollBar()->setValue(scroll);
    if (!ui->errorVisible)
        ui->notice->setText(engine && !engine->settings().enabled ? tr("Torrent support is disabled in Preferences.") : tr("Manage Torrent jobs and files here. Live DC++ and Torrent traffic appears in the shared Transfers section below."));
    if (engine) engine->selectObservedJob(selectedId());
    refreshFiles();
    refreshPeers();
    updateActions();
}

void TorrentWindow::refreshPeers()
{
    const auto id = selectedId();
    const auto peers = engine ? engine->peers(id) : QList<Peer>{};
    renderPeers(id, peers);
}

void TorrentWindow::renderPeers(const QString &id, const QList<Peer> &peers)
{
    if (ui->dragTarget) {
        ui->pendingPeers = true;
        ui->pendingPeerJobId = id;
        ui->pendingPeerSnapshot = peers;
        return;
    }
    const QSignalBlocker blocked(ui->peers);
    const QSignalBlocker headerBlocked(ui->peers->header());
    ui->peers->setSortingEnabled(false);
    if (ui->peerJobId != id) {
        ui->peers->clear();
        ui->peerJobId = id;
    }
    const int scroll = ui->peers->verticalScrollBar()->value();
    QHash<QString, QTreeWidgetItem *> existing;
    for (int i = 0; i < ui->peers->topLevelItemCount(); ++i) {
        auto *item = ui->peers->topLevelItem(i);
        existing.insert(item->data(PeerAddress, Qt::UserRole).toString(), item);
    }
    for (const auto &peer : peers) {
        auto *item = existing.take(peer.id);
        if (!item) {
            item = new PeerItem(ui->peers);
            item->setData(PeerAddress, Qt::UserRole, peer.id);
        }
        const auto code = peer.countryCode.trimmed().toUpper();
        item->setText(PeerCountry, country_names::fromCode(code));
        const auto flagKey = code + ':' + QString::number(devicePixelRatioF());
        if (!ui->countryFlags.contains(flagKey))
            ui->countryFlags.insert(flagKey, countryFlag(code, devicePixelRatioF()));
        item->setIcon(PeerCountry, ui->countryFlags.value(flagKey));
        const auto address = (peer.ip.contains(':') ? '[' + peer.ip + ']' : peer.ip) + ':' + QString::number(peer.port);
        item->setText(PeerAddress, address);
        item->setData(PeerAddress, PeerSortRole, endpointSortKey(peer));
        // QTreeWidget's standard delegate paints display strings as plain text.
        // Do not mirror remote client strings into rich-text tooltips or labels.
        item->setText(PeerClient, peer.client);
        const double progress = std::isfinite(peer.progress) ? std::clamp(peer.progress, 0.0, 1.0) : 0;
        item->setText(PeerProgress, QLocale().toString(progress * 100, 'f', 1) + '%');
        item->setData(PeerProgress, PeerSortRole, progress);
        item->setText(PeerDownload, tr("%1/s").arg(sizeText(peer.downloadRate)));
        item->setData(PeerDownload, PeerSortRole, peer.downloadRate);
        item->setText(PeerUpload, tr("%1/s").arg(sizeText(peer.uploadRate)));
        item->setData(PeerUpload, PeerSortRole, peer.uploadRate);
        item->setText(PeerState, QStringList{peer.state, peer.transport, tr("Encryption: %1").arg(peer.encryption)}.join(QStringLiteral(" / ")));
    }
    for (auto *item : existing) delete item;
    ui->peers->setSortingEnabled(true);
    ui->peers->verticalScrollBar()->setValue(scroll);
    ui->peerNotice->setText(id.isEmpty() ? tr("Select a Torrent to review its peers.") :
        peers.isEmpty() ? tr("No connected peers. Details refresh in the background for the selected Torrent.") :
        tr("Connected peers for the selected Torrent. Payload rates update about once per second; country is blank when unavailable in the local GeoIP data."));
}

void TorrentWindow::refreshFiles()
{
    const auto id = selectedId();
    if (selectionDirty && id == fileJobId)
        return;
    const auto files = engine && !id.isEmpty() ? engine->files(id) : QList<FileEntry>{};
    if (ui->filesInitialized && id == fileJobId &&
        files.size() == ui->displayedFiles.size() &&
        std::equal(files.begin(), files.end(), ui->displayedFiles.begin(), [](const FileEntry &a, const FileEntry &b) {
            return a.index == b.index && a.path == b.path && a.size == b.size && a.wanted == b.wanted && a.priority == b.priority;
        }))
        return;
    ui->displayedFiles = files;
    ui->filesInitialized = true;
    fileJobId = id;
    selectionDirty = false;
    const QSignalBlocker blocked(ui->files);
    const int scroll = ui->files->verticalScrollBar()->value();
    ui->files->clear();
    for (const auto &file : files) {
        auto *item = new QTreeWidgetItem(ui->files);
        item->setText(0, file.path);
        item->setIcon(0, ui->fileIcons.iconForFile(file.path, ui->files->style()->standardIcon(QStyle::SP_FileIcon)));
        item->setText(1, sizeText(file.size));
        item->setData(2, FilePriorityRole, file.priority);
        item->setText(2, file.priority == 1 ? tr("Low") : file.priority == 7 ? tr("High") : tr("Normal"));
        item->setToolTip(2, tr("Choose Low, Normal or High. Unchecked files are skipped regardless of priority."));
        item->setData(0, Qt::UserRole, file.index);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(0, file.wanted ? Qt::Checked : Qt::Unchecked);
    }
    ui->files->verticalScrollBar()->setValue(scroll);
    ui->fileNotice->setText(id.isEmpty() ? tr("Select a Torrent to review its files.") :
                           files.isEmpty() ? tr("File selection is available when metadata arrives. A paused magnet may need to be resumed to fetch metadata.") :
                           tr("Unchecked files are not wanted. Apply an empty selection to download none. Changing selection retracts managed DC publication; it does not delete existing data."));
}

void TorrentWindow::updateActions()
{
    const bool available = engine && engine->settings().enabled;
    const auto ids = selectedIds();
    QList<Job> selectedJobs;
    if (engine) {
        for (const auto &job : engine->jobs())
            if (ids.contains(job.id)) selectedJobs.append(job);
    }
    const bool selected = !selectedJobs.isEmpty();
    const bool single = selectedJobs.size() == 1 && ids.size() == 1;
    ui->dcSharing->setEnabled(selected);
    ui->dcShareFollowGlobal->setEnabled(selected);
    ui->dcShareExclude->setEnabled(selected);
    ui->dcShareFollowGlobal->setChecked(selected && std::all_of(selectedJobs.cbegin(), selectedJobs.cend(),
        [](const Job &job) { return !job.dcShareExcluded; }));
    ui->dcShareExclude->setChecked(selected && std::all_of(selectedJobs.cbegin(), selectedJobs.cend(),
        [](const Job &job) { return job.dcShareExcluded; }));
    ui->addFile->setEnabled(available);
    ui->addMagnet->setEnabled(available);
    torrent_action_menu::update({ui->pause, ui->resume, ui->stop}, single ? selectedJobs : QList<Job>{}, available);
    ui->shareDc->setEnabled(selected);
    ui->shareTorrent->setEnabled(selected);
    ui->remove->setEnabled(single);
    ui->deleteFiles->setEnabled(single);
    ui->recheck->setEnabled(single && available);
    ui->files->setEnabled(single);
    ui->peers->setEnabled(single);
    ui->applyFiles->setEnabled(single && selectionDirty && !fileJobId.isEmpty());
}

void TorrentWindow::createTorrent()
{
    if (auto *existing = findChild<TorrentCreateDialog *>()) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dialog = new TorrentCreateDialog(engine, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void TorrentWindow::addSource(const QString &source)
{
    if (source.trimmed().isEmpty())
        return;
    if (!engine || !engine->settings().enabled) {
        showError(tr("Enable Torrent support in Preferences before adding a Torrent."));
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("torrentAddDialog"));
    dialog.setWindowTitle(tr("Add Torrent"));
    auto *form = new QFormLayout(&dialog);
    auto *destination = new QLineEdit(engine->settings().downloadPath, &dialog);
    destination->setObjectName(QStringLiteral("torrentDestination"));
    auto *pathRow = new QWidget(&dialog);
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton(tr("Browse..."), pathRow);
    pathLayout->addWidget(destination);
    pathLayout->addWidget(browse);
    form->addRow(tr("Download directory:"), pathRow);
    connect(browse, &QPushButton::clicked, &dialog, [this, destination] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Select Torrent directory"), destination->text());
        if (!path.isEmpty())
            destination->setText(path);
    });
    auto *notice = new QLabel(tr("All files are selected initially. Start paused to review a .torrent file's checklist before payload download. A paused magnet has no file list until metadata is fetched: resuming it requires network access and can start downloading all files. Apply your file selection before resuming whenever metadata is available."), &dialog);
    notice->setWordWrap(true);
    form->addRow(notice);
    auto *pause = new QCheckBox(tr("Start paused (review files before downloading)"), &dialog);
    pause->setObjectName(QStringLiteral("torrentStartPaused"));
    pause->setChecked(true);
    form->addRow(pause);
    auto *shareWarning = new QLabel(&dialog);
    shareWarning->setTextFormat(Qt::PlainText);
    shareWarning->setWordWrap(true);
    form->addRow(shareWarning);
    auto *acknowledge = new QCheckBox(tr("I understand this destination is already shared in DC, including partial files"), &dialog);
    form->addRow(acknowledge);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    auto updateDestination = [this, destination, shareWarning, acknowledge, buttons] {
        const auto path = destination->text().trimmed();
        const auto completedPath = engine ? engine->settings().completedPath : QString();
        bool shared = false;
        for (const auto &directory : manuallySharedDirectories) {
            if ((!path.isEmpty() && insideDirectory(path, directory)) ||
                (!completedPath.isEmpty() && insideDirectory(completedPath, directory))) {
                shared = true;
                break;
            }
        }
        shareWarning->setText(shared ? tr("Warning: the download or completed directory lies in an existing manual DC share. Disabling Torrent sharing cannot protect files in a manual share.") :
                              tr("Use a directory outside all manual DC shares. Torrent-managed sharing publishes only eligible completed files when enabled."));
        acknowledge->setVisible(shared);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!path.isEmpty() && QDir::isAbsolutePath(path) && (!shared || acknowledge->isChecked()));
    };
    connect(destination, &QLineEdit::textChanged, &dialog, [acknowledge, updateDestination] {
        acknowledge->setChecked(false);
        updateDestination();
    });
    connect(acknowledge, &QCheckBox::toggled, &dialog, updateDestination);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateDestination();
    dialog.resize(620, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted || !engine)
        return;
    ui->errorVisible = false;
    ui->notice->clear();
    const auto id = engine->add(source, destination->text().trimmed(), {}, pause->isChecked());
    if (id.isEmpty()) {
        if (ui->notice->text().isEmpty())
            showError(tr("The Torrent was rejected. Check the source, destination and proxy settings."));
        return;
    }
    refresh();
    for (int i = 0; i < ui->jobs->topLevelItemCount(); ++i) {
        auto *item = ui->jobs->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == id) {
            ui->jobs->setCurrentItem(item);
            break;
        }
    }
}

void TorrentWindow::removeSelected(bool deleteFiles)
{
    const auto id = selectedId();
    if (!engine || id.isEmpty())
        return;
    const auto title = deleteFiles ? tr("Delete Torrent data?") : tr("Remove Torrent?");
    const auto message = deleteFiles ? tr("Permanently delete this Torrent's downloaded data and remove the job? This cannot be undone. Managed DC publication must be retracted first; manual DC shares remain unchanged.") :
        tr("Remove this Torrent job and keep all downloaded files? Managed DC publication must be retracted first; manual DC shares remain unchanged.");
    if (QMessageBox::warning(this, title, message, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes || !engine)
        return;
    emit shareInvalidationRequested(id);
    if (engine)
        engine->remove(id, deleteFiles);
}

void TorrentWindow::recheckSelected()
{
    const auto id = selectedId();
    if (!engine || id.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Recheck Torrent?"), tr("Recheck all downloaded pieces? Managed DC publication must be retracted before verification and remain unavailable until the files are verified again. No files are deleted."), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes || !engine)
        return;
    emit shareInvalidationRequested(id);
    if (engine)
        engine->recheck(id);
}

void TorrentWindow::applyFileSelection()
{
    const auto id = selectedId();
    if (!engine || id.isEmpty() || id != fileJobId || !selectionDirty)
        return;
    QList<int> wanted;
    QMap<int, int> priorities;
    for (int i = 0; i < ui->files->topLevelItemCount(); ++i) {
        const auto *item = ui->files->topLevelItem(i);
        priorities.insert(item->data(0, Qt::UserRole).toInt(), item->data(2, FilePriorityRole).toInt());
        if (item->checkState(0) == Qt::Checked)
            wanted.append(item->data(0, Qt::UserRole).toInt());
    }
    if (wanted.isEmpty() && QMessageBox::question(this, tr("Download no files?"), tr("Apply an empty file selection? No files will be wanted. Existing downloaded data is kept."), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    emit shareInvalidationRequested(id);
    if (engine)
        engine->setWantedFiles(id, wanted, priorities);
    selectionDirty = false;
    // Checkbox edits no longer match displayedFiles. A coalesced refresh may
    // skip the applied snapshot, so even an unchanged cached snapshot must render.
    ui->filesInitialized = false;
    updateActions();
}

void TorrentWindow::scheduleLayoutSave()
{
    if (!ui->dragTarget)
        ui->layoutTimer->start();
}

void TorrentWindow::saveLayout()
{
    ui->layoutTimer->stop();
    QSettings saved(layoutPath, QSettings::IniFormat);
    saved.setValue(QStringLiteral("jobs/header"), ui->jobs->header()->saveState());
    saved.setValue(QStringLiteral("jobs/unifiedTransfersLayout"), true);
    saved.setValue(QStringLiteral("files/header"), ui->files->header()->saveState());
    saved.setValue(QStringLiteral("peers/header"), ui->peers->header()->saveState());
    saved.setValue(QStringLiteral("jobs/selected"), selectedId().isEmpty() ? ui->restoreJobId : selectedId());
    saved.setValue(QStringLiteral("window/detailTab"), ui->details->currentIndex());
    saved.setValue(QStringLiteral("window/splitter"), ui->splitter->saveState());
}

void TorrentWindow::closeEvent(QCloseEvent *event)
{
    saveLayout();
    // The arena manager owns tab visibility; this view never owns engine lifetime.
    QWidget::closeEvent(event);
}
#endif
