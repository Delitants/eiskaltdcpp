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

/*
 * Automatic Directory Listing Search
 * Henrik Engstr�m, henrikengstrom at home se
 */

#pragma once

#include "DirectoryListing.h"
#include "SettingsManager.h"
#include "DCContext.h"
#include "StringSearch.h"
#include "Util.h"

namespace dcpp {

class AdlSearchManager;

///  Class that represent an ADL search
class ADLSearch
{
public:
    ADLSearch();

    /// The search string
    string searchString;

    /// Active search
    bool isActive;

    /// Auto Queue Results
    bool isAutoQueue;

    /// Search source type
    enum SourceType {
        TypeFirst = 0,
        OnlyFile = TypeFirst,
        OnlyDirectory,
        FullPath,
        TypeLast
    } sourceType;

    SourceType StringToSourceType(const string& s);
    string SourceTypeToString(SourceType t);

    // Maximum & minimum file sizes (in bytes).
    // Negative values means do not check.
    int64_t minFileSize;
    int64_t maxFileSize;

    enum SizeType {
        SizeBytes   = TypeFirst,
        SizeKibiBytes,
        SizeMebiBytes,
        SizeGibiBytes
    };

    SizeType typeFileSize;
    SizeType StringToSizeType(const string& s);
    string SizeTypeToString(SizeType t);
    int64_t GetSizeBase();

    /// Name of the destination directory (empty = 'ADLSearch') and its index
    string destDir;
    unsigned long ddIndex;

private:
    friend class ADLSearchManager;

    /// Prepare search
    void prepare(ParamMap& params);

    /// Search for file match
    bool matchesFile(const string& f, const string& fp, int64_t size);
    /// Search for directory match
    bool matchesDirectory(const string& d);

    bool searchAll(const string& s);

    //decide if regexp should be used
    bool bUseRegexp;
    string regexpstring;

    // Substring searches
    StringSearch::List stringSearchList;
};

///  Class that holds all active searches
class ADLSearchManager : public ContextAware
{
public:
    // Destination directory indexing
    struct DestDir {
        string name;
        DirectoryListing::Directory* dir;
        DirectoryListing::Directory* subdir;
        bool fileAdded;
    };
    typedef vector<DestDir> DestDirList;

    explicit ADLSearchManager(DCContext& ctx);
    virtual ~ADLSearchManager();

    // Search collection
    typedef vector<ADLSearch> SearchCollection;
    SearchCollection collection;

    // Load/save search collection to XML file
    void load();
    void save();
    // Retryable snapshot until terminal saving. Owner serializes saves, load,
    // matching, public collection edits and destruction; this is no input fence.
    [[nodiscard]] bool saveChecked() noexcept;
    // Owner-fenced final snapshot. Caches success OR failure for every later
    // save/destructor without rewriting; retains collection and search state.
    [[nodiscard]] bool saveForShutdownChecked() noexcept;

    // Settings
    GETSET(bool, breakOnFirst, BreakOnFirst)
    GETSET(HintedUser, user, User)

    /// @remarks Used to add ADLSearch directories to an existing DirectoryListing
    void matchListing(DirectoryListing& aDirList, const std::function<bool()>& cancel = {});

private:
    bool shutdownSnapshotRequested = false;
    bool shutdownSnapshotSucceeded = false;
    bool writeSnapshotChecked() noexcept;

    // Recurse through the directories
    void matchRecurse(DestDirList& aDestList, DirectoryListing::Directory* aDir, string& aPath, const std::function<bool()>& cancel);
    // Search for file match
    void matchesFile(DestDirList& destDirVector, DirectoryListing::File *currentFile, string& fullPath, const std::function<bool()>& cancel);
    // Search for directory match
    void matchesDirectory(DestDirList& destDirVector, DirectoryListing::Directory* currentDir, string& fullPath, const std::function<bool()>& cancel);
    // Step up directory
    void stepUpDirectory(DestDirList& destDirVector);
    // Prepare destination directory indexing
    void prepareDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory* root, ParamMap& params, const std::function<bool()>& cancel);
    // Finalize destination directories
    void finalizeDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory* root);

    static string getConfigFile();
};

} // namespace dcpp
