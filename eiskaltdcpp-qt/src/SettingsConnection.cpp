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

#include "SettingsConnection.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "DHTBootstrapList.h"
#include "PublicHubsList.h"
#include "MainWindow.h"
#include "WulforSettings.h"
#include "WulforUtil.h"

#include "dcpp/stdinc.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/Socket.h"
#include "dcpp/DCPlusPlus.h"

#include <QLineEdit>
#include <QRadioButton>
#include <QList>
#include <QMessageBox>
#include <QGridLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QFileDialog>
#include <QLabel>
#include <QToolButton>
#include <QHostAddress>
#include <QAbstractSocket>

#ifndef IPTOS_TOS_MASK
#define	IPTOS_TOS_MASK		0x1E
#endif
#ifndef IPTOS_TOS
#define	IPTOS_TOS(tos)		((tos) & IPTOS_TOS_MASK)
#endif
#ifndef IPTOS_LOWDELAY
#define	IPTOS_LOWDELAY		0x10
#endif
#ifndef IPTOS_THROUGHPUT
#define	IPTOS_THROUGHPUT	0x08
#endif
#ifndef IPTOS_RELIABILITY
#define	IPTOS_RELIABILITY	0x04
#endif
#ifndef IPTOS_LOWCOST
#define	IPTOS_LOWCOST		0x02
#endif
#ifndef IPTOS_MINCOST
#define	IPTOS_MINCOST		IPTOS_LOWCOST
#endif

using namespace dcpp;

