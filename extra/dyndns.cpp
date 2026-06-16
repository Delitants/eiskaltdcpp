//      Copyright 2011 Eugene Petrov <dhamp@ya.ru>
//      Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; either version 2 of the License, or
//      (at your option) any later version.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//
//      You should have received a copy of the GNU General Public License
//      along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include "dyndns.h"
#include "dcpp/SettingsManager.h"
#include "dcpp/ClientManager.h"
#include "dcpp/DCPlusPlus.h"
#include "dcpp/Util.h"

namespace dcpp {

DynDNS::DynDNS(DCContext& ctx) :
    ContextAware(ctx),
    request(false),
    minutesCounter(0),
    httpConnection(this->ctx())
{
    httpConnection.addListener(this);
}

DynDNS::~DynDNS() {
    httpConnection.removeListener(this);
}

void DynDNS::load()
{
    request = true;
    minutesCounter = 0;
    Request();
}

void DynDNS::stop()
{
    request = false;
}

void DynDNS::Request() {
    if (CTX_BOOLSETTING(DYNDNS_ENABLE)) {
        html.clear();
        if(CTX_SETTING(OUTGOING_CONNECTIONS) != SettingsManager::OUTGOING_DIRECT) {
            ctx().getSettingsManager()->unset(SettingsManager::INTERNETIP);
        }

        string tmps = CTX_SETTING(DYNDNS_SERVER);
        if (tmps.compare(0, 7, "http://") != 0 &&
                tmps.compare(0, 8, "https://") != 0) {
            tmps = "http://" + CTX_SETTING(DYNDNS_SERVER);
        }
        httpConnection.downloadFile(tmps);
    }
}

void DynDNS::on(TimerManagerListener::Minute, uint64_t) noexcept {
    ++minutesCounter;
    if (minutesCounter < 2) {
        return;
    }
    else {
        minutesCounter = 0;
    }

    if (request) {
        Request();
    }
}

void DynDNS::on(HttpConnectionListener::Data, HttpConnection*, const uint8_t* buf, size_t len) noexcept {
    html += string((const char*)buf, len);
}

void DynDNS::on(HttpConnectionListener::Complete, HttpConnection*, string const&) noexcept {
    request = false;
    string internetIP = Util::firstPublicIpFromText(html);

    if (!internetIP.empty()) {
        ctx().getSettingsManager()->set(SettingsManager::INTERNETIP, internetIP);
        Client::List clients = ctx().getClientManager()->getClients();

        for(auto c : clients) {
            if(c->isConnected()) {
                c->reloadSettings(false);
            }
        }
    }
    request = true;
}

void DynDNS::on(HttpConnectionListener::Failed, HttpConnection*, const string&) noexcept {
    if (request) {
        Request();
    }
}

}
