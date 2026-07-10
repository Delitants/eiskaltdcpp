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

#include "SettingsPersonal.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "LocalizedDefaults.h"

#include <QComboBox>
#include <QLabel>

#include "dcpp/stdinc.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/DCPlusPlus.h"

#include "ClientTagPresets.h"
#include "WulforUtil.h"
#include "WulforSettings.h"

using namespace dcpp;

namespace {

void populateSpeedCombo(QComboBox *combo, const string& currentSpeed) {
    for (auto i = SettingsManager::connectionSpeeds.begin(); i != SettingsManager::connectionSpeeds.end(); ++i) {
        combo->addItem((*i).c_str());

        if (currentSpeed == *i)
            combo->setCurrentIndex(i - SettingsManager::connectionSpeeds.begin());
    }
}

string protocolSpeed(SettingsManager *settings, SettingsManager::StrSetting setting) {
    if (!settings->isDefault(setting))
        return settings->get(setting, false);

    if (!settings->isDefault(SettingsManager::UPLOAD_SPEED))
        return settings->get(SettingsManager::UPLOAD_SPEED, false);

    return settings->get(setting, true);
}

bool selectedSpeed(QComboBox *combo, string& speed) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= static_cast<int>(SettingsManager::connectionSpeeds.size()))
        return false;

    speed = SettingsManager::connectionSpeeds[index];
    return true;
}

void populateNmdcSpeedCombo(QComboBox *combo, const string& currentSpeed) {
    for (auto i = SettingsManager::nmdcConnectionSpeeds.begin(); i != SettingsManager::nmdcConnectionSpeeds.end(); ++i) {
        combo->addItem((*i).c_str());

        if (currentSpeed == *i)
            combo->setCurrentIndex(i - SettingsManager::nmdcConnectionSpeeds.begin());
    }
}

bool selectedNmdcSpeed(QComboBox *combo, string& speed) {
    if (combo->currentIndex() < 0)
        return false;

    speed = combo->currentText().toStdString();
    return !speed.empty();
}

}

SettingsPersonal::SettingsPersonal(QWidget *parent):
        QWidget(parent)
{
    setupUi(this);

    init();
}

SettingsPersonal::~SettingsPersonal(){

}

void SettingsPersonal::ok(){
    SettingsManager *SM = qtCtx()->dcCtx().getSettingsManager();

    SM->set(SettingsManager::NICK, lineEdit_NICK->text().trimmed().toStdString());
    SM->set(SettingsManager::EMAIL, lineEdit_EMAIL->text().toStdString());
    SM->set(SettingsManager::DESCRIPTION, lineEdit_DESC->text().toStdString());
    string adcSpeed;
    if (selectedSpeed(comboBox_SPEED, adcSpeed)) {
        SM->set(SettingsManager::UPLOAD_SPEED, adcSpeed);
        SM->set(SettingsManager::ADC_UPLOAD_SPEED, adcSpeed);
    }

    string nmdcSpeed;
    if (selectedNmdcSpeed(comboBox_NMDC_SPEED, nmdcSpeed))
        SM->set(SettingsManager::NMDC_UPLOAD_SPEED, nmdcSpeed);

    const QString awayMessage = lineEdit_AWAYMSG->text().trimmed();
    const QString translationsPath = qtCtx()->wulforUtil()->getTranslationsPath();
    const QString storedAwayMessage = (awayMessage.isEmpty() ||
            LocalizedDefaults::isAwayMessageDefault(awayMessage, translationsPath))
            ? LocalizedDefaults::awayMessage()
            : awayMessage;
    SM->set(SettingsManager::DEFAULT_AWAY_MESSAGE, storedAwayMessage.toStdString());
    SM->set(SettingsManager::CLIENT_ID_NMDC, comboBox_CLIENT_ID->currentData(Qt::UserRole).toString().toStdString());
    SM->set(SettingsManager::CLIENT_ID_ADC, comboBox_CLIENT_ID->currentData(Qt::UserRole + 1).toString().toStdString());

    QString enc = comboBox_ENC->currentText();

    qtCtx()->settings()->setStr(WS_DEFAULT_LOCALE, enc);
    enc = qtCtx()->wulforUtil()->qtEnc2DcEnc(comboBox_ENC->currentText());

    if (enc.indexOf(" ") > 0){
        enc = enc.left(enc.indexOf(" "));
        enc.replace(" ", "");
    }

    Text::hubDefaultCharset = _tq(enc);

    qtCtx()->settings()->setBool(WB_APP_AUTOAWAY_BY_TIMER, checkBox_AUTOAWAY->isChecked());
    qtCtx()->settings()->setInt(WI_APP_AUTOAWAY_INTERVAL, spinBox->value());

    SM->save();

    qtCtx()->settings()->save();
}

