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

#include "SettingsAdvanced.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "dcpp/DCPlusPlus.h"
#include "MainWindow.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>

using namespace dcpp;

SettingsAdvanced::SettingsAdvanced(QWidget *parent) :
    QWidget(parent)
{
    setupUi(this);
    init();
}

SettingsAdvanced::~SettingsAdvanced() {

}

void SettingsAdvanced::ok() {
    SettingsManager *SM = qtCtx()->dcCtx().getSettingsManager();

    const QString handler = checkBox_CUSTOM_MIME->isChecked() ? lineEdit_MIME->text().trimmed() : QString();
    SM->set(SettingsManager::MIME_HANDLER, _tq(handler));
}

void SettingsAdvanced::init() {
    const QString handler = _q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MIME_HANDLER, true)).trimmed();
    lineEdit_MIME->setText(handler);
    lineEdit_MIME->setPlaceholderText(tr("Use system defaults unless a custom handler is enabled"));
    lineEdit_MIME->setToolTip(tr("Optional command or macOS .app used for web links and non-DC magnet links."));
    checkBox_CUSTOM_MIME->setChecked(!handler.isEmpty());

    const auto updateMimeControls = [this](bool enabled) {
        lineEdit_MIME->setEnabled(enabled);
        toolButton_BROWSE->setEnabled(enabled);
    };
    connect(checkBox_CUSTOM_MIME, &QCheckBox::toggled, this, updateMimeControls);
    updateMimeControls(checkBox_CUSTOM_MIME->isChecked());

    toolButton_BROWSE->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFOLDER_BLUE));

    connect(toolButton_BROWSE, &QToolButton::clicked, this, &SettingsAdvanced::slotBrowse);
}

void SettingsAdvanced::slotBrowse()
{
    QString file = QFileDialog::getOpenFileName(this,
                                                tr("Select application or executable"),
                                                QStringLiteral("/Applications"),
                                                tr("Applications (*.app);;All files (*)"));

    if (file.isEmpty())
        return;

    file = QDir::toNativeSeparators(file);
    lineEdit_MIME->setText(file);
}