SettingsConnection::SettingsConnection( QWidget *parent):
        QWidget(parent),
        dirty(false)
{
    setupUi(this);

    tabWidget->setDocumentMode(true);
    tabWidget->setUsesScrollButtons(false);

    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->setHorizontalSpacing(8);
    formLayout->setVerticalSpacing(6);
    verticalLayout->setContentsMargins(2, 2, 2, 2);
    verticalLayout->setSpacing(6);
    verticalLayout_5->setContentsMargins(2, 2, 2, 2);
    verticalLayout_5->setSpacing(6);
    gridLayout_4->setHorizontalSpacing(10);
    gridLayout_4->setVerticalSpacing(6);
    gridLayout_5->setHorizontalSpacing(14);
    gridLayout_5->setVerticalSpacing(8);
    gridLayout_6->setHorizontalSpacing(8);
    gridLayout_6->setVerticalSpacing(8);
    gridLayout_7->setHorizontalSpacing(14);
    gridLayout_7->setVerticalSpacing(8);
    gridLayout_8->setHorizontalSpacing(8);
    gridLayout_8->setVerticalSpacing(6);
    gridLayout_11->setHorizontalSpacing(10);
    gridLayout_11->setVerticalSpacing(8);
    gridLayout_11->setColumnStretch(0, 0);
    gridLayout_11->setColumnStretch(1, 0);
    gridLayout_11->setColumnStretch(2, 0);
    gridLayout_11->setColumnStretch(3, 1);
    gridLayout_12->setHorizontalSpacing(10);
    gridLayout_12->setVerticalSpacing(6);
    gridLayout_13->setHorizontalSpacing(10);
    gridLayout_13->setVerticalSpacing(6);

    if (auto *item = gridLayout_11->itemAtPosition(0, 0)) {
        if (auto *spacer = item->spacerItem())
            spacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    gridLayout_11->addWidget(label_20, 0, 0);
    gridLayout_11->addWidget(spinBox_RECONNECT_DELAY, 0, 1);
    gridLayout_11->addWidget(label_22, 1, 0);
    gridLayout_11->addWidget(comboBox_TOS, 1, 1);

    label_20->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label_22->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label_20->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    label_22->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    groupBox->setProperty("settingsSectionHeader", true);
    groupBox_2->setProperty("settingsSectionHeader", true);
    groupBox_DHT->setProperty("settingsSectionHeader", true);
    groupBox_4->setFlat(true);
    groupBox_5->setFlat(true);
    groupBox_4->setProperty("settingsFlatSection", true);
    groupBox_5->setProperty("settingsFlatSection", true);
    groupBox_5->layout()->setContentsMargins(0, 0, 0, 0);
    groupBox_4->layout()->setContentsMargins(0, 0, 0, 0);
    groupBox_4->layout()->setSpacing(6);
    groupBox_5->layout()->setSpacing(6);

    checkBox_USE_IPV6 = new QCheckBox(tr("Enable IPv6"), tab);
    formLayout->addRow(checkBox_USE_IPV6);

    auto* labelWanIp6 = new QLabel(tr("External/WAN IPv6:"), tab);
    labelWanIp6->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lineEdit_WANIP6 = new QLineEdit(tab);
    lineEdit_WANIP6->setPlaceholderText(tr("e.g. 2001:db8::1234"));
    formLayout->addRow(labelWanIp6, lineEdit_WANIP6);

    auto* labelBind6 = new QLabel(tr("Bind IPv6 address"), groupBox_5);
    labelBind6->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lineEdit_BIND_ADDRESS6 = new QLineEdit(groupBox_5);
    lineEdit_BIND_ADDRESS6->setPlaceholderText("::");
    gridLayout_12->addWidget(labelBind6, 1, 0);
    gridLayout_12->addWidget(lineEdit_BIND_ADDRESS6, 1, 1, 1, 3);

    auto *labelCountryDb = new QLabel(tr("Country MMDB file"), tab_3);
    labelCountryDb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lineEdit_COUNTRY_DB = new QLineEdit(tab_3);
    lineEdit_COUNTRY_DB->setPlaceholderText(tr("Path to GeoLite2/MaxMind country .mmdb"));
    lineEdit_COUNTRY_DB->setMinimumWidth(320);
    toolButton_COUNTRY_DB = new QToolButton(tab_3);
    toolButton_COUNTRY_DB->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFOLDER_BLUE));
    toolButton_COUNTRY_DB->setAutoRaise(false);
    gridLayout_11->addWidget(labelCountryDb, 2, 0);
    gridLayout_11->addWidget(lineEdit_COUNTRY_DB, 2, 1);
    gridLayout_11->addWidget(toolButton_COUNTRY_DB, 2, 2);
    connect(toolButton_COUNTRY_DB, &QToolButton::clicked, this, &SettingsConnection::slotBrowseCountryDb);

    gridLayout_13->removeWidget(lineEdit_DHT_BOOTSTRAP_URLS);
    lineEdit_DHT_BOOTSTRAP_URLS->hide();
    horizontalSpacer_3->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    horizontalLayout_2->setContentsMargins(12, 6, 12, 8);
    horizontalLayout_2->setSpacing(0);
    horizontalLayout_2->setStretch(0, 0);
    horizontalLayout_2->setStretch(1, 1);
    label_23->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label_DHT_BOOTSTRAP_URLS->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    gridLayout_13->setHorizontalSpacing(12);
    gridLayout_13->setVerticalSpacing(8);
    gridLayout_13->setColumnStretch(0, 0);
    gridLayout_13->setColumnStretch(1, 0);
    gridLayout_13->setColumnStretch(2, 1);
    auto *buttonEditDHT = new QPushButton(tr("Configure DHT bootstrap URLs"), groupBox_DHT);
    buttonEditDHT->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    gridLayout_13->addWidget(buttonEditDHT, 1, 1);
    connect(buttonEditDHT, &QPushButton::clicked, this, &SettingsConnection::slotCfgDHTBootstrap);

    auto *labelHubLists = new QLabel(tr("Public hub list URLs"), tab_3);
    labelHubLists->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *buttonEditHubLists = new QPushButton(tr("Configure public hub list URLs"), tab_3);
    buttonEditHubLists->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    gridLayout_11->addWidget(labelHubLists, 3, 0);
    gridLayout_11->addWidget(buttonEditHubLists, 3, 1);
    connect(buttonEditHubLists, &QPushButton::clicked, this, &SettingsConnection::slotCfgPublicHubs);

    checkBox_PROXY_P2P = new QCheckBox(tr("Proxy downloads and uploads too (passive mode)"), frame_2);
    checkBox_PROXY_P2P->setToolTip(tr("When enabled, peer-to-peer transfers use the selected proxy. "
                                      "Incoming connection options are disabled and the client is advertised as passive."));
    gridLayout_8->addWidget(checkBox_PROXY_P2P, 7, 0, 1, 4);

    comboBox_TOS->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    comboBox_TLS->setSizeAdjustPolicy(QComboBox::AdjustToContents);


    init();
}

bool SettingsConnection::eventFilter(QObject *obj, QEvent *e){
    if ((e->type() == QEvent::KeyRelease) || (e->type() == QEvent::MouseButtonRelease))//May be some settings has been changed
        dirty = true;

    return QWidget::eventFilter(obj, e);
}

