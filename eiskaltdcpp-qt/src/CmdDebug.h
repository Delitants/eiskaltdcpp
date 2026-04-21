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

#pragma once

#include <QEvent>
#include <QWidget>

#include "ui_UICmdDebug.h"
#include "ArenaWidget.h"
#include "WulforUtil.h"

#include <dcpp/stdinc.h>
#include <dcpp/DebugManager.h>
#include <dcpp/Text.h>

class CmdDebugPrivate {
public:
    int maxLines;
};

#include "QtContextAware.h"
#include "QtContext.h"

class CmdDebug : public QWidget,
        private Ui::UICmdDebug,
        public ArenaWidget,
        private dcpp::DebugManagerListener,
        public QtContextAware
{
    Q_OBJECT
    Q_INTERFACES(ArenaWidget)

    friend class QtContext;
public:
    explicit CmdDebug(dcpp::DCContext& ctx, QWidget *parent = nullptr);
    ~CmdDebug() override;

    QWidget *getWidget() override;
    QString getArenaTitle() override;
    QString getArenaShortTitle() override;
    QMenu *getMenu() override;
    const QPixmap &getPixmap() override { return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiCONSOLE); }
    void requestClear() override { plainTextEdit_DEBUG->clear(); }
    void requestFilter() override { slotShowSearchBar(); }
    void requestFocus() override { pushButton_ClearLog->setFocus(); }
    ArenaWidget::Role role() const override { return ArenaWidget::CmdDebug; }

Q_SIGNALS:
    void coreDebugCommand(const QString&, const QString&);

private Q_SLOTS:
    void addOutput(const QString&, const QString&);
    void maxLinesChanged(int);
    void slotFindForward() { findText(QTextDocument::FindFlags()); }
    void slotFindBackward(){ findText(QTextDocument::FindBackward); }
    void slotFindTextEdited(const QString &text);
    void slotFindAll();
    void slotShowSearchBar();
    void slotHideSearchBar();
    void slotSettingsChanged(const QString&, const QString&);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void on(dcpp::DebugManagerListener::DebugDetection, const std::string& com) noexcept override;
    void on(dcpp::DebugManagerListener::DebugCommand, const std::string& mess, int typedir, const std::string& ip) noexcept override;
    void findText(QTextDocument::FindFlags );

    Q_DECLARE_PRIVATE(CmdDebug)
    CmdDebugPrivate *d_ptr;
};
