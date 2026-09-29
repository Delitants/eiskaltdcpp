/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#include "TabFrame.h"
#include "ArenaTabInfo.h"
#include "QtContextAware.h"
#include "QtContext.h"

#include "FlowLayout.h"
#include "TabButton.h"
#include "WulforUtil.h"
#include "ArenaWidgetManager.h"
#include "DebugHelper.h"
#include "GlobalTimer.h"

#include <QtWidgets>

#include <QPushButton>
#include <QWheelEvent>

TabFrame::TabFrame(QWidget *parent) :
    QFrame(parent)
{
    DEBUG_BLOCK
    
    setAcceptDrops(true);

    tabContents = new QWidget(this);
    fr_layout = new FlowLayout(tabContents, 0, 2, 2);
    fr_layout->setContentsMargins(0, 0, 0, 0);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("tabScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidget(tabContents);
    scrollArea->viewport()->installEventFilter(this);
    layout->addWidget(scrollArea, 1);
    auto *controls = new QVBoxLayout();
    controls->setSpacing(0);
    allTabs = new tab_navigation::AllTabsMenu(this);
    controls->addWidget(tab_navigation::makeAllTabsButton(allTabs, this));
    previousRow = tab_navigation::makeScrollButton(Qt::UpArrow, this);
    previousRow->setObjectName(QStringLiteral("previousTabRow"));
    previousRow->setArrowType(Qt::UpArrow);
    previousRow->setToolTip(tr("Previous tab row"));
    nextRow = tab_navigation::makeScrollButton(Qt::DownArrow, this);
    nextRow->setObjectName(QStringLiteral("nextTabRow"));
    nextRow->setArrowType(Qt::DownArrow);
    nextRow->setToolTip(tr("Next tab row"));
    for (auto *button : {previousRow, nextRow}) {
        button->setAccessibleName(button->toolTip());
        button->setFixedSize(30, 24);
        button->setAutoRaise(true);
        button->hide();
        controls->addWidget(button);
    }
    controls->addStretch();
    layout->addLayout(controls);
    connect(previousRow, &QToolButton::clicked, this, [this]() {
        auto *bar = scrollArea->verticalScrollBar();
        bar->setValue(bar->value() - bar->singleStep());
    });
    connect(nextRow, &QToolButton::clicked, this, [this]() {
        auto *bar = scrollArea->verticalScrollBar();
        bar->setValue(bar->value() + bar->singleStep());
    });
    const auto updateArrows = [this]() {
        auto *bar = scrollArea->verticalScrollBar();
        previousRow->setEnabled(bar->value() > bar->minimum());
        nextRow->setEnabled(bar->value() < bar->maximum());
    };
    connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, updateArrows);
    connect(scrollArea->verticalScrollBar(), &QScrollBar::rangeChanged, this, updateArrows);
    connect(allTabs, &QMenu::aboutToShow, this, &TabFrame::refreshTabList);
    connect(allTabs, &tab_navigation::AllTabsMenu::selected, this, [this](quint64 id) {
        if (auto *widget = registry.resolve(id)) {
            if (awgt_map.contains(widget))
                qtCtx()->arenaWidgetManager()->activate(widget);
        }
    });
    setMinimumHeight(30);
    setMaximumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    shortcuts << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_1)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_2)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_3)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_4)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_5)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_6)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_7)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_8)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_9)), this))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_0)), this));

    for (const auto &s : shortcuts){
        s->setContext(Qt::ApplicationShortcut);

        connect(s, &QShortcut::activated, this, &TabFrame::slotShorcuts);
    }

    connect(qtCtx()->globalTimer(), &GlobalTimer::second, this, &TabFrame::redraw);
}


TabFrame::~TabFrame(){
    DEBUG_BLOCK

    for (const auto &key : tbtn_map.keys()){
        TabButton *btn = const_cast<TabButton*>(key);

        btn->deleteLater();
    }
}

void TabFrame::resizeEvent(QResizeEvent *e){
    QFrame::resizeEvent(e);
    scheduleLayout();
}