void SettingsConnection::ok(){

    SettingsManager *SM = qtCtx()->dcCtx().getSettingsManager();
    const bool use_proxy = !radioButton_DC->isChecked();
    const bool proxyP2P = use_proxy && checkBox_PROXY_P2P && checkBox_PROXY_P2P->isChecked();
    const bool hubStealth = use_proxy && checkBox_SOCKS_STEALTH && checkBox_SOCKS_STEALTH->isChecked();
    const bool hubPassive = proxyP2P || hubStealth;
    bool active = !radioButton_PASSIVE->isChecked() && !hubPassive;

    int old_mode = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::INCOMING_CONNECTIONS, true);
    SM->set(SettingsManager::AUTO_DETECT_CONNECTION, checkBox_AUTO_DETECT_CONNECTION->isChecked() && !hubPassive);
    if (active){
        if (radioButton_ACTIVE->isChecked())
            SM->set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_DIRECT);
        else if (radioButton_PORT->isChecked())
            SM->set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_FIREWALL_NAT);
#if (defined USE_MINIUPNP)
        else
            SM->set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_FIREWALL_UPNP);
#endif
        SM->set(SettingsManager::TCP_PORT, spinBox_TCP->value());
        SM->set(SettingsManager::UDP_PORT, spinBox_UDP->value());

        if (spinBox_TLS->value() != qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TCP_PORT, true))
            SM->set(SettingsManager::TLS_PORT, spinBox_TLS->value());
        else
            SM->set(SettingsManager::TLS_PORT, spinBox_TLS->value()+1);

        SM->set(SettingsManager::EXTERNAL_IP, lineEdit_WANIP->text().toStdString());
        QString bind_ip=lineEdit_BIND_ADDRESS->text();
        if (validateIp4(bind_ip))
            SM->set(SettingsManager::BIND_ADDRESS, lineEdit_BIND_ADDRESS->text().toStdString());
        SM->set(SettingsManager::NO_IP_OVERRIDE, checkBox_DONTOVERRIDE->checkState() == Qt::Checked);

        const bool useIPv6 = checkBox_USE_IPV6 && checkBox_USE_IPV6->isChecked();
        QString wanIp6 = lineEdit_WANIP6 ? lineEdit_WANIP6->text().trimmed() : QString();
        QString bindIp6 = lineEdit_BIND_ADDRESS6 ? lineEdit_BIND_ADDRESS6->text().trimmed() : QString();
        if(bindIp6.isEmpty()) {
            bindIp6 = "::";
        }

        if(useIPv6) {
            if(!wanIp6.isEmpty() && !validateIp6(wanIp6)) {
                showMsg(tr("No valid external IPv6 address found!"), lineEdit_WANIP6);
                return;
            }
            if(!validateIp6(bindIp6)) {
                showMsg(tr("No valid bind IPv6 address found!"), lineEdit_BIND_ADDRESS6);
                return;
            }
        }

        SM->set(SettingsManager::EXTERNAL_IP6, wanIp6.toStdString());
        SM->set(SettingsManager::BIND_ADDRESS6, bindIp6.toStdString());
        SM->set(SettingsManager::USE_IPV6, useIPv6);
    }
    else {
        SM->set(SettingsManager::INCOMING_CONNECTIONS, SettingsManager::INCOMING_FIREWALL_PASSIVE);
        QString bind_ip=lineEdit_BIND_ADDRESS->text();
        if (validateIp4(bind_ip))
            SM->set(SettingsManager::BIND_ADDRESS, lineEdit_BIND_ADDRESS->text().toStdString());

        const bool useIPv6 = checkBox_USE_IPV6 && checkBox_USE_IPV6->isChecked();
        QString wanIp6 = lineEdit_WANIP6 ? lineEdit_WANIP6->text().trimmed() : QString();
        QString bindIp6 = lineEdit_BIND_ADDRESS6 ? lineEdit_BIND_ADDRESS6->text().trimmed() : QString();
        if(bindIp6.isEmpty()) {
            bindIp6 = "::";
        }

        if(useIPv6) {
            if(!wanIp6.isEmpty() && !validateIp6(wanIp6)) {
                showMsg(tr("No valid external IPv6 address found!"), lineEdit_WANIP6);
                return;
            }
            if(!validateIp6(bindIp6)) {
                showMsg(tr("No valid bind IPv6 address found!"), lineEdit_BIND_ADDRESS6);
                return;
            }
        }

        SM->set(SettingsManager::EXTERNAL_IP6, wanIp6.toStdString());
        SM->set(SettingsManager::BIND_ADDRESS6, bindIp6.toStdString());
        SM->set(SettingsManager::USE_IPV6, useIPv6);
    }

    const bool use_shadowsocks = radioButton_SHADOWSOCKS && radioButton_SHADOWSOCKS->isChecked();
    int type = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS, true);

    SM->set(SettingsManager::BIND_IFACE, radioButton_BIND_IFACE->isChecked());
    SM->set(SettingsManager::BIND_IFACE_NAME, _tq(comboBox_IFACES->currentText()));

    if (use_proxy){
        const QString server = lineEdit_SIP->text().trimmed();

        if (server.isEmpty()){
            showMsg(use_shadowsocks ? tr("No Shadowsocks server found!") : tr("No SOCKS5 server found!"), lineEdit_SIP);

            return;
        }

        int port = lineEdit_SPORT->text().toInt();
        if (port <= 0 || port > 65535) {
            showMsg(tr("No valid proxy port found!"), lineEdit_SPORT);

            return;
        }

        SM->set(SettingsManager::SOCKS_RESOLVE, checkBox_RESOLVE->checkState() == Qt::Checked);
        SM->set(SettingsManager::SOCKS_STEALTH, checkBox_SOCKS_STEALTH && checkBox_SOCKS_STEALTH->isChecked());
        SM->set(SettingsManager::PROXY_P2P_CONNECTIONS, proxyP2P);

        if (use_shadowsocks) {
            SM->set(SettingsManager::SHADOWSOCKS_SERVER, server.toStdString());
            SM->set(SettingsManager::SHADOWSOCKS_PASSWORD, lineEdit_SPSWD->text().toStdString());
            SM->set(SettingsManager::SHADOWSOCKS_METHOD, comboBox_SHADOWSOCKS_METHOD->currentData().toString().toStdString());
            SM->set(SettingsManager::SHADOWSOCKS_PORT, port);
            SM->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SHADOWSOCKS);
        } else {
            SM->set(SettingsManager::SOCKS_SERVER, server.toStdString());
            SM->set(SettingsManager::SOCKS_USER, lineEdit_SUSR->text().toStdString());
            SM->set(SettingsManager::SOCKS_PASSWORD, lineEdit_SPSWD->text().toStdString());
            SM->set(SettingsManager::SOCKS_PORT, port);
            SM->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_SOCKS5);
        }
    }
    else{
        SM->set(SettingsManager::OUTGOING_CONNECTIONS, SettingsManager::OUTGOING_DIRECT);
        SM->set(SettingsManager::PROXY_P2P_CONNECTIONS, false);
    }

    if (use_proxy || qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS, true) != type)
        Socket::socksUpdated(qtCtx()->dcCtx());

    SM->set(SettingsManager::THROTTLE_ENABLE, checkBox_THROTTLE_ENABLE->isChecked());
    SM->set(SettingsManager::TIME_DEPENDENT_THROTTLE, checkBox_TIME_DEPENDENT_THROTTLE->isChecked());
    SM->set(SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, spinBox_DOWN_LIMIT_NORMAL->value());
    SM->set(SettingsManager::MAX_UPLOAD_SPEED_MAIN, spinBox_UP_LIMIT_NORMAL->value());
    SM->set(SettingsManager::MAX_DOWNLOAD_SPEED_ALTERNATE, spinBox_DOWN_LIMIT_TIME->value());
    SM->set(SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE, spinBox_UP_LIMIT_TIME->value());
    SM->set(SettingsManager::BANDWIDTH_LIMIT_START, spinBox_BANDWIDTH_LIMIT_START->value());
    SM->set(SettingsManager::BANDWIDTH_LIMIT_END, spinBox_BANDWIDTH_LIMIT_END->value());
    SM->set(SettingsManager::SLOTS_ALTERNATE_LIMITING, spinBox_ALTERNATE_SLOTS->value());
    SM->set(SettingsManager::RECONNECT_DELAY, spinBox_RECONNECT_DELAY->value());
    SM->set(SettingsManager::IP_TOS_VALUE, comboBox_TOS->itemData(comboBox_TOS->currentIndex()).toInt());
    if (lineEdit_COUNTRY_DB)
        SM->set(SettingsManager::COUNTRY_DB_PATH, lineEdit_COUNTRY_DB->text().trimmed().toStdString());
    SM->set(SettingsManager::DYNDNS_SERVER, lineEdit_DYNDNS_SERVER->text().toStdString());
    SM->set(SettingsManager::DYNDNS_ENABLE, checkBox_DYNDNS->isChecked());
