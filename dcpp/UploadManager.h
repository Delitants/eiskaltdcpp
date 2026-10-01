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

#pragma once

#include <algorithm>
#include <condition_variable>
#include <list>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "forward.h"
#include "UserConnectionListener.h"
#include "DCContext.h"
#include "UploadManagerListener.h"
#include "Client.h"
#include "ClientManagerListener.h"
#include "User.h"
#include "TimerManager.h"
#include "Speaker.h"
#include "PerFolderLimit.h"
#include "SettingsManager.h"
#include "HintedUser.h"
#include "UserConnection.h"
#include "GetSet.h"

namespace dcpp {

class UploadManager : private ClientManagerListener, private UserConnectionListener, public Speaker<UploadManagerListener>, private TimerManagerListener, public ContextAware
{
public:
    /** @return Number of uploads. */
    size_t getUploadCount() { Lock l(cs); return uploads.size(); }

    /**
     * @remarks This is only used in the tray icons. Could be used in
     * MainFrame too.
     *
     * @return Running average download speed in Bytes/s
     */
    int64_t getRunningAverage();
    uint8_t getSlots() const { return (uint8_t) (CTX_SETTING(SLOTS)* Client::getTotalCounts());}
    /** @return Number of free slots. */
    int getFreeSlots() { return max((CTX_SETTING(SLOTS) - running), 0); }

    /** @internal */
    int getFreeExtraSlots() { return max(3 - getExtra(), 0); }

    /** @param aUser Reserve an upload slot for this user and connect. */
    void reserveSlot(const HintedUser& aUser);

    /** */
    void reloadRestrictions();

    typedef set<string> FileSet;
    typedef unordered_map<UserPtr, FileSet, User::Hash> FilesMap;
    void clearUserFiles(const UserPtr&);
    HintedUserList getWaitingUsers() const;
    const FileSet& getWaitingUserFiles(const UserPtr&);

    /** @internal */
    void addConnection(UserConnectionPtr conn);

    void notifyQueuedUsers();
    void setRunning(int _running);

    // Stops new work only; accepted transfer/stream/slot cleanup remains alive.
    // Retain dependencies through socket drain. Never from callbacks or under
    // manager/listener locks; serialize manual raw/config/destruction.
    void stopProducersForShutdown();
    // Final accepted-callback phase, after owner-fenced peer/socket drain. Joins
    // admitted cleanup; retained streams/slots yield a stable false outcome, not
    // teardown/install permission. Retain dependencies; never from callbacks or
    // manager/listener locks. This does not initiate socket shutdown.
    bool stopCleanupForShutdown();

    //GETSET(int, running, Running);
    GETSET(uint8_t, extraPartial, ExtraPartial);
    GETSET(uint8_t, extra, Extra);
    GETSET(uint64_t, lastGrant, LastGrant);

    void updateLimits() {limits.RenewList(NULL);}
private:
    int running;
    UploadList uploads;
    mutable CriticalSection cs;
    std::mutex producerMutex;
    std::condition_variable producersChanged;
    bool producerShutdownRequested = false;
    bool producerShutdownComplete = false;
    size_t producers = 0;
    bool beginProducer();
    void finishProducer();
    bool cleanupShutdownRequested = false;
    bool cleanupShutdownComplete = false;
    bool cleanupSucceeded = false;
    size_t cleanupOperations = 0;
    std::unordered_map<std::thread::id, size_t> cleanupThreads;
    bool beginCleanup();
    void finishCleanup();

    typedef unordered_set<UserPtr, User::Hash> SlotSet;
    typedef SlotSet::iterator SlotIter;
    SlotSet reservedSlots;
    CPerfolderLimit limits;
    int lastFreeSlots; /// amount of free slots at the previous minute

    typedef pair<HintedUser, uint64_t> WaitingUser;
    typedef list<WaitingUser> WaitingUserList;

    struct WaitingUserFresh {
        bool operator()(const WaitingUser& wu) { return wu.second > GET_TICK() - 5*60*1000; }
    };

    //functions for manipulating waitingFiles and waitingUsers
    WaitingUserList waitingUsers;       //this one merely lists the users waiting for slots
    FilesMap waitingFiles;      //set of files which this user has asked for
    void addFailedUpload(const UserConnection& source, string filename);

public:
    explicit UploadManager(DCContext& ctx);
    virtual ~UploadManager();

private:

    bool hasUpload(UserConnection& aSource);
    bool getAutoSlot();
    void removeConnection(UserConnection* aConn);
    void removeUpload(Upload* aUpload);

    // ClientManagerListener
    virtual void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser);

    // TimerManagerListener
    virtual void on(Second, uint64_t aTick);
    virtual void on(Minute, uint64_t aTick);

    // UserConnectionListener
    virtual void on(BytesSent, UserConnection*, size_t, size_t);
    virtual void on(Failed, UserConnection*, const string&);
    virtual void on(Get, UserConnection*, const string&, int64_t);
    virtual void on(Send, UserConnection*);
    virtual void on(GetListLength, UserConnection* conn);
    virtual void on(TransmitDone, UserConnection*);

    virtual void on(AdcCommand::GET, UserConnection*, const AdcCommand&);
    virtual void on(AdcCommand::GFI, UserConnection*, const AdcCommand&);

    bool prepareFile(UserConnection& aSource, const string& aType, const string& aFile, int64_t aResume, int64_t aBytes, bool listRecursive = false);
};

} // namespace dcpp
