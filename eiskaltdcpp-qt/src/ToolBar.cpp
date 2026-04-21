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

#include "ToolBar.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "WulforUtil.h"

#include <QMenu>
#include <QMouseEvent>
#include <QSize>
#include <QToolButton>
#include <QStyle>
#include <QHBoxLayout>

#include "ArenaWidget.h"
#include "ArenaWidgetManager.h"
#include "MainWindow.h"
#include "PMWindow.h"
#include "WulforSettings.h"
#include "GlobalTimer.h"

ToolBar::ToolBar(QWidget *parent):
    QToolBar(parent),
    tabbar(nullptr)
{
    setContextMenuPolicy(Qt::CustomContextMenu);
}

ToolBar::~ToolBar(){
    for (const auto &s : shortcuts)
        s->deleteLater();
}

bool ToolBar::eventFilter(QObject *obj, QEvent *e){
    if (e->type() == QEvent::MouseButtonRelease){
        QMouseEvent *m_e = reinterpret_cast<QMouseEvent*>(e);

        if (reinterpret_cast<QTabBar*>(obj) == tabbar && m_e->button() == Qt::MiddleButton){
            QPoint p = tabbar->mapFromGlobal(QCursor::pos());
            int index = tabbar->tabAt(p);

            if (index >= 0)
                slotClose(index);
        }
    }
    else if (e->type() == QEvent::DragEnter && reinterpret_cast<QTabBar*>(obj) == tabbar) {
        QDragEnterEvent *m_e = reinterpret_cast<QDragEnterEvent*>(e);
        m_e->acceptProposedAction();

        int tab = tabbar->tabAt(m_e->position().toPoint());
        if (tab >=0 && tab != tabbar->currentIndex())
            slotIndexChanged(tab);

        return true;
    }
    else if (e->type() == QEvent::DragMove && reinterpret_cast<QTabBar*>(obj) == tabbar) {
        QDragMoveEvent *m_e = reinterpret_cast<QDragMoveEvent*>(e);

        int tab = tabbar->tabAt(m_e->position().toPoint());
        if (tab >=0) {
            m_e->acceptProposedAction();
            if (tab != tabbar->currentIndex())
                slotIndexChanged(tab);
        } else {
            m_e->setDropAction(Qt::IgnoreAction);
        }
        return true;
    }

    return QToolBar::eventFilter(obj, e);
}

void ToolBar::showEvent(QShowEvent *e){
    e->accept();

    if (tabbar && e->spontaneous()){
        tabbar->hide();// I know, this is crap, but tabbar->repaint() doesn't fit all tabs in tabbar properly when
        tabbar->show();// MainWindow becomes visible (restoring from system tray)
    }
}

void ToolBar::initTabs(){
    tabbar = new QTabBar(parentWidget());
    tabbar->setObjectName("arenaTabbar");
    tabbar->setTabsClosable(false);
    tabbar->setDocumentMode(true);
    tabbar->setMovable(true);
    tabbar->setSelectionBehaviorOnRemove(QTabBar::SelectPreviousTab);
    tabbar->setExpanding(false);
    tabbar->setElideMode(Qt::ElideNone);
    tabbar->setUsesScrollButtons(true);
    tabbar->setContextMenuPolicy(Qt::CustomContextMenu);
    tabbar->setSizePolicy(QSizePolicy::Expanding, tabbar->sizePolicy().verticalPolicy());
    tabbar->setIconSize(QSize(16, 16));
    tabbar->setAcceptDrops(true);
    tabbar->setStyleSheet(QStringLiteral(
        "QTabBar#arenaTabbar { background: transparent; }"
        "QTabBar#arenaTabbar::tab {"
        " min-width: 0px;"
        " padding: 4px 42px 4px 12px;"
        " margin-right: 4px;"
        " border: 1px solid palette(mid);"
        " border-radius: 6px;"
        " background: palette(button);"
        "}"
        "QTabBar#arenaTabbar::tab:selected {"
        " background: palette(window);"
        " font-weight: 600;"
        "}"
        "QTabBar#arenaTabbar::tab:hover:!selected {"
        " background: palette(alternate-base);"
        "}"
        "QTabBar#arenaTabbar::close-button {"
        " width: 14px;"
        " height: 14px;"
        " subcontrol-origin: padding;"
        " subcontrol-position: right;"
        " right: 20px;"
        " margin: 0px;"
        "}"));

    tabbar->installEventFilter(this);

    shortcuts << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_1)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_2)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_3)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_4)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_5)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_6)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_7)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_8)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_9)), parentWidget()))
              << (new QShortcut(QKeySequence(int(Qt::ALT) | int(Qt::Key_0)), parentWidget()));

    for (const auto &s : shortcuts){
        s->setContext(Qt::ApplicationShortcut);

        connect(s, &QShortcut::activated, this, &ToolBar::slotShorcuts);
    }

    connect(tabbar, &QTabBar::currentChanged, this, &ToolBar::slotIndexChanged);
    connect(tabbar, &QTabBar::tabMoved, this, &ToolBar::slotTabMoved);
    connect(tabbar, &QTabBar::tabCloseRequested, this, &ToolBar::slotClose);
    connect(tabbar, &QWidget::customContextMenuRequested, this, &ToolBar::slotContextMenu);
    
    connect(qtCtx()->arenaWidgetManager(), &ArenaWidgetManager::added,     this, &ToolBar::insertWidget);
    connect(qtCtx()->arenaWidgetManager(), &ArenaWidgetManager::removed,   this, &ToolBar::removeWidget);
    connect(qtCtx()->arenaWidgetManager(), &ArenaWidgetManager::activated, this, &ToolBar::mapped);
    connect(qtCtx()->arenaWidgetManager(), &ArenaWidgetManager::updated,   this, &ToolBar::updated);
    connect(qtCtx()->arenaWidgetManager(), &ArenaWidgetManager::toggled,   this, &ToolBar::toggled);
       
    connect(qtCtx()->globalTimer(), &GlobalTimer::second, this, &ToolBar::redraw);

    addWidget(tabbar);
    syncCloseButtons();
}