#ifdef WITH_DHT
    SM->set(SettingsManager::USE_DHT, groupBox_DHT->isChecked());
    if (spinBox_DHT->value() != qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::UDP_PORT, true))
        SM->set(SettingsManager::DHT_PORT, spinBox_DHT->value());
    else
        SM->set(SettingsManager::DHT_PORT, spinBox_DHT->value()+1);

    if (!(old_dht < 1024) && (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DHT_PORT, true) < 1024))
        showMsg(tr("Program need root privileges to open ports less than 1024"), nullptr);
#endif
    SM->set(SettingsManager::ALLOW_UNTRUSTED_CLIENTS, checkBox_UNTRUSTED_CLIENTS->isChecked());
    SM->set(SettingsManager::ALLOW_UNTRUSTED_HUBS, checkBox_UNTRUSTED_HUBS->isChecked());

    SM->set(SettingsManager::USE_TLS, (comboBox_TLS->currentIndex() == 1) || (comboBox_TLS->currentIndex() == 2));
    SM->set(SettingsManager::REQUIRE_TLS, (comboBox_TLS->currentIndex() == 2));

    if (old_mode != qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::INCOMING_CONNECTIONS, true) || old_tcp != (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TCP_PORT, true))
        || old_udp != (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::UDP_PORT, true)) || old_tls != (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TLS_PORT, true)))
    {
        if (!(old_tcp < 1024 || old_tls < 1024 || old_udp < 1024) &&
            (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TCP_PORT, true) < 1024 || qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::UDP_PORT, true) < 1024 || qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TLS_PORT, true) < 1024))
            showMsg(tr("Program need root privileges to open ports less than 1024"), nullptr);

        qtCtx()->mainWindow()->startSocket(true);
    }
}

