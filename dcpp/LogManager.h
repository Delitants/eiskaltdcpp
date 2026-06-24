/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2009-2019 EiskaltDC++ developers
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

#pragma once

#include "typedefs.h"
#include "CriticalSection.h"
#include "DCContext.h"
#include "Speaker.h"
#include "LogManagerListener.h"
#include "DCPlusPlus.h"

#include <cstdint>
#include <ctime>
#include <deque>

namespace dcpp {

enum class LogArea : uint8_t {
    CHAT,
    PM,
    DOWNLOAD,
    FINISHED_DOWNLOAD,
    UPLOAD,
    SYSTEM,
    STATUS,
    SPY,
    CMD_DEBUG,
    LAST
};

struct LogEntry {
    uint64_t sequence;
    time_t timestamp;
    LogArea area;
    string message;
};

class LogManager : public Speaker<LogManagerListener>, public ContextAware
{
public:
    typedef pair<time_t, string> Pair;
    typedef std::deque<Pair> List;
    typedef std::deque<LogEntry> EntryList;

    using Area = LogArea;
    static constexpr Area CHAT = Area::CHAT;
    static constexpr Area PM = Area::PM;
    static constexpr Area DOWNLOAD = Area::DOWNLOAD;
    static constexpr Area FINISHED_DOWNLOAD = Area::FINISHED_DOWNLOAD;
    static constexpr Area UPLOAD = Area::UPLOAD;
    static constexpr Area SYSTEM = Area::SYSTEM;
    static constexpr Area STATUS = Area::STATUS;
    static constexpr Area SPY = Area::SPY;
    static constexpr Area CMD_DEBUG = Area::CMD_DEBUG;
    static constexpr size_t LAST = static_cast<size_t>(Area::LAST);
    enum { FILE, FORMAT };

    void log(Area area, ParamMap& params, bool writeToFile);
    void message(const string& msg);

    List getLastLogs();
    EntryList getLiveEntries() const;
    uint64_t clearLiveEntries();
    string getPath(Area area, ParamMap& params) const;
    string getPath(Area area) const;

    const string& getSetting(int area, int sel) const;
    const string& getSetting(Area area, int sel) const;
    void saveSetting(int area, int sel, const string& setting);
    void saveSetting(Area area, int sel, const string& setting);

private:
    void write(const string& path, const string& message);


public:
    explicit LogManager(DCContext& ctx);
    virtual ~LogManager();

private:
    static constexpr size_t MAX_LIVE_ENTRIES = 5000;

    mutable CriticalSection cs;
    List lastLogs;
    EntryList liveEntries;
    uint64_t nextSequence = 0;

    int options[LAST][2];
};

#define CTX_LOG(area, msg, writeToFile) this->ctx().getLogManager()->log(area, msg, writeToFile)

} // namespace dcpp