void ToolBar::insertWidget(ArenaWidget *awgt){
    if (!(awgt && awgt->getWidget()) || (awgt->state() & ArenaWidget::Hidden) || map.contains(awgt))
        return;

    int index = tabbar->addTab(awgt->getPixmap(), awgt->getArenaShortTitle());

    if (awgt->toolButton())
        awgt->toolButton()->setChecked(true);

    if (index >= 0){
        map.insert(awgt, index);
        syncCloseButtons();

        if (tabbar->isHidden())
            tabbar->show();

        if (!(typeid(*awgt) == typeid(PMWindow) && qtCtx()->settings()->getBool(WB_CHAT_KEEPFOCUS)))
            tabbar->setCurrentIndex(index);
    }
}

void ToolBar::removeWidget(ArenaWidget *awgt){
    if (!awgt || !awgt->getWidget() || !map.contains(awgt))
        return;

    int index = map.value(awgt);

    if (index >= 0){
        map.erase(map.find(awgt));

        rebuildIndexes(index);

        tabbar->removeTab(index);
        syncCloseButtons();

        if (map.isEmpty())
            tabbar->hide();

        if (awgt->toolButton())
            awgt->toolButton()->setChecked(false);
    }
}

void ToolBar::updated ( ArenaWidget *awgt ) {
    if (!awgt)
        return;
    
    if ( awgt->state() & ArenaWidget::Hidden ) {
        removeWidget ( awgt );
    } else if ( !map.contains(awgt)) {
        insertWidget ( awgt );
    }
}

void ToolBar::slotIndexChanged(int index){
    if (index < 0)
        return;

    ArenaWidget *awgt = findWidgetForIndex(index);

    if (!awgt || !awgt->getWidget())
        return;

    qtCtx()->arenaWidgetManager()->activate(awgt);
}

void ToolBar::toggled ( ArenaWidget *awgt) {
    if (!awgt)
        return;
        
    if (!(awgt->state() & ArenaWidget::Singleton))
        return;
    
    if (awgt->state() & ArenaWidget::Hidden)
        qtCtx()->arenaWidgetManager()->activate(awgt);
    else
        qtCtx()->arenaWidgetManager()->rem(awgt);
}

void ToolBar::slotTabMoved(int from, int to){
    ArenaWidget *from_wgt = nullptr;
    ArenaWidget *to_wgt   = nullptr;

    for (auto it = map.begin(); it != map.end(); ++it){
        if (it.value() == from){
            from_wgt = it.key();
        }
        else if (it.value() == to)
            to_wgt = it.key();

        if (to_wgt && from_wgt){
            map[to_wgt] = from;
            map[from_wgt] = to;
            syncCloseButtons();

            slotIndexChanged(tabbar->currentIndex());

            return;
        }
    }
}

void ToolBar::slotClose(int index){
    if (index < 0)
        return;

    ArenaWidget *awgt = findWidgetForIndex(index);

    if (!awgt || !awgt->getWidget())
        return;

    qtCtx()->arenaWidgetManager()->rem(awgt);
}