void SettingsConnection::init(){
    lineEdit_WANIP->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::EXTERNAL_IP, true)));
    lineEdit_BIND_ADDRESS->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BIND_ADDRESS, true)));
    if (lineEdit_WANIP6)
        lineEdit_WANIP6->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::EXTERNAL_IP6, true)));
    if (lineEdit_BIND_ADDRESS6)
        lineEdit_BIND_ADDRESS6->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BIND_ADDRESS6, true)));
    if (checkBox_USE_IPV6)
        checkBox_USE_IPV6->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::USE_IPV6, true));

    spinBox_TCP->setValue(old_tcp = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TCP_PORT, true));
    spinBox_UDP->setValue(old_udp = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::UDP_PORT, true));
    spinBox_TLS->setValue(old_tls = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::TLS_PORT, true));
    checkBox_AUTO_DETECT_CONNECTION->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::AUTO_DETECT_CONNECTION, true));
    checkBox_THROTTLE_ENABLE->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::THROTTLE_ENABLE, true));
    checkBox_TIME_DEPENDENT_THROTTLE->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::TIME_DEPENDENT_THROTTLE, true));
    spinBox_DOWN_LIMIT_NORMAL->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MAX_DOWNLOAD_SPEED_MAIN, true));
    spinBox_UP_LIMIT_NORMAL->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MAX_UPLOAD_SPEED_MAIN, true));
    spinBox_DOWN_LIMIT_TIME->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MAX_DOWNLOAD_SPEED_ALTERNATE, true));
    spinBox_UP_LIMIT_TIME->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE, true));
    spinBox_BANDWIDTH_LIMIT_START->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BANDWIDTH_LIMIT_START, true));
    spinBox_BANDWIDTH_LIMIT_END->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BANDWIDTH_LIMIT_END, true));
    spinBox_ALTERNATE_SLOTS->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::SLOTS_ALTERNATE_LIMITING, true));
    spinBox_RECONNECT_DELAY->setValue(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::RECONNECT_DELAY, true));
    if (lineEdit_COUNTRY_DB)
        lineEdit_COUNTRY_DB->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::COUNTRY_DB_PATH, true)));
    checkBox_DONTOVERRIDE->setCheckState( qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::NO_IP_OVERRIDE, true)? Qt::Checked : Qt::Unchecked );
    checkBox_DYNDNS->setCheckState( qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::DYNDNS_ENABLE, true) ? Qt::Checked : Qt::Unchecked );
    lineEdit_DYNDNS_SERVER->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DYNDNS_SERVER, true)));
#ifdef WITH_DHT
    groupBox_DHT->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::USE_DHT, true));
    spinBox_DHT->setValue(old_dht = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::DHT_PORT, true));
#else
    groupBox_DHT->hide();