bool TabFrame::eventFilter(QObject *obj, QEvent *e){
    if (obj == scrollArea->viewport() && e->type() == QEvent::Resize)
        scheduleLayout();
    TabButton *btn = qobject_cast<TabButton*>(obj);
    if (btn && e->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent*>(e);
        if (wheel->phase() == Qt::ScrollBegin) {
            tabWheelRemainder = 0;
            rowWheelRemainder = 0;
        }
        auto *bar = scrollArea->verticalScrollBar();
        if (bar->maximum() > bar->minimum()) {
            tabWheelRemainder = 0;
            if (wheel->pixelDelta().y()) {
                rowWheelRemainder = 0;
                bar->setValue(bar->value() - wheel->pixelDelta().y());
            } else if (wheel->angleDelta().y()) {
                rowWheelRemainder += wheel->angleDelta().y() * bar->singleStep() * QApplication::wheelScrollLines();
                bar->setValue(bar->value() - rowWheelRemainder / 120);
                rowWheelRemainder %= 120;
            } else {
                return QFrame::eventFilter(obj, e);
            }
        } else {
            rowWheelRemainder = 0;
            if (!wheel->pixelDelta().isNull() || !wheel->angleDelta().y())
                return QFrame::eventFilter(obj, e);
            tabWheelRemainder += wheel->angleDelta().y();
            while (qAbs(tabWheelRemainder) >= 120) {
                const bool forward = tabWheelRemainder > 0;
                tabWheelRemainder += forward ? -120 : 120;
                if (forward) nextTab(); else prevTab();
            }
        }
        wheel->accept();
        return true;
    }

    return QFrame::eventFilter(obj, e);
}

QSize TabFrame::sizeHint() const {
    return QSize(320, minimumHeight());
}

QSize TabFrame::minimumSizeHint() const{
    return QSize(112, minimumHeight());
}

void TabFrame::removeWidget(ArenaWidget *awgt){
    DEBUG_BLOCK
    
    if (!awgt_map.contains(awgt))
        return;

    TabButton *btn = const_cast<TabButton*>(awgt_map.value(awgt));
    const bool wasActive = btn->isChecked();

    fr_layout->removeWidget(btn);
    tbtn_map.remove(btn);
    awgt_map.remove(awgt);
    registry.remove(awgt);

    btn->hide();
    btn->deleteLater();

    historyPurge(awgt);
    if (wasActive)
        historyPop();
    
     if (awgt->toolButton())
        awgt->toolButton()->setChecked(false);
    refreshTabList();
    scheduleLayout();
}

void TabFrame::insertWidget(ArenaWidget *awgt){
    DEBUG_BLOCK
    
    if (awgt_map.contains(awgt) || (awgt && (awgt->state() & ArenaWidget::Hidden)) || !awgt)
        return;

    if (!awgt->getWidget())
        return;
    TabButton *btn = new TabButton(tabContents);
    btn->setProperty("arenaTabId", QVariant::fromValue(registry.add(awgt)));
    btn->setText(tab_navigation::title(awgt));
    btn->setToolTip(tab_navigation::toolTip(awgt));
    btn->setWidgetIcon(awgt->getIcon());
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    btn->installEventFilter(this);

    fr_layout->addWidget(btn);

    awgt_map.insert(awgt, btn);
    tbtn_map.insert(btn, awgt);
    
    if (awgt->toolButton())
        awgt->toolButton()->setChecked(true);

    connect(btn, &QWidget::customContextMenuRequested, this, &TabFrame::slotContextMenu);
    connect(btn, &TabButton::clicked, this, &TabFrame::buttonClicked);
    connect(btn, &TabButton::closeRequest, this, &TabFrame::closeRequsted);
    connect(btn, &TabButton::dropped, this, &TabFrame::slotDropped);
    refreshTabList();
    scheduleLayout();
}

bool TabFrame::hasWidget(ArenaWidget *awgt) const{
    DEBUG_BLOCK
    
    return awgt_map.contains(awgt);
}

void TabFrame::mapped(ArenaWidget *awgt){
    DEBUG_BLOCK
    
    if (!awgt_map.contains(awgt))
        return;

    TabButton *btn = const_cast<TabButton*>(awgt_map.value(awgt));

    btn->setChecked(true);
    btn->setFocus();

    historyPush(awgt);
    revealActive();
    if (allTabs->isVisible())
        refreshTabList();
}

void TabFrame::updated ( ArenaWidget* awgt ) {
    DEBUG_BLOCK
    
    if (!awgt)
        return;
    if (awgt->state() & ArenaWidget::Hidden){
        removeWidget(awgt);
    }
    else if (!awgt_map.contains(awgt)){
        insertWidget(awgt);
    }
}

void TabFrame::redraw() {
    DEBUG_BLOCK
    
    for (auto it = tbtn_map.begin(); it != tbtn_map.end(); ++it){
        TabButton *btn = const_cast<TabButton*>(it.key());
        ArenaWidget *awgt = registry.resolve(btn->property("arenaTabId").toULongLong());
        if (!awgt)
            continue;

        btn->setText(tab_navigation::title(awgt));
        btn->setToolTip(tab_navigation::toolTip(awgt));
        btn->setWidgetIcon(awgt->getIcon());

        if (awgt->state() & ArenaWidget::Hidden)
            continue;
        else
            btn->resetGeometry();
    }
    if (allTabs->isVisible())
        refreshTabList();
    scheduleLayout();
}

