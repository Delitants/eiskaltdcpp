/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2019 Boris Pek <tehnick-8@yandex.ru>
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, compiling, linking, and/or
 * using OpenSSL with this program is allowed.
 *
 * This program uses the MiniUPnP client library by Thomas Bernard
 * http://miniupnp.free.fr https://miniupnp.tuxfamily.org/
 */

#include "upnpc.h"
#include "dcpp/Util.h"
#include "dcpp/SettingsManager.h"
#ifndef STATICLIB
#define STATICLIB
#endif
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#include "dcpp/DCContext.h"

struct UPnPc::Gateway {
    UPNPUrls urls{};
    IGDdatas data{};
    char lanAddress[64]{};
    ~Gateway() { FreeUPNPUrls(&urls); }
};

UPnPc::UPnPc(dcpp::DCContext& ctx) : ctx_(ctx), gateway(new Gateway) {}
UPnPc::~UPnPc() = default;
const std::string UPnPc::name = "MiniUPnP";

using namespace std;
using namespace dcpp;

bool UPnPc::init()
{
    auto discovered = std::make_unique<Gateway>();
    auto* sm = ctx_.getSettingsManager();
    const string bind_address = sm->get(SettingsManager::BIND_ADDRESS, true);
    const char *multicast_interface = (bind_address.empty() || bind_address == "0.0.0.0") ? nullptr : bind_address.c_str();

#if (MINIUPNPC_API_VERSION >= 14)
    UPNPDev *devices = upnpDiscover(5000, multicast_interface, nullptr, 0, 0, 2, nullptr);
#else
    UPNPDev *devices = upnpDiscover(5000, multicast_interface, nullptr, 0, 0, nullptr);
#endif

    if (!devices)
        return false;

#if (MINIUPNPC_API_VERSION >= 18)
    const int ret = UPNP_GetValidIGD(devices, &discovered->urls, &discovered->data,
        discovered->lanAddress, sizeof(discovered->lanAddress), nullptr, 0);
#else
    const int ret = UPNP_GetValidIGD(devices, &discovered->urls, &discovered->data,
        discovered->lanAddress, sizeof(discovered->lanAddress));
#endif

    freeUPNPDevlist(devices);

#if (MINIUPNPC_API_VERSION >= 18)
    // API 18 introduced the private-WAN result before naming the constants.
    const bool connected = ret == 1 || ret == 2;
#else
    const bool connected = ret == 1;
#endif
    if(!connected || !discovered->urls.controlURL || !discovered->lanAddress[0])
        return false;
    gateway = std::move(discovered);
    return true;
}

bool UPnPc::add(const string& port, const UPnP::Protocol protocol, const string& description)
{
    if(!gateway->urls.controlURL)
        return false;
    auto addWithLease = [&](const char* lease) {
        return UPNP_AddPortMapping(gateway->urls.controlURL, gateway->data.first.servicetype,
            port.c_str(), port.c_str(), gateway->lanAddress, description.c_str(),
            protocols[protocol], nullptr, lease);
    };
    int result = addWithLease("3600");
    if(result == 725) // Older routers accept only permanent mappings.
        result = addWithLease("0");
    return result == UPNPCOMMAND_SUCCESS;
}

bool UPnPc::remove(const string& port, const UPnP::Protocol protocol)
{
    if(!gateway->urls.controlURL)
        return false;
    return UPNP_DeletePortMapping(gateway->urls.controlURL, gateway->data.first.servicetype, port.c_str(),
        protocols[protocol], nullptr) == UPNPCOMMAND_SUCCESS;
}

string UPnPc::getExternalIP()
{
    char buf[16] = { 0 };
    if (gateway->urls.controlURL && UPNP_GetExternalIPAddress(gateway->urls.controlURL, gateway->data.first.servicetype, buf) == UPNPCOMMAND_SUCCESS)
        return string(buf);
    return Util::emptyString;
}