#endif

    checkBox_UNTRUSTED_CLIENTS->setChecked(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::ALLOW_UNTRUSTED_CLIENTS, true));
    checkBox_UNTRUSTED_HUBS->setChecked(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::ALLOW_UNTRUSTED_HUBS, true));

    comboBox_TLS->setCurrentIndex(0);
    if (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::USE_TLS, true)) {
        comboBox_TLS->setCurrentIndex(1);
    }
    if (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::REQUIRE_TLS, true)) {
        comboBox_TLS->setCurrentIndex(2);
    }

    QStringList ifaces = qtCtx()->wulforUtil()->getLocalIfaces();

    if (!ifaces.isEmpty())
        comboBox_IFACES->addItems(ifaces);

    comboBox_IFACES->addItem("");

    if (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BIND_IFACE, true)){
        radioButton_BIND_IFACE->toggle();

        if (ifaces.contains(_q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BIND_IFACE_NAME, true))))
            comboBox_IFACES->setCurrentIndex(ifaces.indexOf(_q(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::BIND_IFACE_NAME, true))));
        else
            comboBox_IFACES->setCurrentIndex(comboBox_IFACES->count()-1);
    }
    else
        radioButton_BIND_ADDR->toggle();

    switch (qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::INCOMING_CONNECTIONS, true)){
    case SettingsManager::INCOMING_DIRECT:
        {
            radioButton_ACTIVE->setChecked(true);

            break;
        }
    case SettingsManager::INCOMING_FIREWALL_NAT:
        {
            radioButton_PORT->setChecked(true);

            break;
        }
    case SettingsManager::INCOMING_FIREWALL_PASSIVE:
        {
            radioButton_PASSIVE->setChecked(true);

            break;
        }
#if (defined USE_MINIUPNP)
    case SettingsManager::INCOMING_FIREWALL_UPNP:
        {
            radioButton_UPNP->setChecked(true);

            break;
        }
#endif
    }
#if (!defined USE_MINIUPNP)
    radioButton_UPNP->setEnabled(false);
#endif

    if(comboBox_SHADOWSOCKS_METHOD && comboBox_SHADOWSOCKS_METHOD->count() == 0) {
        comboBox_SHADOWSOCKS_METHOD->addItem(QStringLiteral("aes-256-gcm"), QStringLiteral("aes-256-gcm"));
        comboBox_SHADOWSOCKS_METHOD->addItem(QStringLiteral("aes-128-gcm"), QStringLiteral("aes-128-gcm"));
        comboBox_SHADOWSOCKS_METHOD->addItem(QStringLiteral("chacha20-ietf-poly1305"), QStringLiteral("chacha20-ietf-poly1305"));
    }

    const int outgoingMode = qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::OUTGOING_CONNECTIONS, true);
    const bool shadowsocksMode = outgoingMode == SettingsManager::OUTGOING_SHADOWSOCKS;

    lineEdit_SIP->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(
        shadowsocksMode ? SettingsManager::SHADOWSOCKS_SERVER : SettingsManager::SOCKS_SERVER, true)));
    lineEdit_SUSR->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::SOCKS_USER, true)));
    lineEdit_SPORT->setText(QString().setNum(qtCtx()->dcCtx().getSettingsManager()->get(
        shadowsocksMode ? SettingsManager::SHADOWSOCKS_PORT : SettingsManager::SOCKS_PORT, true)));
    lineEdit_SPSWD->setText(QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(
        shadowsocksMode ? SettingsManager::SHADOWSOCKS_PASSWORD : SettingsManager::SOCKS_PASSWORD, true)));

    checkBox_RESOLVE->setCheckState( qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::SOCKS_RESOLVE, true)? Qt::Checked : Qt::Unchecked );
    if(checkBox_SOCKS_STEALTH)
        checkBox_SOCKS_STEALTH->setChecked(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::SOCKS_STEALTH, true));
    if(checkBox_PROXY_P2P)
        checkBox_PROXY_P2P->setChecked(qtCtx()->dcCtx().getSettingsManager()->getBool(SettingsManager::PROXY_P2P_CONNECTIONS, true));

    const QString shadowsocksMethod = QString::fromStdString(qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::SHADOWSOCKS_METHOD, true));
    const int methodIndex = comboBox_SHADOWSOCKS_METHOD->findData(shadowsocksMethod);
    comboBox_SHADOWSOCKS_METHOD->setCurrentIndex(methodIndex >= 0 ? methodIndex : 0);

    switch (outgoingMode){
    case SettingsManager::OUTGOING_DIRECT:
        {
            radioButton_DC->toggle();

            break;
        }
    case SettingsManager::OUTGOING_SOCKS5:
        {
            radioButton_SOCKS->toggle();

            break;
        }
    case SettingsManager::OUTGOING_SHADOWSOCKS:
        {
            radioButton_SHADOWSOCKS->toggle();

            break;
        }
    }

    comboBox_TOS->setItemData(0, -1);
    comboBox_TOS->setItemData(1, IPTOS_LOWDELAY);
    comboBox_TOS->setItemData(2, IPTOS_THROUGHPUT);
    comboBox_TOS->setItemData(3, IPTOS_RELIABILITY);
    comboBox_TOS->setItemData(4, IPTOS_MINCOST);

    for (int i = 0; i < comboBox_TOS->count(); i++){
        if (comboBox_TOS->itemData(i).toInt() == qtCtx()->dcCtx().getSettingsManager()->get(SettingsManager::IP_TOS_VALUE, true)){
            comboBox_TOS->setCurrentIndex(i);

            break;
        }
    }

    slotToggleIncomming();
    slotToggleOutgoing();

    connect(radioButton_ACTIVE, &QRadioButton::toggled, this, &SettingsConnection::slotToggleIncomming);
    connect(radioButton_PORT, &QRadioButton::toggled, this, &SettingsConnection::slotToggleIncomming);
    connect(radioButton_PASSIVE, &QRadioButton::toggled, this, &SettingsConnection::slotToggleIncomming);
