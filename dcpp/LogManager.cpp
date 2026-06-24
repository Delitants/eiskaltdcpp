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

#include "stdinc.h"

#include "LogManager.h"

#include "File.h"
#include "TimerManager.h"

namespace dcpp {

void LogManager::log(Area area, ParamMap& params, bool writeToFile) {
    const auto formattedMessage = Util::formatParams(getSetting(area, FORMAT), params);
    const auto timestamp = GET_TIME();
    LogEntry entry;

    {
        Lock l(cs);
        entry = { ++nextSequence, timestamp, area, formattedMessage };
        liveEntries.push_back(entry);
        while(liveEntries.size() > MAX_LIVE_ENTRIES) {
            liveEntries.pop_front();
        }

        if(writeToFile) {
            write(getPath(area, params), formattedMessage);
        }
    }

    fire(LogManagerListener::EntryAdded(), entry);
}

void LogManager::message(const string& msg) {
    ParamMap params;
    params["message"] = msg;
    log(SYSTEM, params, CTX_BOOLSETTING(LOG_SYSTEM));

    time_t t = GET_TIME();
    {
        Lock l(cs);
        // Keep the last 100 messages (completely arbitrary number...)
        while(lastLogs.size() > 100)
            lastLogs.pop_front();
        lastLogs.emplace_back(t, msg);
    }
    fire(LogManagerListener::Message(), t, msg);
}

LogManager::List LogManager::getLastLogs() {
    Lock l(cs);
    return lastLogs;
}

LogManager::EntryList LogManager::getLiveEntries() const {
    Lock l(cs);
    return liveEntries;
}

uint64_t LogManager::clearLiveEntries() {
    Lock l(cs);
    liveEntries.clear();
    return nextSequence;
}

string LogManager::getPath(Area area, ParamMap& params) const {
    return CTX_SETTING(LOG_DIRECTORY) + Util::formatParams(getSetting(area, FILE), params, true);
}

string LogManager::getPath(Area area) const {
    ParamMap params;
    return getPath(area, params);
}

const string& LogManager::getSetting(int area, int sel) const {
    return ctx().getSettingsManager()->get(static_cast<SettingsManager::StrSetting>(options[area][sel]), true);
}

const string& LogManager::getSetting(Area area, int sel) const {
    return getSetting(static_cast<int>(area), sel);
}

void LogManager::saveSetting(int area, int sel, const string& setting) {
    ctx().getSettingsManager()->set(static_cast<SettingsManager::StrSetting>(options[area][sel]), setting);
}

void LogManager::saveSetting(Area area, int sel, const string& setting) {
    saveSetting(static_cast<int>(area), sel, setting);
}

void LogManager::write(const string& path, const string& message) {
    try {
        string aArea = Util::validateFileName(path);
        File::ensureDirectory(aArea);
        File f(aArea, File::WRITE, File::OPEN | File::CREATE);
        f.setEndPos(0);
        f.write(message + "\r\n");
    } catch (const FileException&) {
        // ...
    }
}

LogManager::LogManager(DCContext& ctx) : ContextAware(ctx) {
    const auto index = [](Area area) { return static_cast<size_t>(area); };
    options[index(UPLOAD)][FILE]              = SettingsManager::LOG_FILE_UPLOAD;
    options[index(UPLOAD)][FORMAT]            = SettingsManager::LOG_FORMAT_POST_UPLOAD;
    options[index(DOWNLOAD)][FILE]            = SettingsManager::LOG_FILE_DOWNLOAD;
    options[index(DOWNLOAD)][FORMAT]          = SettingsManager::LOG_FORMAT_POST_DOWNLOAD;
    options[index(FINISHED_DOWNLOAD)][FILE]   = SettingsManager::LOG_FILE_FINISHED_DOWNLOAD;
    options[index(FINISHED_DOWNLOAD)][FORMAT] = SettingsManager::LOG_FORMAT_POST_FINISHED_DOWNLOAD;
    options[index(CHAT)][FILE]                = SettingsManager::LOG_FILE_MAIN_CHAT;
    options[index(CHAT)][FORMAT]              = SettingsManager::LOG_FORMAT_MAIN_CHAT;
    options[index(PM)][FILE]                  = SettingsManager::LOG_FILE_PRIVATE_CHAT;
    options[index(PM)][FORMAT]                = SettingsManager::LOG_FORMAT_PRIVATE_CHAT;
    options[index(SYSTEM)][FILE]              = SettingsManager::LOG_FILE_SYSTEM;
    options[index(SYSTEM)][FORMAT]            = SettingsManager::LOG_FORMAT_SYSTEM;
    options[index(STATUS)][FILE]              = SettingsManager::LOG_FILE_STATUS;
    options[index(STATUS)][FORMAT]            = SettingsManager::LOG_FORMAT_STATUS;
    options[index(SPY)][FILE]                 = SettingsManager::LOG_FILE_SPY;
    options[index(SPY)][FORMAT]               = SettingsManager::LOG_FORMAT_SPY;
    options[index(CMD_DEBUG)][FILE]           = SettingsManager::LOG_FILE_CMD_DEBUG;
    options[index(CMD_DEBUG)][FORMAT]         = SettingsManager::LOG_FORMAT_CMD_DEBUG;
}

LogManager::~LogManager() {
}

} // namespace dcpp
