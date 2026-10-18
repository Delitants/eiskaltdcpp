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

#include "SettingsUC.h"
#include "UCModel.h"

#include <QItemSelectionModel>
#include <QPushButton>
#include <QSizePolicy>

#include <dcpp/FavoriteManager.h>

SettingsUC::SettingsUC(QWidget *parent) :
    QWidget(parent)
{
    setupUi(this);

    model = new UCModel(this);
    model->loadUC();

    treeView->setModel(model);
    gridLayout->setHorizontalSpacing(10);
    gridLayout->setVerticalSpacing(6);

    const QList<QPushButton*> buttons {
        pushButton_ADD,
        pushButton_CH,
        pushButton_REM,
        pushButton_UP,
        pushButton_DOWN
    };
    for (QPushButton *button : buttons) {
        button->setMinimumSize(78, 28);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    connect(pushButton_ADD, &QPushButton::clicked,          model, &UCModel::newUC);
    connect(pushButton_REM, &QPushButton::clicked,          this,  &SettingsUC::slotRemClicked);
    connect(pushButton_CH,  &QPushButton::clicked,          this,  &SettingsUC::slotChangeClicked);
    connect(pushButton_UP,  &QPushButton::clicked,          this,  &SettingsUC::slotUpClicked);
    connect(pushButton_DOWN, &QPushButton::clicked,          this,  &SettingsUC::slotDownClicked);
    connect(this,           &SettingsUC::remUC,     model, &UCModel::remUC);
    connect(this,           &SettingsUC::changeUC,  model, &UCModel::changeUC);
    connect(this,           &SettingsUC::upUC,      model, &UCModel::moveUp);
    connect(this,           &SettingsUC::downUC,    model, &UCModel::moveDown);
    connect(model,          &UCModel::selectIndex, this, &SettingsUC::slotSelect);
    connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SettingsUC::updateActionState);
    connect(treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SettingsUC::updateActionState);
    connect(model, &QAbstractItemModel::rowsInserted, this, &SettingsUC::updateActionState);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &SettingsUC::updateActionState);
    connect(model, &QAbstractItemModel::modelReset, this, &SettingsUC::updateActionState);
    connect(model, &QAbstractItemModel::layoutChanged, this, &SettingsUC::updateActionState);
    updateActionState();
}

SettingsUC::~SettingsUC(){
    model->deleteLater();
}

void SettingsUC::ok(){

}

QModelIndex SettingsUC::selectedIndex(){
    QItemSelectionModel *s_m = treeView->selectionModel();
    QModelIndexList list = s_m->selectedRows(0);

    if (!list.isEmpty())
        return list.at(0);
    else
        return QModelIndex();
}

void SettingsUC::slotRemClicked(){
    const auto index = selectedIndex();
    if (index.isValid())
        emit remUC(index);
}

void SettingsUC::slotChangeClicked(){
    const auto index = selectedIndex();
    if (index.isValid())
        emit changeUC(index);
}

void SettingsUC::slotUpClicked(){
    const auto index = selectedIndex();
    if (index.isValid() && index.row() > 0)
        emit upUC(index);
}

void SettingsUC::slotDownClicked(){
    const auto index = selectedIndex();
    if (index.isValid() && index.row() + 1 < model->rowCount())
        emit downUC(index);
}

void SettingsUC::slotSelect(const QModelIndex &i){
    QItemSelectionModel *s_m = treeView->selectionModel();

    s_m->setCurrentIndex(i, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    updateActionState();
}

void SettingsUC::updateActionState(){
    const auto index = selectedIndex();
    const bool selected = index.isValid();
    pushButton_CH->setEnabled(selected);
    pushButton_REM->setEnabled(selected);
    pushButton_UP->setEnabled(selected && index.row() > 0);
    pushButton_DOWN->setEnabled(selected && index.row() + 1 < model->rowCount());
}