#if (defined USE_MINIUPNP)
    connect(radioButton_UPNP, &QRadioButton::toggled, this, &SettingsConnection::slotToggleIncomming);
#endif
    connect(radioButton_DC, &QRadioButton::toggled, this, &SettingsConnection::slotToggleOutgoing);
    connect(radioButton_SOCKS, &QRadioButton::toggled, this, &SettingsConnection::slotToggleOutgoing);
    connect(radioButton_SHADOWSOCKS, &QRadioButton::toggled, this, &SettingsConnection::slotToggleOutgoing);
    if(checkBox_PROXY_P2P)
        connect(checkBox_PROXY_P2P, &QCheckBox::toggled, this, &SettingsConnection::slotToggleOutgoing);
    if(checkBox_SOCKS_STEALTH)
        connect(checkBox_SOCKS_STEALTH, &QCheckBox::toggled, this, &SettingsConnection::slotToggleOutgoing);
    if(checkBox_USE_IPV6 && lineEdit_WANIP6 && lineEdit_BIND_ADDRESS6) {
        auto syncIpv6Fields = [this]() {
            const bool enabled = checkBox_USE_IPV6->isChecked();
            lineEdit_WANIP6->setEnabled(enabled);
            lineEdit_BIND_ADDRESS6->setEnabled(enabled);
        };
        connect(checkBox_USE_IPV6, &QCheckBox::toggled, this, [syncIpv6Fields](bool) { syncIpv6Fields(); });
        syncIpv6Fields();
    }

    lineEdit_SIP->installEventFilter(this);
    lineEdit_SPORT->installEventFilter(this);
    lineEdit_SPSWD->installEventFilter(this);
    lineEdit_SUSR->installEventFilter(this);
    comboBox_SHADOWSOCKS_METHOD->installEventFilter(this);
    lineEdit_WANIP->installEventFilter(this);
    if (lineEdit_WANIP6)
        lineEdit_WANIP6->installEventFilter(this);
    if (lineEdit_BIND_ADDRESS6)
        lineEdit_BIND_ADDRESS6->installEventFilter(this);
    if (checkBox_USE_IPV6)
        checkBox_USE_IPV6->installEventFilter(this);
    if (lineEdit_COUNTRY_DB)
        lineEdit_COUNTRY_DB->installEventFilter(this);

    spinBox_TCP->installEventFilter(this);
    spinBox_UDP->installEventFilter(this);
    spinBox_TLS->installEventFilter(this);

    radioButton_ACTIVE->installEventFilter(this);
    radioButton_DC->installEventFilter(this);
    radioButton_PASSIVE->installEventFilter(this);
    radioButton_PORT->installEventFilter(this);
    radioButton_SOCKS->installEventFilter(this);
    radioButton_SHADOWSOCKS->installEventFilter(this);
#if (defined USE_MINIUPNP)
    radioButton_UPNP->installEventFilter(this);
#endif
    checkBox_DONTOVERRIDE->installEventFilter(this);
    checkBox_RESOLVE->installEventFilter(this);
    checkBox_SOCKS_STEALTH->installEventFilter(this);
    if (checkBox_PROXY_P2P)
        checkBox_PROXY_P2P->installEventFilter(this);
}