void SettingsPersonal::init(){
    lineEdit_NICK->setText(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::NICK, true).c_str());
    lineEdit_EMAIL->setText(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::EMAIL, true).c_str());
    lineEdit_DESC->setText(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DESCRIPTION, true).c_str());
    lineEdit_AWAYMSG->setPlaceholderText(LocalizedDefaults::awayMessage());
    const QString awayMessage = _q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DEFAULT_AWAY_MESSAGE, true));
    lineEdit_AWAYMSG->setText(LocalizedDefaults::isAwayMessageDefault(awayMessage, qtCtx()->wulforUtil()->getTranslationsPath())
            ? LocalizedDefaults::awayMessage()
            : awayMessage);

    SettingsManager *SM = qtCtx()->dcCtx().getSettingsManager();
    populateSpeedCombo(comboBox_SPEED, protocolSpeed(SM, SettingsManager::ADC_UPLOAD_SPEED));
    populateNmdcSpeedCombo(comboBox_NMDC_SPEED, protocolSpeed(SM, SettingsManager::NMDC_UPLOAD_SPEED));

    QStringList encodings = qtCtx()->wulforUtil()->encodings();

    comboBox_ENC->addItem(tr("System default"));
    comboBox_ENC->addItems(encodings);

    QString default_enc = qtCtx()->settings()->getStr(WS_DEFAULT_LOCALE);

    if (encodings.contains(default_enc))
        comboBox_ENC->setCurrentIndex(encodings.indexOf(default_enc)+1);
    else
        comboBox_ENC->setCurrentIndex(0);

    checkBox_AUTOAWAY->setChecked(qtCtx()->settings()->getBool(WB_APP_AUTOAWAY_BY_TIMER));
    spinBox->setValue(qtCtx()->settings()->getInt(WI_APP_AUTOAWAY_INTERVAL));

    initClientTagPresets();
}

void SettingsPersonal::initClientTagPresets(){
    comboBox_CLIENT_ID = new QComboBox(frame_2);

    const auto presetList = ClientTagPresets::presets();
    for (const ClientTagPresets::Preset &preset : presetList) {
        comboBox_CLIENT_ID->addItem(preset.label);
        const int index = comboBox_CLIENT_ID->count() - 1;
        comboBox_CLIENT_ID->setItemData(index, preset.nmdc, Qt::UserRole);
        comboBox_CLIENT_ID->setItemData(index, preset.adc, Qt::UserRole + 1);
    }

    const QString currentNMDC = _q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::CLIENT_ID_NMDC, false));
    const QString currentADC = _q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::CLIENT_ID_ADC, false));
    for (int i = 0; i < comboBox_CLIENT_ID->count(); ++i) {
        if (comboBox_CLIENT_ID->itemData(i, Qt::UserRole).toString() == currentNMDC &&
            comboBox_CLIENT_ID->itemData(i, Qt::UserRole + 1).toString() == currentADC) {
            comboBox_CLIENT_ID->setCurrentIndex(i);
            break;
        }
    }

    QLabel *label = new QLabel(tr("Client tag"), frame_2);
    label->setWordWrap(true);
    comboBox_CLIENT_ID->setToolTip(tr("Optional client tag spoofing preset. Favorite hub settings can override this per hub."));
    gridLayout->addWidget(label, 3, 0);
    gridLayout->addWidget(comboBox_CLIENT_ID, 3, 1, 1, 2);
}