void ToolBar::slotContextMenu(const QPoint &p){
    int tab = tabbar->tabAt(p);
    ArenaWidget *awgt = findWidgetForIndex(tab);

    if (!awgt){
        QMenu *m = new QMenu(this);
        QAction *act = new QAction(tr("Show close buttons"), m);

        act->setCheckable(true);
        act->setChecked(qtCtx()->settings()->getBool(WB_APP_TBAR_SHOW_CL_BTNS));

        m->addAction(act);

        if (m->exec(QCursor::pos())){
            qtCtx()->settings()->setBool(WB_APP_TBAR_SHOW_CL_BTNS, act->isChecked());
            tabbar->setTabsClosable(false);
            syncCloseButtons();
        }

        m->deleteLater();

        return;
    }

    QMenu *m = awgt->getMenu();

    if (m)
        m->exec(QCursor::pos());
}

void ToolBar::slotShorcuts(){
    QShortcut *sh = qobject_cast<QShortcut*>(sender());

    if (!sh)
        return;

    int index = shortcuts.indexOf(sh);

    if (index >= 0 && tabbar->count() >= (index + 1))
        tabbar->setCurrentIndex(index);
}

ArenaWidget *ToolBar::findWidgetForIndex(const int index){
    if (index < 0)
        return nullptr;

    for (const auto &k : map.keys()) {
        if (map[k] == index)
            return const_cast<ArenaWidget*>(k);
    }

    return nullptr;
}

void ToolBar::redraw(){
    for (auto it = map.begin(); it != map.end(); ++it){
        tabbar->setTabText(it.value(), it.key()->getArenaShortTitle());
        tabbar->setTabToolTip(it.value(), qtCtx()->wulforUtil()->compactToolTipText(it.key()->getArenaTitle(), 60, "\n"));
        tabbar->setTabIcon(it.value(), it.key()->getPixmap());
    }

    syncCloseButtons();

    tabbar->repaint();

    ArenaWidget *awgt = findWidgetForIndex(tabbar->currentIndex());

    if (awgt)
        qtCtx()->mainWindow()->setWindowTitle(awgt->getArenaTitle() +
                                   " :: " + QString::fromStdString(eiskaltdcppAppNameString));
}

QWidget *ToolBar::makeCloseButton(int index)
{
    auto *holder = new QWidget(tabbar);
    holder->setFixedSize(QSize(30, 16));

    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 12, 0);
    layout->setSpacing(0);

    auto *button = new QToolButton(holder);
    button->setAutoRaise(true);
    button->setCursor(Qt::ArrowCursor);
    button->setFixedSize(QSize(16, 16));
    button->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    button->setIconSize(QSize(12, 12));
    button->setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0px; margin: 0px; }"));
    connect(button, &QToolButton::clicked, this, [this, index]() { slotClose(index); });
    layout->addWidget(button, 0, Qt::AlignVCenter | Qt::AlignRight);

    return holder;
}

void ToolBar::syncCloseButtons()
{
    if (!tabbar)
        return;

    const bool showClose = qtCtx()->settings()->getBool(WB_APP_TBAR_SHOW_CL_BTNS);
    tabbar->setTabsClosable(false);

    for (int index = 0; index < tabbar->count(); ++index) {
        QWidget *existing = tabbar->tabButton(index, QTabBar::RightSide);
        if (existing) {
            existing->deleteLater();
            tabbar->setTabButton(index, QTabBar::RightSide, nullptr);
        }

        if (showClose)
            tabbar->setTabButton(index, QTabBar::RightSide, makeCloseButton(index));
    }
}

void ToolBar::nextTab(){
    if (!tabbar)
        return;

    if (tabbar->currentIndex()+1 < tabbar->count())
        tabbar->setCurrentIndex(tabbar->currentIndex()+1);
    else
        tabbar->setCurrentIndex(0);
}

void ToolBar::prevTab(){
    if (!tabbar)
        return;

    if (tabbar->currentIndex()-1 >= 0)
        tabbar->setCurrentIndex(tabbar->currentIndex()-1);
    else
        tabbar->setCurrentIndex(tabbar->count()-1);
}

void ToolBar::rebuildIndexes(const int removed){
    if (removed < 0)
        return;

    for (auto it = map.begin(); it != map.end(); ++it){
        if (it.value() > removed)
            map[it.key()] = it.value()-1;
    }
}

void ToolBar::mapped(ArenaWidget *awgt){
    blockSignals(true);
    if (map.contains(awgt))
        tabbar->setCurrentIndex(map[awgt]);

    redraw();

    blockSignals(false);
}

bool ToolBar::hasWidget(ArenaWidget *w) const{
    return map.contains(w);
}

void ToolBar::mapWidget(ArenaWidget *w){
    if (hasWidget(w))
        tabbar->setCurrentIndex(map[w]);
}