void SettingsConnection::slotToggleIncomming(){
    const bool hubPassive = isProxyP2PMode() || isProxyHubStealthMode();
    if(hubPassive && !radioButton_PASSIVE->isChecked())
        radioButton_PASSIVE->setChecked(true);

    bool b = !radioButton_PASSIVE->isChecked() && !hubPassive;

    frame->setEnabled(b);
    checkBox_AUTO_DETECT_CONNECTION->setEnabled(!hubPassive);
    radioButton_ACTIVE->setEnabled(!hubPassive);
    radioButton_PORT->setEnabled(!hubPassive);
    radioButton_PASSIVE->setEnabled(!hubPassive);
#if (defined USE_MINIUPNP)
    radioButton_UPNP->setEnabled(!hubPassive);
#endif
    groupBox_5->setEnabled(true);
}

void SettingsConnection::slotToggleOutgoing(){
    const bool proxy = !radioButton_DC->isChecked();
    const bool socks = proxy && radioButton_SOCKS && radioButton_SOCKS->isChecked();
    const bool shadowsocks = proxy && radioButton_SHADOWSOCKS && radioButton_SHADOWSOCKS->isChecked();

    frame_2->setEnabled(true);
    if(label_5)
        label_5->setEnabled(proxy);
    if(lineEdit_SIP)
        lineEdit_SIP->setEnabled(proxy);
    if(label_6)
        label_6->setEnabled(proxy);
    if(lineEdit_SPORT)
        lineEdit_SPORT->setEnabled(proxy);
    if(label_7)
        label_7->setEnabled(socks);
    if(lineEdit_SUSR)
        lineEdit_SUSR->setEnabled(socks);
    if(label_8)
        label_8->setEnabled(proxy);
    if(lineEdit_SPSWD)
        lineEdit_SPSWD->setEnabled(proxy);
    if(label_SHADOWSOCKS_METHOD)
        label_SHADOWSOCKS_METHOD->setEnabled(shadowsocks);
    if(comboBox_SHADOWSOCKS_METHOD)
        comboBox_SHADOWSOCKS_METHOD->setEnabled(shadowsocks);
    if(checkBox_RESOLVE)
        checkBox_RESOLVE->setEnabled(proxy);
    if(checkBox_SOCKS_STEALTH)
        checkBox_SOCKS_STEALTH->setEnabled(proxy);
    if(checkBox_PROXY_P2P)
        checkBox_PROXY_P2P->setEnabled(proxy);

    slotToggleIncomming();
}

bool SettingsConnection::isProxyP2PMode() const {
    return radioButton_DC && !radioButton_DC->isChecked() &&
           checkBox_PROXY_P2P && checkBox_PROXY_P2P->isChecked();
}

bool SettingsConnection::isProxyHubStealthMode() const {
    return radioButton_DC && !radioButton_DC->isChecked() &&
           checkBox_SOCKS_STEALTH && checkBox_SOCKS_STEALTH->isChecked();
}

void SettingsConnection::slotCfgDHTBootstrap(){
    DHTBootstrapList dhtList(this);
    dhtList.exec();
}

void SettingsConnection::slotCfgPublicHubs(){
    PublicHubsList hubsList(this);
    hubsList.exec();
}

void SettingsConnection::slotBrowseCountryDb()
{
    const QString startPath = lineEdit_COUNTRY_DB && !lineEdit_COUNTRY_DB->text().trimmed().isEmpty()
        ? lineEdit_COUNTRY_DB->text().trimmed()
        : QDir::homePath();
    const QString file = QFileDialog::getOpenFileName(this,
                                                      tr("Select MaxMind country database"),
                                                      startPath,
                                                      tr("MaxMind DB (*.mmdb);;All files (*.*)"));

    if (!file.isEmpty() && lineEdit_COUNTRY_DB)
        lineEdit_COUNTRY_DB->setText(QDir::toNativeSeparators(file));
}

bool SettingsConnection::validateIp4(QString &ip){
    if (ip.isEmpty() || ip.isNull())
        return false;

    QStringList l = ip.split(".", Qt::SkipEmptyParts);

    if (l.size() != 4)
        return false;

    QIntValidator v(0, 255, this);

    bool valid = true;
    int pos = 0;

    for (QString s : l)
        valid = valid && (v.validate(s, pos) == QValidator::Acceptable);

    return valid;
}

bool SettingsConnection::validateIp6(QString& ip) const {
    if(ip.isEmpty() || ip.isNull()) {
        return false;
    }

    QHostAddress addr;
    if(!addr.setAddress(ip)) {
        return false;
    }

    return addr.protocol() == QAbstractSocket::IPv6Protocol;
}

void SettingsConnection::showMsg(QString msg, QWidget *focusTo){
    QMessageBox::warning(this, tr("Warning"), msg);

    if (focusTo)
        focusTo->setFocus();
}
