#pragma once

#include <QMetaType>
#include <QWidget>

#include "ArenaWidget.h"
#include "QtContextAware.h"
#include "dcpp/LogManager.h"
#include "ui_UILiveLog.h"

Q_DECLARE_METATYPE(dcpp::LogEntry)

class QAction;
class QMenu;
class LiveLogModel;

class LiveLog :
    public QWidget,
    private Ui::UILiveLog,
    public ArenaWidget,
    private dcpp::LogManagerListener,
    public QtContextAware
{
    Q_OBJECT
    Q_INTERFACES(ArenaWidget)

    friend class QtContext;

public:
    explicit LiveLog(dcpp::DCContext& ctx, QWidget* parent = nullptr);
    ~LiveLog() override;

    QWidget* getWidget() override { return this; }
    QString getArenaTitle() override { return tr("Live Log"); }
    QString getArenaShortTitle() override { return getArenaTitle(); }
    QMenu* getMenu() override { return nullptr; }
    const QPixmap& getPixmap() override;
    ArenaWidget::Role role() const override { return ArenaWidget::LiveLog; }
    void requestClear() override;

Q_SIGNALS:
    void coreEntryAdded(const dcpp::LogEntry& entry);

private Q_SLOTS:
    void slotEntryAdded(const dcpp::LogEntry& entry);
    void slotPauseChanged(bool paused);
    void slotClear();
    void slotCategoryChanged();
    void slotSelectAllCategories();
    void slotSelectNoCategories();
    void slotSettingsChanged(const QString& key, const QString& value);

private:
    void createCategoryMenu();
    void updateCategoryTexts();
    void applyCategoryMask(quint32 mask, bool persist);
    bool isAtBottom() const;
    void scrollToBottomIfNeeded(bool wasAtBottom);
    void on(dcpp::LogManagerListener::EntryAdded, const dcpp::LogEntry& entry) noexcept override;

    LiveLogModel* model;
    QMenu* categoryMenu;
    QAction* allCategoriesAction;
    QAction* noCategoriesAction;
    QList<QAction*> categoryActions;
};