void TabFrame::refreshTabList()
{
    QList<tab_navigation::Entry> entries;
    for (int i = 0; i < fr_layout->count(); ++i) {
        auto *button = qobject_cast<TabButton*>(fr_layout->itemAt(i)->widget());
        if (!button)
            continue;
        const auto id = button->property("arenaTabId").toULongLong();
        if (auto *widget = registry.resolve(id))
            entries.append({id, tab_navigation::title(widget), tab_navigation::details(widget),
                            widget->getIcon(), button->isChecked()});
    }
    allTabs->setEntries(entries);
}

void TabFrame::scheduleLayout()
{
    if (layoutPending)
        return;
    layoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        layoutPending = false;
        layoutTabs();
    });
}

void TabFrame::layoutTabs()
{
    const int available = qMax(80, scrollArea->viewport()->width());
    int rowHeight = 30;
    for (auto *button : tbtn_map.keys()) {
        button->setMaximumWidth(qMin(tab_navigation::MaximumWidth, available));
        rowHeight = qMax(rowHeight, button->normalHeight());
    }
    const int contentHeight = fr_layout->heightForWidth(available);
    const bool geometryChanged = tabContents->size() != QSize(available, contentHeight);
    tabContents->resize(available, contentHeight);
    fr_layout->setGeometry(tabContents->rect());
    const int visibleHeight = qBound(rowHeight, contentHeight, rowHeight * 3 + 4);
    if (minimumHeight() != visibleHeight)
        setFixedHeight(visibleHeight);
    const bool overflow = contentHeight > visibleHeight;
    previousRow->setVisible(overflow);
    nextRow->setVisible(overflow);
    scrollArea->verticalScrollBar()->setSingleStep(rowHeight + 2);
    TabButton *active = nullptr;
    for (auto *button : tbtn_map.keys())
        if (button->isChecked()) {
            active = button;
            break;
        }
    const bool activeMoved = active &&
        (lastActiveButton != active || lastActiveGeometry != active->geometry());
    if (!active) {
        lastActiveButton.clear();
        lastActiveGeometry = QRect();
    }
    if (geometryChanged || activeMoved)
        revealActive();
}

void TabFrame::revealActive()
{
    for (auto *button : tbtn_map.keys()) {
        if (button->isChecked()) {
            scrollArea->ensureWidgetVisible(button, 0, 0);
            // Content coordinates do not change when the user scrolls the viewport.
            lastActiveButton = button;
            lastActiveGeometry = button->geometry();
            break;
        }
    }
}

void TabFrame::historyPush(ArenaWidget *awgt){
    DEBUG_BLOCK
    
    historyPurge(awgt);

    history.push_back(awgt);
}

void TabFrame::historyPurge(ArenaWidget *awgt){
    DEBUG_BLOCK
    
    if (history.contains(awgt))
        history.removeAt(history.indexOf(awgt));
}

void TabFrame::historyPop(){
    DEBUG_BLOCK
    
    if (history.isEmpty() && fr_layout->count() > 0){
        QLayoutItem *item = fr_layout->itemAt(0);

        if (!item)
            return;

        TabButton *btn = qobject_cast<TabButton*>(item->widget());

        if (btn)
            qtCtx()->arenaWidgetManager()->activate(tbtn_map[btn]);

        return;
    }
    else if (history.isEmpty()){
        qtCtx()->arenaWidgetManager()->activate(nullptr);
        
        return;
    }

    ArenaWidget *awgt = history.takeLast();

    qtCtx()->arenaWidgetManager()->activate(awgt);
}

void TabFrame::buttonClicked(){
    DEBUG_BLOCK
    
    TabButton *btn = qobject_cast<TabButton*>(sender());

    if (!(btn && tbtn_map.contains(btn)))
        return;

    btn->setFocus();

    if (auto *widget = registry.resolve(btn->property("arenaTabId").toULongLong()))
        qtCtx()->arenaWidgetManager()->activate(widget);
}

void TabFrame::closeRequsted() {
    DEBUG_BLOCK
    
    TabButton *btn = qobject_cast<TabButton*>(sender());

    if (!(btn && tbtn_map.contains(btn)))
        return;

    if (auto *widget = registry.resolve(btn->property("arenaTabId").toULongLong()))
        qtCtx()->arenaWidgetManager()->rem(widget);
}

void TabFrame::nextTab(){
    DEBUG_BLOCK
    
    TabButton *next = nullptr;

    for (int i = 0; i < fr_layout->count(); i++){
        TabButton *t = qobject_cast<TabButton*>(fr_layout->itemAt(i)->widget());

        if (t && t->isChecked()){
            if (i == (fr_layout->count()-1)){
                next = qobject_cast<TabButton*>(fr_layout->itemAt(0)->widget());
                break;
            }

            next = qobject_cast<TabButton*>(fr_layout->itemAt(i+1)->widget());
            break;
        }
    }

    if (!next)
        return;

    qtCtx()->arenaWidgetManager()->activate(tbtn_map[next]);
}

