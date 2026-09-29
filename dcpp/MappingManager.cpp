/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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
 */

#include "stdinc.h"

#include "MappingManager.h"

#include "ConnectionManager.h"
#include "SearchManager.h"
#include "LogManager.h"
#include "format.h"
#include "version.h"
#include "ConnectivityManager.h"
#ifdef USE_MINIUPNP
#include "extra/upnpc.h"
#endif
#ifdef WITH_DHT
#include "dht/DHT.h"
#endif
namespace dcpp {

void MappingManager::addImplementation(UPnP* impl) {
    impls.push_back(std::unique_ptr<UPnP>(impl));
}

bool MappingManager::open() {
    if(CTX_SETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_GOST)
        return false;
    if(opened)
        return false;

#ifdef USE_MINIUPNP
    if(impls.empty()) {
        runMiniUPnP();
    }
#endif

    if(impls.empty()) {
        log(_("No UPnP implementation available"));
        return false;
    }

    if(portMapping.exchange(true) == true) {
        log(_("Another UPnP port mapping attempt is in progress..."));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(renewalMutex);
        stopping = false;
    }
    try {
        start();
    } catch(...) {
        portMapping = false;
        throw;
    }

    return true;
}

void MappingManager::close() {
    {
        std::lock_guard<std::mutex> lock(renewalMutex);
        stopping = true;
    }
    renewalWake.notify_all();
    join(); // Never delete mappings while the worker is adding or renewing them.
    for(auto &i : impls) {
        close(*i);
    }
    opened = false;
    portMapping = false;
}

int MappingManager::run() {
    // cache these
    const string
            conn_port = ctx().getConnectionManager()->getPort(),
            secure_port = ctx().getConnectionManager()->getSecurePort(),
            search_port = ctx().getSearchManager()->getPort();
#ifdef WITH_DHT
    const string dht_port = ctx().getDHT()->getPort();
#endif

    for(auto &i : impls) {
        UPnP& impl = *i;

        if(isStopping())
            break;

        close(impl);

        if(!impl.init()){
            log(str(F_("Failed to initialize the %1% interface") % impl.getName()));
            continue;
        }

        if(isStopping())
            break;
        if(!conn_port.empty() && !impl.open(conn_port, UPnP::PROTOCOL_TCP, str(F_(APPNAME " Transfer Port (%1% TCP)") % conn_port))){
            log(str(F_("The %1% interface has failed to map the %2% %3% port") % impl.getName() % "TCP" % conn_port));
            close(impl);
            continue;
        }

        if(isStopping())
            break;
        if(!secure_port.empty() && !impl.open(secure_port, UPnP::PROTOCOL_TCP, str(F_(APPNAME " Encrypted Transfer Port (%1% TCP)") % secure_port))){
            log(str(F_("The %1% interface has failed to map the %2% %3% port") % impl.getName() % "TLS" % secure_port));
            close(impl);
            continue;
        }

        if(isStopping())
            break;
        if(!search_port.empty() && !impl.open(search_port, UPnP::PROTOCOL_UDP, str(F_(APPNAME " Search Port (%1% UDP)") % search_port))){
            log(str(F_("The %1% interface has failed to map the %2% %3% port") % impl.getName() % "UDP" % search_port));
            close(impl);
            continue;
        }
#ifdef WITH_DHT
        if(isStopping())
            break;
        if(!dht_port.empty() && !impl.open(dht_port, UPnP::PROTOCOL_UDP, str(F_(APPNAME " DHT Port (%1% UDP)") % dht_port))){
            log(str(F_("The %1% interface has failed to map the %2% %3% port") % impl.getName() % "UDP" % dht_port));
            close(impl);
            continue;
        }
#endif

        if(isStopping())
            break;
        opened = true;

#ifdef WITH_DHT
        if(!dht_port.empty())
            log(str(F_("Successfully created port mappings (TCP: %1%, UDP: %2%, TLS: %3%, DHT: %4%), mapped using the %5% interface") % conn_port % search_port % secure_port % dht_port % impl.getName()));
        else
            log(str(F_("Successfully created port mappings (TCP: %1%, UDP: %2%, TLS: %3%), mapped using the %4% interface") % conn_port % search_port % secure_port % impl.getName()));
#else
        log(str(F_("Successfully created port mappings (TCP: %1%, UDP: %2%, TLS: %3%), mapped using the %4% interface") % conn_port % search_port % secure_port % impl.getName()));
#endif

        if(!CTX_BOOLSETTING(NO_IP_OVERRIDE)) {
            // now lets configure the external IP (connect to me) address
            string ExternalIP = impl.getExternalIP();
            if(!ExternalIP.empty()) {
                // woohoo, we got the external IP from the UPnP framework
                ctx().getSettingsManager()->set(SettingsManager::EXTERNAL_IP, ExternalIP);
            } else {
                //:-( Looks like we have to rely on the user setting the external IP manually
                // no need to do cleanup here because the mappings work
                log(_("Failed to get external IP"));
            }
        }

        if(isStopping())
            break;
        ctx().getConnectivityManager()->mappingFinished(true);

        maintainMappings(impl);

        break;
    }

    if(!opened && !isStopping()) {
        log(_("Failed to create port mappings"));
        ctx().getConnectivityManager()->mappingFinished(false);
    }
    portMapping = false;
    return 0;
}

bool MappingManager::isStopping() {
    std::lock_guard<std::mutex> lock(renewalMutex);
    return stopping;
}

void MappingManager::maintainMappings(UPnP& impl) {
    bool healthy = true;
    std::unique_lock<std::mutex> lock(renewalMutex);
    while(!renewalWake.wait_for(lock, std::chrono::seconds(healthy ? 300 : 60),
                              [this] { return stopping; })) {
        lock.unlock();
        // AddPortMapping refreshes existing leases and recreates lost ones;
        // deleting them first would interrupt otherwise healthy connections.
        const auto cancelled = [this] { return isStopping(); };
        bool renewed = impl.renew(cancelled);
        if(!renewed && !cancelled())
            renewed = impl.init() && impl.renew(cancelled);
        if(cancelled()) {
            lock.lock();
            break;
        }
        if(renewed) {
            log(healthy ? _("Port mappings renewed") : _("Port mappings restored"));
        } else {
            log(_("Port mapping renewal failed; retrying in 60 seconds"));
        }
        healthy = renewed;
        lock.lock();
    }
}

void MappingManager::close(UPnP& impl) {
    if(impl.hasRules()) {
        log(impl.close() ? str(F_("Successfully removed port mappings with the %1% interface") % impl.getName()) :
                           str(F_("Failed to remove port mappings with the %1% interface") % impl.getName()));
    }
}

void MappingManager::log(const string& message) {
    ctx().getConnectivityManager()->log(str(F_("UPnP: %1%") % message));
}

#ifdef USE_MINIUPNP
void MappingManager::runMiniUPnP() {
#ifdef USE_MINIUPNP
    addImplementation(new UPnPc(ctx()));
#endif
}
#endif
} // namespace dcpp
