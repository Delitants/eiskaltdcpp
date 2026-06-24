#include "LiveLog.h"

#include "LiveLogModel.h"
#include "QtContext.h"
#include "WulforSettings.h"
#include "WulforUtil.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QScrollBar>
#include <QSignalBlocker>

LiveLog::LiveLog(dcpp::DCContext& ctx, QWidget* parent)
    : QWidget(parent)
    , QtContextAware(ctx)
    , model(new LiveLogModel(this))
    , categoryMenu(new QMenu(this))
    , allCategoriesAction(nullptr)
    , noCategoriesAction(nullptr)
{
    setupUi(this);
    qRegisterMetaType<dcpp::LogEntry>("dcpp::LogEntry");

    tableView->setModel(model);
    tableView->verticalHeader()->hide();
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->setColumnWidth(LiveLogModel::Time, 150);
    tableView->setColumnWidth(LiveLogModel::Category, 150);

    createCategoryMenu();
    toolButton_CATEGORIES->setMenu(categoryMenu);

    const auto mask = static_cast<quint32>(qtCtx()->settings()->getInt(
        WS_LIVE_LOG_CATEGORIES, static_cast<int>(LiveLogModel::allCategoryMask())));
    applyCategoryMask(mask, false);

    connect(this, &LiveLog::coreEntryAdded, this, &LiveLog::slotEntryAdded,
        Qt::QueuedConnection);
    connect(toolButton_PAUSE, &QToolButton::toggled, this, &LiveLog::slotPauseChanged);
    connect(pushButton_CLEAR, &QPushButton::clicked, this, &LiveLog::slotClear);
    connect(qtCtx()->settings(), &WulforSettings::strValueChanged,
        this, &LiveLog::slotSettingsChanged);

    auto* logs = dcCtx().getLogManager();
    logs->addListener(this);
    model->replaceEntries(logs->getLiveEntries());

    ArenaWidget::setState(ArenaWidget::Flags(
        ArenaWidget::state() | ArenaWidget::Singleton | ArenaWidget::Hidden));
}

LiveLog::~LiveLog()
{
    dcCtx().getLogManager()->removeListener(this);
}

const QPixmap& LiveLog::getPixmap()
{
    return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiOPEN_LOG_FILE);
}

void LiveLog::requestClear()
{
    slotClear();
}

void LiveLog::createCategoryMenu()
{
    allCategoriesAction = categoryMenu->addAction(QString());
    noCategoriesAction = categoryMenu->addAction(QString());
    categoryMenu->addSeparator();

    for(size_t index = 0; index < dcpp::LogManager::LAST; ++index) {
        auto* action = categoryMenu->addAction(QString());
        action->setCheckable(true);
        action->setData(static_cast<int>(index));
        categoryActions.push_back(action);
        connect(action, &QAction::toggled, this, &LiveLog::slotCategoryChanged);
    }

    connect(allCategoriesAction, &QAction::triggered,
        this, &LiveLog::slotSelectAllCategories);
    connect(noCategoriesAction, &QAction::triggered,
        this, &LiveLog::slotSelectNoCategories);
    updateCategoryTexts();
}

void LiveLog::updateCategoryTexts()
{
    allCategoriesAction->setText(tr("All categories"));
    noCategoriesAction->setText(tr("No categories"));

    for(int index = 0; index < categoryActions.size(); ++index) {
        categoryActions[index]->setText(LiveLogModel::categoryName(
            static_cast<dcpp::LogManager::Area>(index)));
    }
}

void LiveLog::applyCategoryMask(quint32 mask, bool persist)
{
    mask &= LiveLogModel::allCategoryMask();

    for(int index = 0; index < categoryActions.size(); ++index) {
        const QSignalBlocker blocker(categoryActions[index]);
        categoryActions[index]->setChecked(
            (mask & LiveLogModel::categoryBit(
                static_cast<dcpp::LogManager::Area>(index))) != 0);
    }

    model->setCategoryMask(mask);
    if(persist) {
        qtCtx()->settings()->setInt(WS_LIVE_LOG_CATEGORIES, static_cast<int>(mask));
    }
}

bool LiveLog::isAtBottom() const
{
    const auto* scrollBar = tableView->verticalScrollBar();
    return scrollBar->value() >= scrollBar->maximum() - 1;
}

void LiveLog::scrollToBottomIfNeeded(bool wasAtBottom)
{
    if(wasAtBottom && checkBox_AUTOSCROLL->isChecked()) {
        tableView->scrollToBottom();
    }
}

void LiveLog::slotEntryAdded(const dcpp::LogEntry& entry)
{
    if(toolButton_PAUSE->isChecked()) {
        return;
    }

    const bool wasAtBottom = isAtBottom();
    model->appendEntry(entry);
    scrollToBottomIfNeeded(wasAtBottom);
}

void LiveLog::slotPauseChanged(bool paused)
{
    if(paused) {
        return;
    }

    const bool wasAtBottom = isAtBottom();
    model->appendNewEntries(dcCtx().getLogManager()->getLiveEntries());
    scrollToBottomIfNeeded(wasAtBottom);
}

void LiveLog::slotClear()
{
    const auto clearedThrough = dcCtx().getLogManager()->clearLiveEntries();
    model->clearThroughSequence(clearedThrough);
}

void LiveLog::slotCategoryChanged()
{
    quint32 mask = 0;
    for(int index = 0; index < categoryActions.size(); ++index) {
        if(categoryActions[index]->isChecked()) {
            mask |= LiveLogModel::categoryBit(
                static_cast<dcpp::LogManager::Area>(index));
        }
    }
    applyCategoryMask(mask, true);
}

void LiveLog::slotSelectAllCategories()
{
    applyCategoryMask(LiveLogModel::allCategoryMask(), true);
}

void LiveLog::slotSelectNoCategories()
{
    applyCategoryMask(0, true);
}

void LiveLog::slotSettingsChanged(const QString& key, const QString&)
{
    if(key == WS_TRANSLATION_FILE) {
        retranslateUi(this);
        updateCategoryTexts();
        model->refreshTranslations();
    }
}

void LiveLog::on(dcpp::LogManagerListener::EntryAdded,
    const dcpp::LogEntry& entry) noexcept
{
    emit coreEntryAdded(entry);
}
