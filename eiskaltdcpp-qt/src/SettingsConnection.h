/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QWidget>
#include <QIntValidator>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QToolButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>

#include "ui_UISettingsConnection.h"
#include "SettingsInterface.h"
#include "SettingsConnectionHelpers.h"

class SettingsConnection :
        public QWidget,
        private Ui::UISettingsConnection
{
    Q_OBJECT
public:
    SettingsConnection(QWidget* = nullptr);

public slots:
    void ok();

protected:
    virtual bool eventFilter(QObject*, QEvent*);

private slots:
    void slotToggleIncomming();
    void slotToggleOutgoing();
    void slotCfgDHTBootstrap();
    void slotCfgPublicHubs();
    void slotBrowseCountryDb();
    void slotTestProxy();
    void slotTestHubListProxy();

private:
    void init();

    bool validateIp4(QString&);
    bool validateIp6(QString&) const;
    void showMsg(QString, QWidget* = nullptr);
    bool isProxyP2PMode() const;
    bool isProxyHubStealthMode() const;
    QString connectionModeText(int mode) const;
    void updateAutoDetectStatus();
    QString bindAddressText() const;
    QString bindAddress6Text() const;
    void populateBindAddressCombos(const QString& bind4, const QString& bind6);
    int selectedProxyFormMode() const;
    settings_connection::ProxyUiState visibleProxyFormState() const;
    void applyProxyFormState(const settings_connection::ProxyUiState& state);
    void syncProxyFormStateWithSelection();
    void saveCurrentProxyFormState();

    bool dirty;
    QCheckBox* checkBox_USE_IPV6 = nullptr;
    QCheckBox* checkBox_PROXY_P2P = nullptr;
    QCheckBox* checkBox_SOCKS_TLS = nullptr;
    QLabel* label_AUTO_DETECT_STATUS = nullptr;
    QLabel* label_WANIP6 = nullptr;
    QLabel* label_BIND_ADDRESS6 = nullptr;
    QLabel* label_SHADOWSOCKS_TRANSPORT = nullptr;
    QLabel* label_HUBLIST_PROXY_HOST = nullptr;
    QLabel* label_HUBLIST_PROXY_PORT = nullptr;
    QGroupBox* groupBox_HUBLIST_PROXY = nullptr;
    QComboBox* comboBox_BIND_ADDRESS = nullptr;
    QComboBox* comboBox_BIND_ADDRESS6 = nullptr;
    QComboBox* comboBox_SHADOWSOCKS_TRANSPORT = nullptr;
    QLineEdit* lineEdit_WANIP6 = nullptr;
    QLineEdit* lineEdit_BIND_ADDRESS6 = nullptr;
    QLineEdit* lineEdit_HUBLIST_PROXY_HOST = nullptr;
    QLineEdit* lineEdit_HUBLIST_PROXY_PORT = nullptr;
    QLineEdit *lineEdit_COUNTRY_DB = nullptr;
    QToolButton *toolButton_COUNTRY_DB = nullptr;
    QPushButton* button_TEST_PROXY = nullptr;
    QPushButton* button_TEST_HUBLIST_PROXY = nullptr;
    settings_connection::ProxyUiState socksProxyState;
    settings_connection::ProxyUiState shadowsocksProxyState;
    int currentProxyFormMode = settings_connection::ProxyUiDirect;

    int old_tcp, old_udp, old_tls
#ifdef WITH_DHT
        , old_dht
#endif
        ;
};