void TabFrame::prevTab(){
    DEBUG_BLOCK
    
    TabButton *next = nullptr;

    for (int i = 0; i < fr_layout->count(); i++){
        TabButton *t = qobject_cast<TabButton*>(fr_layout->itemAt(i)->widget());

        if (t && t->isChecked()){
            if (!i){
                next = qobject_cast<TabButton*>(fr_layout->itemAt(fr_layout->count()-1)->widget());
                break;
            }

            next = qobject_cast<TabButton*>(fr_layout->itemAt(i-1)->widget());
            break;
        }
    }

    if (!next)
        return;

   qtCtx()->arenaWidgetManager()->activate(tbtn_map[next]);
}

void TabFrame::slotShorcuts(){
    DEBUG_BLOCK
    
    QShortcut *sh = qobject_cast<QShortcut*>(sender());

    if (!sh)
        return;

    int index = shortcuts.indexOf(sh);

    if (index >= 0 && fr_layout->count() >= (index + 1)){
        TabButton *next = qobject_cast<TabButton*>(fr_layout->itemAt(index)->widget());

        if (!next)
            return;

        qtCtx()->arenaWidgetManager()->activate(tbtn_map[next]);
    }
}

void TabFrame::slotContextMenu() {
    DEBUG_BLOCK
    
    TabButton *btn = qobject_cast<TabButton*>(sender());

    if (!(btn && tbtn_map.contains(btn)))
        return;

    ArenaWidget *awgt = const_cast<ArenaWidget*>(tbtn_map[btn]);

    if (awgt) {
        QMenu *widget_menu = awgt->getMenu();
        if (widget_menu) {
            widget_menu->exec(btn->mapToGlobal(btn->rect().bottomLeft()));
        } else {
            widget_menu = new QMenu(this);
            widget_menu->addAction(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiEDITDELETE), tr("Close"));

            const auto id = btn->property("arenaTabId").toULongLong();
            if (widget_menu->exec(QCursor::pos())) {
                if (auto *widget = registry.resolve(id))
                    qtCtx()->arenaWidgetManager()->rem(widget);
            }

            delete widget_menu;
        }
    }
}

void TabFrame::slotDropped(TabButton *source, TabButton *target){
    DEBUG_BLOCK

    if (!source || !target || source == target)
        return;

    if (!tbtn_map.contains(source) || !tbtn_map.contains(target))
        return;

    QList<TabButton*> order;

    for (int i = 0; i < fr_layout->count(); ++i) {
        QLayoutItem *item = fr_layout->itemAt(i);
        if (!item)
            continue;

        TabButton *btn = qobject_cast<TabButton*>(item->widget());
        if (btn)
            order.push_back(btn);
    }

    const int from = order.indexOf(source);
    const int to   = order.indexOf(target);

    if (from < 0 || to < 0 || from == to)
        return;

    order.move(from, to);

    while (QLayoutItem *item = fr_layout->takeAt(0)) {
        delete item;
    }

    for (TabButton *btn : order)
        fr_layout->addWidget(btn);

    fr_layout->invalidate();
    updateGeometry();
    update();
    refreshTabList();
    scheduleLayout();

    qtCtx()->arenaWidgetManager()->activate(tbtn_map[source]);
}

void TabFrame::moveLeft(){
    DEBUG_BLOCK
    
    for (int i = 0; i < fr_layout->count(); i++){
        QLayoutItem *item = const_cast<QLayoutItem*>(fr_layout->itemAt(i));
        TabButton *t = qobject_cast<TabButton*>(item->widget());

        if (t && t->isChecked()){
            fr_layout->moveLeft(item);
            refreshTabList();
            revealActive();

            break;
        }
    }
}

void TabFrame::moveRight(){
    DEBUG_BLOCK
    
    for (int i = 0; i < fr_layout->count(); i++){
        QLayoutItem *item = const_cast<QLayoutItem*>(fr_layout->itemAt(i));
        TabButton *t = qobject_cast<TabButton*>(item->widget());

        if (t && t->isChecked()){
            fr_layout->moveRight(item);
            refreshTabList();
            revealActive();

            break;
        }
    }
}

void TabFrame::toggled ( ArenaWidget* awgt ) {
    DEBUG_BLOCK
    
    if (!awgt)
        return;
    
    if (!(awgt->state() & ArenaWidget::Singleton))
        return;
    
    if (awgt->state() & ArenaWidget::Hidden)
        qtCtx()->arenaWidgetManager()->activate(awgt);
    else
        qtCtx()->arenaWidgetManager()->rem(awgt);
}
