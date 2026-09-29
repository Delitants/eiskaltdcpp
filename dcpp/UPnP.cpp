/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#include "UPnP.h"

namespace dcpp {

const char* UPnP::protocols[PROTOCOL_LAST] = {
    "TCP",
    "UDP"
};

bool UPnP::open(const string& port, const Protocol protocol, const string& description) {
    if(!add(port, protocol, description))
        return false;

    for(auto& existing : rules) {
        if(existing.port == port && existing.protocol == protocol) {
            existing.description = description;
            return true;
        }
    }
    rules.push_back({port, protocol, description});
    return true;
}

bool UPnP::renew(const std::function<bool()>& cancelled) {
    bool success = true;
    for(const auto& entry : rules) {
        if(cancelled && cancelled())
            return false;
        success = add(entry.port, entry.protocol, entry.description) && success;
    }
    return success;
}

bool UPnP::close() {
    bool ret = true;

    for(std::vector<rule>::const_iterator i = rules.begin(), iend = rules.end(); i != iend; ++i)
        ret &= remove(i->port, i->protocol);
    rules.clear();

    return ret;
}

bool UPnP::hasRules() const {
    return !rules.empty();
}
} // namespace dcpp
