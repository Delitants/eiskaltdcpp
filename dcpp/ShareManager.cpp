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
#include "ShareManager.h"

#include "AdcHub.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "CryptoManager.h"
#include "Download.h"
#include "File.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "HashBloom.h"
#include "HashManager.h"
#include "QueueManager.h"
#include "ScopedFunctor.h"
#include "SearchResult.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "Wildcards.h"
#include "Transfer.h"
#include "UploadManager.h"
#include "UserConnection.h"
#include "version.h"
#ifdef WITH_DHT
#include "dht/DHT.h"
#include "dht/IndexManager.h"
#endif
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#endif

#include <limits>
#include <charconv>
#include <array>
#include <filesystem>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "DCPlusPlus.h"

namespace dcpp {

using std::numeric_limits;

bool ShareManager::caseSensitiveFilelist_ = false;

namespace {
uint64_t shareDate(const string& text) {
    uint64_t value = 0;
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    return parsed.ec == std::errc() && parsed.ptr == end && value > 0 &&
        value <= static_cast<uint64_t>(numeric_limits<int64_t>::max()) ? value : 0;
}

struct ShareObservation {
    string identity;
    int64_t size = 0;
    uint64_t modified = 0;
};

#ifdef _WIN32
ShareObservation observeShareHandle(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION st;
    FILE_BASIC_INFO basic;
    if(!GetFileInformationByHandle(handle, &st) ||
       !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) ||
       ((st.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory)
        return {};
    ShareObservation result;
    result.size = (static_cast<int64_t>(st.nFileSizeHigh) << 32) | st.nFileSizeLow;
    constexpr int64_t epoch = 116444736000000000LL;
    if(basic.LastWriteTime.QuadPart > epoch)
        result.modified = (basic.LastWriteTime.QuadPart - epoch) / 10000000;
    result.identity = Util::toString(st.dwVolumeSerialNumber) + ":" + Util::toString(st.nFileIndexHigh) + ":" +
        Util::toString(st.nFileIndexLow) + ":" + Util::toString(result.size) + ":" +
        Util::toString(basic.LastWriteTime.QuadPart) + ":" + Util::toString(basic.ChangeTime.QuadPart);
    return result;
}
#else
ShareObservation observeShareStat(const struct stat& st, bool directory) {
    if(directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) return {};
#ifdef __APPLE__
    const auto mt = st.st_mtimespec, ct = st.st_ctimespec;
#else
    const auto mt = st.st_mtim, ct = st.st_ctim;
#endif
    ShareObservation result;
    result.size = st.st_size;
    if(mt.tv_sec > 0 && static_cast<uint64_t>(mt.tv_sec) <= static_cast<uint64_t>(numeric_limits<int64_t>::max()))
        result.modified = static_cast<uint64_t>(mt.tv_sec);
    result.identity = Util::toString(st.st_dev) + ":" + Util::toString(st.st_ino) + ":" + Util::toString(result.size) + ":" +
        Util::toString(mt.tv_sec) + ":" + Util::toString(mt.tv_nsec) + ":" +
        Util::toString(ct.tv_sec) + ":" + Util::toString(ct.tv_nsec);
    return result;
}
#endif

ShareObservation observeSharePath(const string& path, bool directory) {
#ifdef _WIN32
    auto native = std::filesystem::u8path(path);
    if(!native.has_filename()) native = native.parent_path();
    HANDLE handle = CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if(handle == INVALID_HANDLE_VALUE) return {};
    const auto result = observeShareHandle(handle, directory);
    CloseHandle(handle);
    return result;
#else
    struct stat st;
    return ::stat(Text::fromUtf8(path).c_str(), &st) == 0 ? observeShareStat(st, directory) : ShareObservation{};
#endif
}

constexpr size_t managedCacheLimit = 64 * 1024 * 1024;
const string managedCacheMagic = "EISKALT-MANAGED-HASHES-1\n";

void cacheNumber(string& out, uint64_t value) {
    for(int i = 0; i < 8; ++i) out.push_back(static_cast<char>(value >> (i * 8)));
}
void cacheString(string& out, const string& value) {
    cacheNumber(out, value.size());
    out.append(value);
}
struct ManagedCacheReader {
    const string& data;
    size_t pos = 0;
    uint64_t number() {
        if(data.size() - pos < 8) throw ShareException("Truncated managed hash cache");
        uint64_t value = 0;
        for(int i = 0; i < 8; ++i) value |= uint64_t(static_cast<uint8_t>(data[pos++])) << (i * 8);
        return value;
    }
    string bytes(size_t limit) {
        const auto count = number();
        if(count > limit || count > data.size() - pos) throw ShareException("Invalid managed hash cache length");
        auto value = data.substr(pos, static_cast<size_t>(count));
        pos += static_cast<size_t>(count);
        return value;
    }
};
string cacheDigest(const string& data) {
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if(EVP_Digest(data.data(), data.size(), bytes, &size, EVP_sha256(), nullptr) != 1)
        throw ShareException("Cannot checksum managed hash cache");
    return string(reinterpret_cast<char*>(bytes), size);
}
string managedCachePath() {
    return Util::getPath(Util::PATH_USER_CONFIG) + "ManagedHashes-v1.bin";
}

bool managedNameSafe(const string& name) {
    if(name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
       !Text::validateUtf8(name))
        return false;
    for(unsigned char c : name) {
        if(c < 32 || c == 127 || string("<>:\"/\\|?*").find(c) != string::npos)
            return false;
    }
    const string stem = Text::toLower(name.substr(0, name.find('.')));
    if(stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
       (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
        stem[3] >= '1' && stem[3] <= '9'))
        return false;
    return true;
}

bool managedPayloadNameSafe(const string& name) {
    if(!managedNameSafe(name)) return false;
    const auto ext = Text::toLower(Util::getFileExt(name));
    for(const auto* excluded : {".torrent", ".resume", ".fastresume", ".part", ".partial", ".parts",
                              ".dctmp", ".tmp", ".!ut", ".!qb", ".crdownload"}) {
        if(ext == excluded) return false;
    }
    return true;
}

string managedLeafName(const string& path) {
    const auto leaf = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    return string(leaf.begin(), leaf.end());
}

// Keep the opened regular file pinned while hashing. On POSIX each path component
// is opened relative to the preceding no-follow directory descriptor.
class ManagedInput : private NonCopyable {
public:
    explicit ManagedInput(const string& name) {
        namespace fs = std::filesystem;
        if(name.find('\0') != string::npos) throw ShareException("Unsafe managed path");
        const fs::path path(std::u8string(name.begin(), name.end()));
        if(!path.is_absolute() || path.filename().empty()) throw ShareException("Managed path must be absolute");
        for(const auto& part : path.relative_path()) {
            const auto utf8 = part.u8string();
            if(!managedNameSafe(string(utf8.begin(), utf8.end()))) throw ShareException("Unsafe managed path component");
        }
        if(!managedPayloadNameSafe(managedLeafName(name))) throw ShareException("Not a managed payload file");
#ifdef _WIN32
        // Reject UNC/device namespaces. Pin ancestors against rename/delete and
        // reject all reparse points (including junctions, not just symlinks).
        const auto drive = path.root_name().wstring();
        if(drive.size() != 2 || drive[1] != L':') throw ShareException("Unsupported managed path root");
        fs::path current = path.root_path();
        try {
            for(const auto& part : path.relative_path()) {
                current /= part;
                const bool leaf = current == path;
                HANDLE h = CreateFileW(current.c_str(), leaf ? GENERIC_READ : FILE_READ_ATTRIBUTES,
                    leaf ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                if(h == INVALID_HANDLE_VALUE) throw ShareException("Cannot open managed file");
                handles.push_back(h);
                BY_HANDLE_FILE_INFORMATION info;
                if(!GetFileInformationByHandle(h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                   (leaf == !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) || GetFileType(h) != FILE_TYPE_DISK)
                    throw ShareException("Not a regular managed path");
            }
        } catch(...) {
            for(auto h : handles) CloseHandle(h);
            throw;
        }
#else
        int current = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if(current == -1) throw ShareException("Cannot open managed root");
        for(auto it = path.begin(); it != path.end(); ++it) {
            if(*it == path.root_path()) continue;
            auto next = it; ++next;
            const bool leaf = next == path.end();
            const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | (leaf ? O_NONBLOCK : O_DIRECTORY);
            int opened = ::openat(current, it->c_str(), flags);
            ::close(current);
            if(opened == -1) throw ShareException("Cannot open managed path without links");
            current = opened;
        }
        struct stat st;
        if(::fstat(current, &st) || !S_ISREG(st.st_mode)) {
            ::close(current);
            throw ShareException("Not a regular managed file");
        }
        fd = current;
#endif
    }

    ~ManagedInput() {
#ifdef _WIN32
        for(auto h : handles) CloseHandle(h);
#else
        ::close(fd);
#endif
    }

    string state(int64_t& size, uint64_t* modified = nullptr) const {
#ifdef _WIN32
        const auto observed = observeShareHandle(handles.back(), false);
#else
        struct stat st;
        if(::fstat(fd, &st)) throw ShareException("Cannot inspect managed file");
        const auto observed = observeShareStat(st, false);
#endif
        if(observed.identity.empty()) throw ShareException("Cannot inspect managed file");
        size = observed.size;
        if(modified) *modified = observed.modified;
        return observed.identity;
    }

    size_t read(void* buffer, size_t size) {
#ifdef _WIN32
        DWORD count;
        if(!ReadFile(handles.back(), buffer, static_cast<DWORD>(size), &count, nullptr))
            throw ShareException("Cannot read managed file");
        return count;
#else
        ssize_t count;
        do { count = ::read(fd, buffer, size); } while(count < 0 && errno == EINTR);
        if(count < 0) throw ShareException("Cannot read managed file");
        return static_cast<size_t>(count);
#endif
    }
private:
#ifdef _WIN32
    vector<HANDLE> handles;
#else
    int fd = -1;
#endif
};
}

bool ShareManager::ManagedFile::available() const {
    try {
        ManagedInput input(realPath);
        int64_t currentSize;
        return input.state(currentSize) == identity && currentSize == size;
    } catch(const Exception&) {
        return false;
    } catch(const std::filesystem::filesystem_error&) {
        return false;
    }
}

string ShareManager::Directory::File::getRealPath() const {
    if(managed) {
        if(!managed->available()) throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
        return managed->realPath;
    }
    return parent->getRealPath(name);
}

bool ShareManager::managedConflicts(const string& owner, const ManagedShare& candidate) const {
    const auto root = getByVirtual(candidate.virtualName);
    if(root != directories.end() && !(*root)->managedRoot) return true;
    for(const auto& manual : shares) {
        if(Util::stricmp(manual.second, candidate.virtualName) == 0) return true;
    }
    map<string, string> names;
    const auto addFiles = [&names](const ManagedShare& snapshot) {
        for(const auto& file : snapshot.files) {
            const auto name = Text::toLower(managedLeafName(file->realPath));
            const auto added = names.emplace(name, file->realPath);
            if(!added.second && added.first->second != file->realPath) return false;
        }
        return true;
    };
    if(!addFiles(candidate)) return true;
    for(const auto& entry : managedShares) {
        if(entry.first != owner && Util::stricmp(entry.second.virtualName, candidate.virtualName) == 0 &&
           !addFiles(entry.second)) return true;
    }
    return false;
}

bool ShareManager::replaceManagedFiles(const string& owner, const string& virtualName, const StringList& absoluteFiles,
                                      std::function<bool()> externalCancelled, std::function<void()> hashingStarted) {
    if(externalCancelled && externalCancelled()) return false;
    if(owner.empty() || !managedNameSafe(virtualName)) return false;
    auto token = std::make_shared<int>(0);
    {
        Lock l(cs);
        if(externalCancelled && externalCancelled()) return false;
        if(absoluteFiles.empty()) {
            removeManagedFiles(owner);
            return true;
        }
        managedReplacements[owner] = token;
    }
    const auto cancelled = [&] {
        if(externalCancelled && externalCancelled()) return true;
        Lock l(cs);
        const auto i = managedReplacements.find(owner);
        return i == managedReplacements.end() || i->second != token;
    };
    const auto cleanup = [&] {
        Lock l(cs);
        const auto i = managedReplacements.find(owner);
        if(i != managedReplacements.end() && i->second == token) managedReplacements.erase(i);
    };
    ScopedFunctor(cleanup);
    try {
        ManagedShare snapshot;
        snapshot.virtualName = virtualName;
        set<string> seen;
        map<string, ManagedFilePtr> verified;
        {
            Lock l(cs);
            const auto cached = managedVerifiedFiles.find(owner);
            if(cached != managedVerifiedFiles.end())
                for(const auto& file : cached->second) verified.emplace(file->realPath, file);
        }
        set<const ManagedFile*> reused;
        // Validate the complete whitelist before doing potentially expensive reads.
        for(const auto& path : absoluteFiles) {
            if(cancelled()) return false;
            if(!seen.insert(path).second) continue;
            ManagedInput input(path);
            auto file = std::make_shared<ManagedFile>();
            file->realPath = path;
            file->identity = input.state(file->size, &file->modified);
            const auto cached = verified.find(path);
            if(cached != verified.end() && cached->second->identity == file->identity &&
               cached->second->size == file->size) {
                ManagedFilePtr reusedFile = cached->second;
                if(reusedFile->modified != file->modified) {
                    // The v1 cache stores hash evidence, not dates. Attach the
                    // fresh observation without reading/hashing the payload.
                    auto dated = std::make_shared<ManagedFile>(*reusedFile);
                    dated->modified = file->modified;
                    reusedFile = std::move(dated);
                }
                snapshot.files.push_back(reusedFile);
                reused.insert(reusedFile.get());
            } else {
                snapshot.files.push_back(file);
            }
        }
        {
            Lock l(cs);
            if(managedConflicts(owner, snapshot)) return false;
        }
        std::array<uint8_t, 256 * 1024> buffer;
        bool notifiedHashing = false;
        for(auto& entry : snapshot.files) {
            if(cancelled()) return false;
            if(reused.count(entry.get())) continue;
            if(!notifiedHashing && hashingStarted) hashingStarted();
            notifiedHashing = true;
            auto file = std::make_shared<ManagedFile>(*entry);
            ManagedInput input(file->realPath);
            int64_t size;
            if(input.state(size) != file->identity) return false;
            TigerTree tree(max(TigerTree::calcBlockSize(size, 10), HashManager::MIN_BLOCK_SIZE));
            int64_t total = 0;
            for(;;) {
                if(cancelled()) return false;
                const auto count = input.read(buffer.data(), buffer.size());
                if(!count) break;
                if(count > static_cast<uint64_t>(size - total)) return false;
                total += count;
                tree.update(buffer.data(), count);
            }
            if(total != size || input.state(size) != file->identity || !file->available()) return false;
            tree.finalize();
            file->tth = tree.getRoot();
            // Store leaves without the seconds-only filename cache or TTHDone
            // callbacks. Never take HashManager's lock while holding cs here.
            ctx().getHashManager()->addTree(tree);
            TigerTree stored;
            if(!ctx().getHashManager()->getTree(file->tth, stored)) return false;
            // Hash-store rebuild only retains filename-indexed trees. Keep the
            // exact leaves alive with the owned snapshot, independent of that cache.
            file->leaves = tree.getLeafData();
            entry = file;
        }
        {
            Lock l(cs);
            if(cancelled() || managedConflicts(owner, snapshot)) return false;
            for(const auto& file : snapshot.files) {
                if(!file->available()) return false;
            }
            if(cancelled()) return false;
            rememberManagedFiles(owner, snapshot);
            managedShares[owner] = std::move(snapshot);
            rebuildManagedDirectories();
        }
        saveManagedHashCache();
        return true;
    } catch(const Exception&) {
        return false;
    } catch(const std::filesystem::filesystem_error&) {
        return false;
    }
}

void ShareManager::removeManagedFiles(const string& owner) {
    Lock l(cs);
    managedReplacements.erase(owner);
    if(managedShares.erase(owner)) rebuildManagedDirectories();
}

void ShareManager::forgetManagedFileHashes(const string& owner) {
    {
        Lock l(cs);
        // Also fence an in-flight replacement that captured the old cache.
        managedReplacements.erase(owner);
        const auto cached = managedVerifiedFiles.find(owner);
        if(cached == managedVerifiedFiles.end()) return;
        for(const auto& file : cached->second)
            managedVerifiedBytes -= file->leaves.size() + file->realPath.size() + file->identity.size();
        managedVerifiedCount -= cached->second.size();
        managedVerifiedFiles.erase(cached);
    }
    saveManagedHashCache(true);
}

void ShareManager::rememberManagedFiles(const string& owner, const ManagedShare& snapshot) {
    size_t bytes = 0;
    for(const auto& file : snapshot.files)
        bytes += file->leaves.size() + file->realPath.size() + file->identity.size();
    // Eviction affects performance only, never publication or upload authorization.
    const auto previous = managedVerifiedFiles.find(owner);
    if(previous != managedVerifiedFiles.end()) {
        for(const auto& file : previous->second)
            managedVerifiedBytes -= file->leaves.size() + file->realPath.size() + file->identity.size();
        managedVerifiedCount -= previous->second.size();
        managedVerifiedFiles.erase(previous);
    }
    if(bytes > managedVerifiedByteLimit || snapshot.files.size() > managedVerifiedFileLimit) return;
    while(!managedVerifiedFiles.empty() &&
          (managedVerifiedBytes + bytes > managedVerifiedByteLimit ||
           managedVerifiedCount + snapshot.files.size() > managedVerifiedFileLimit)) {
        const auto victim = managedVerifiedFiles.begin();
        for(const auto& file : victim->second)
            managedVerifiedBytes -= file->leaves.size() + file->realPath.size() + file->identity.size();
        managedVerifiedCount -= victim->second.size();
        managedVerifiedFiles.erase(victim);
    }
    managedVerifiedFiles[owner] = snapshot.files;
    managedVerifiedBytes += bytes;
    managedVerifiedCount += snapshot.files.size();
}

void ShareManager::loadManagedHashCache() {
    // This restores only hash evidence, never a share root or an upload whitelist.
    try {
        ManagedInput input(managedCachePath());
        int64_t size;
        const auto identity = input.state(size);
        if(size < 0 || size > static_cast<int64_t>(managedCacheLimit)) return;
        string encoded(static_cast<size_t>(size), '\0');
        size_t offset = 0;
        while(offset < encoded.size()) {
            const auto count = input.read(encoded.data() + offset, encoded.size() - offset);
            if(!count) return;
            offset += count;
        }
        if(input.state(size) != identity || encoded.size() < managedCacheMagic.size() + 32 ||
           encoded.compare(0, managedCacheMagic.size(), managedCacheMagic) != 0) return;
        const auto body = encoded.substr(managedCacheMagic.size() + 32);
        if(cacheDigest(body) != encoded.substr(managedCacheMagic.size(), 32)) return;
        ManagedCacheReader reader{body};
        const auto count = reader.number();
        if(count > managedVerifiedFileLimit) return;
        map<string, vector<ManagedFilePtr>> restored;
        size_t bytes = 0;
        for(uint64_t i = 0; i < count; ++i) {
            const auto owner = reader.bytes(1024);
            auto file = std::make_shared<ManagedFile>();
            file->realPath = reader.bytes(65536);
            file->identity = reader.bytes(1024);
            const auto fileSize = reader.number();
            if(owner.empty() || file->realPath.empty() || file->identity.empty() ||
               owner.find('\0') != string::npos || file->realPath.find('\0') != string::npos ||
               fileSize > (uint64_t(1) << 50)) return;
            file->size = static_cast<int64_t>(fileSize);
            const auto root = reader.bytes(TigerTree::BYTES);
            const auto leaves = reader.bytes(managedVerifiedByteLimit);
            const auto block = max(TigerTree::calcBlockSize(file->size, 10), HashManager::MIN_BLOCK_SIZE);
            if(root.size() != TigerTree::BYTES ||
               leaves.size() != TigerTree::calcBlocks(file->size, block) * TigerTree::BYTES) return;
            file->leaves.assign(leaves.begin(), leaves.end());
            TigerTree tree(file->size, block, file->leaves.data());
            file->tth = TTHValue(reinterpret_cast<const uint8_t*>(root.data()));
            if(tree.getRoot() != file->tth) return;
            bytes += file->leaves.size() + file->realPath.size() + file->identity.size();
            if(bytes > managedVerifiedByteLimit) return;
            auto& files = restored[owner];
            if(std::any_of(files.begin(), files.end(), [&](const ManagedFilePtr& other) {
                return other->realPath == file->realPath;
            })) return;
            files.push_back(file);
        }
        if(reader.pos != body.size()) return;
        managedVerifiedFiles = std::move(restored);
        managedVerifiedBytes = bytes;
        managedVerifiedCount = static_cast<size_t>(count);
    } catch(...) {
        // Missing, corrupt or incompatible caches are a performance miss only.
    }
}

void ShareManager::saveManagedHashCache(bool invalidatePrevious) {
    // Serialize writers before taking the snapshot so an older write cannot win.
    std::unique_lock ioLock(managedCacheIoMutex);
    std::filesystem::path staging;
    try {
        const auto path = managedCachePath();
        if(invalidatePrevious) {
            std::error_code error;
            std::filesystem::remove(std::filesystem::u8path(path), error);
            if(error) {
                ioLock.unlock();
                ctx().getLogManager()->message(_("Cannot revoke the saved DC++ hash cache. Check profile folder permissions before restarting."));
                return;
            }
        }
        string body;
        {
            Lock l(cs);
            cacheNumber(body, managedVerifiedCount);
            for(const auto& [owner, files] : managedVerifiedFiles) {
                for(const auto& file : files) {
                    cacheString(body, owner);
                    cacheString(body, file->realPath);
                    cacheString(body, file->identity);
                    cacheNumber(body, file->size);
                    cacheString(body, string(reinterpret_cast<const char*>(file->tth.data), TigerTree::BYTES));
                    cacheString(body, string(file->leaves.begin(), file->leaves.end()));
                }
            }
        }
        if(body.size() + managedCacheMagic.size() + 32 > managedCacheLimit) return;
        unsigned char random[16];
        if(RAND_bytes(random, sizeof(random)) != 1) return;
        staging = std::filesystem::u8path(path + "." + Encoder::toBase32(random, sizeof(random)));
        if(!std::filesystem::create_directory(staging)) return;
        std::filesystem::permissions(staging, std::filesystem::perms::owner_all);
        const auto nativeTemp = staging / "cache";
        const auto utf8Temp = nativeTemp.u8string();
        const string temp(utf8Temp.begin(), utf8Temp.end());
        {
            File output(temp, File::WRITE, File::CREATE | File::TRUNCATE);
            std::filesystem::permissions(nativeTemp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
            output.write(managedCacheMagic + cacheDigest(body) + body);
            output.flush();
        }
#ifdef _WIN32
        if(!MoveFileExW(Text::utf8ToWide(temp).c_str(), Text::utf8ToWide(path).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw ShareException("Cannot replace managed hash cache");
#else
        File::renameFile(temp, path);
#endif
    } catch(...) {
        // Sharing still works if the optional cache cannot be written.
    }
    if(!staging.empty()) {
        std::error_code error;
        std::filesystem::remove(staging / "cache", error);
        std::filesystem::remove(staging, error);
    }
}

ShareManager::ManagedFileHashes ShareManager::getManagedFileHashes(const string& owner) const {
    std::shared_ptr<const ManagedHashSnapshots> snapshots;
    {
        std::lock_guard lock(managedHashMutex);
        snapshots = managedHashSnapshots;
    }
    if(!snapshots) return {};
    const auto found = snapshots->find(owner);
    return found == snapshots->end() ? ManagedFileHashes{} : found->second;
}

void ShareManager::rebuildManagedDirectories() {
    auto hashes = std::make_shared<ManagedHashSnapshots>();
    tthIndex.clear(); // iterators must not outlive the old managed directories
    directories.remove_if([](const Directory::Ptr& d) { return d->managedRoot; });
    for(const auto& entry : managedShares) {
        for(const auto& file : entry.second.files) {
            if(!file->available()) continue;
            auto root = getByVirtual(entry.second.virtualName);
            Directory::Ptr dir;
            if(root == directories.end()) {
                dir = Directory::create(entry.second.virtualName);
                dir->managedRoot = true;
                directories.push_back(dir);
            } else {
                dir = *root;
                if(!dir->managedRoot) continue;
            }
            Directory::File item(managedLeafName(file->realPath), file->size, dir, file->tth);
            item.managed = file;
            item.modified = file->modified;
            dir->files.insert(item);
            (*hashes)[entry.first].emplace(file->realPath, std::make_pair(file->size, file->tth));
        }
    }
    rebuildIndices();
    std::shared_ptr<const ManagedHashSnapshots> published = std::move(hashes);
    {
        // Only exchange ownership here; no file I/O, map copying or destruction.
        std::lock_guard lock(managedHashMutex);
        managedHashSnapshots.swap(published);
    }
    setDirty();
    forceXmlRefresh = true;
}

ShareManager::ShareManager(DCContext& ctx) : ContextAware(ctx), hits(0), xmlListLen(0), bzXmlListLen(0),
    xmlDirty(true), forceXmlRefresh(false), refreshDirs(false), update(false), initial(true), listN(0), refreshing(false),
    lastXmlUpdate(0), lastFullUpdate(GET_TICK()), bloom(1<<20)
{
    caseSensitiveFilelist_ = ctx.getSettingsManager()->getBool(SettingsManager::CASESENSITIVE_FILELIST);
    this->ctx().getSettingsManager()->addListener(this);
    this->ctx().getTimerManager()->addListener(this);
    this->ctx().getQueueManager()->addListener(this);
    this->ctx().getHashManager()->addListener(this);
    loadManagedHashCache();
}

ShareManager::~ShareManager() {
    ctx().getSettingsManager()->removeListener(this);
    ctx().getTimerManager()->removeListener(this);
    ctx().getQueueManager()->removeListener(this);
    ctx().getHashManager()->removeListener(this);

    join();

    if(bzXmlRef.get()) {
        bzXmlRef.reset();
        File::deleteFile(getBZXmlFile());
    }
}

ShareManager::Directory::Directory(const string& aName, const ShareManager::Directory::Ptr& aParent) :
    size(0),
    name(aName),
    parent(aParent.get()),
    fileTypes(1 << SearchManager::TYPE_DIRECTORY)
{
}

string ShareManager::Directory::getADCPath() const {
    if(!getParent())
        return '/' + name + '/';
    return getParent()->getADCPath() + name + '/';
}

string ShareManager::Directory::getFullName() const {
    if(!getParent())
        return getName() + '\\';
    return getParent()->getFullName() + getName() + '\\';
}

void ShareManager::Directory::addType(uint32_t type) {
    if(!hasType(type)) {
        fileTypes |= (1 << type);
        if(getParent())
            getParent()->addType(type);
    }
}

string ShareManager::Directory::getRealPath(const std::string& path) const {
    if(getParent()) {
        return getParent()->getRealPath(getName() + PATH_SEPARATOR_STR + path);
    } else {
        return dcpp::getContext()->getShareManager()->findRealRoot(getName(), path);
    }
}

string ShareManager::findRealRoot(const string& virtualRoot, const string& virtualPath) const {
    for(auto& i : shares) {
        if(Util::stricmp(i.second, virtualRoot) == 0) {
            std::string name = i.first + virtualPath;
            dcdebug("Matching %s\n", name.c_str());
            if (File::getSize(name) != -1) //NOTE: see core 0.750
                return name;
        }
    }

    throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

int64_t ShareManager::Directory::getSize() const {
    if(managedRoot) {
        int64_t total = 0;
        for(const auto& file : files) {
            if(file.available()) total += file.getSize();
        }
        return total;
    }
    int64_t tmp = size;
    for(auto& i : directories)
        tmp += i.second->getSize();
    return tmp;
}

string ShareManager::toVirtual(const TTHValue& tth) const {
    if(tth == bzXmlRoot) {
        return Transfer::USER_LIST_NAME_BZ;
    } else if(tth == xmlRoot) {
        return Transfer::USER_LIST_NAME;
    }

    Lock l(cs);
    auto i = tthIndex.find(tth);
    if(i != tthIndex.end() && i->second->available()) {
        return i->second->getADCPath();
    } else {
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
    }
}

string ShareManager::toReal(const string& virtualFile) {
    Lock l(cs);
    if(virtualFile == "MyList.DcLst") {
        throw ShareException("NMDC-style lists no longer supported, please upgrade your client");
    } else if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
        generateXmlList();
        return getBZXmlFile();
    }

    return findFile(virtualFile)->getRealPath();
}

StringList ShareManager::getRealPaths(const string& virtualPath) {
    if(virtualPath.empty())
        throw ShareException("empty virtual path");

    StringList ret;

    Lock l(cs);

    if(*(virtualPath.end() - 1) == '/') {
        // directory
        Directory::Ptr d = splitVirtual(virtualPath).first;

        // imitate Directory::getRealPath
        if(d->getParent()) {
            ret.push_back(d->getParent()->getRealPath(d->getName()));
        } else {
            for(auto& i : shares) {
                if(Util::stricmp(i.second, d->getName()) == 0) {
                    // remove the trailing path sep
                    if(FileFindIter(i.first.substr(0, i.first.size() - 1)) != FileFindIter()) {
                        ret.push_back(i.first);
                    }
                }
            }
        }

    } else {
        // file
        ret.push_back(toReal(virtualPath));
    }

    return ret;
}

TTHValue ShareManager::getTTH(const string& virtualFile) const {
    Lock l(cs);
    if(virtualFile == Transfer::USER_LIST_NAME_BZ) {
        return bzXmlRoot;
    } else if(virtualFile == Transfer::USER_LIST_NAME) {
        return xmlRoot;
    }

    return findFile(virtualFile)->getTTH();
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile) const {
    TigerTree tree;
    try {
        TTHValue tth;
        {
            Lock l(cs);
            if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
                tth = getTTH(virtualFile);
            } else {
                const auto file = findFile(virtualFile);
                if(file->managed) {
                    const auto& leaves = file->managed->leaves;
                    return new MemoryInputStream(leaves.data(), leaves.size());
                }
                tth = file->getTTH();
            }
        }
        if(!ctx().getHashManager()->getTree(tth, tree)) return 0;
    } catch(const Exception&) {
        return 0;
    }

    ByteVector buf = tree.getLeafData();
    return new MemoryInputStream(buf.data(), buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile) {
    if(aFile == Transfer::USER_LIST_NAME) {
        generateXmlList();
        AdcCommand cmd(AdcCommand::CMD_RES);
        cmd.addParam("FN", aFile);
        cmd.addParam("SI", Util::toString(xmlListLen));
        cmd.addParam("TR", xmlRoot.toBase32());
        return cmd;
    } else if(aFile == Transfer::USER_LIST_NAME_BZ) {
        generateXmlList();

        AdcCommand cmd(AdcCommand::CMD_RES);
        cmd.addParam("FN", aFile);
        cmd.addParam("SI", Util::toString(bzXmlListLen));
        cmd.addParam("TR", bzXmlRoot.toBase32());
        return cmd;
    }

    if(aFile.compare(0, 4, "TTH/") != 0)
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

    TTHValue val(aFile.substr(4));
    Lock l(cs);
    auto i = tthIndex.find(val);
    if(i == tthIndex.end() || !i->second->available()) {
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
    }

    const Directory::File& f = *i->second;
    AdcCommand cmd(AdcCommand::CMD_RES);
    cmd.addParam("FN", f.getADCPath());
    cmd.addParam("SI", Util::toString(f.getSize()));
    cmd.addParam("TR", f.getTTH().toBase32());
    return cmd;
}

pair<ShareManager::Directory::Ptr, string> ShareManager::splitVirtual(const string& virtualPath) const {
    if(virtualPath.empty() || virtualPath[0] != '/') {
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
    }

    auto i = virtualPath.find('/', 1);
    if(i == string::npos || i == 1) {
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
    }

    auto dmi = getByVirtual( virtualPath.substr(1, i - 1));
    if(dmi == directories.end()) {
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
    }

    auto d = *dmi;

    auto j = i + 1;
    while((i = virtualPath.find('/', j)) != string::npos) {
        auto mi = d->directories.find(virtualPath.substr(j, i - j));
        j = i + 1;
        if(mi == d->directories.end())
            throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
        d = mi->second;
    }

    return make_pair(d, virtualPath.substr(j));
}

ShareManager::Directory::File::Set::const_iterator ShareManager::findFile(const string& virtualFile) const {
    if(virtualFile.compare(0, 4, "TTH/") == 0) {
        auto i = tthIndex.find(TTHValue(virtualFile.substr(4)));
        if(i == tthIndex.end() || !i->second->available()) {
            throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
        }
        return i->second;
    }

    auto v = splitVirtual(virtualFile);
    auto it = find_if(v.first->files.begin(), v.first->files.end(),
                      Directory::File::StringComp(v.second));
    if(it == v.first->files.end() || !it->available())
        throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
    return it;
}

string ShareManager::validateVirtual(const string& aVirt) const {
    string tmp = aVirt;
    string::size_type idx = 0;

    while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
        tmp[idx] = '_';
    }
    return tmp;
}

bool ShareManager::hasVirtual(const string& virtualName) const {
    Lock l(cs);
    return getByVirtual(virtualName) != directories.end();
}

void ShareManager::load(SimpleXML& aXml) {
    Lock l(cs);
    ++manualSharesGeneration;

    // A settings reload is another caller-reapply boundary. Otherwise a newly
    // loaded manual root could silently acquire an existing managed directory.
    managedReplacements.clear();
    if(!managedShares.empty()) {
        managedShares.clear();
        rebuildManagedDirectories();
    }

    caseSensitiveFilelist_ = ctx().getSettingsManager()->getBool(SettingsManager::CASESENSITIVE_FILELIST);

    aXml.resetCurrentChild();
    if(aXml.findChild("Share")) {
        aXml.stepIn();
        while(aXml.findChild("Directory")) {
            string realPath = aXml.getChildData();
            if(realPath.empty()) {
                continue;
            }
            // make sure realPath ends with a PATH_SEPARATOR
            if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
                realPath += PATH_SEPARATOR;
            }

            const string& virtualName = aXml.getChildAttrib("Virtual");
            string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);
            shares.insert(std::make_pair(realPath, vName));
            if(getByVirtual(vName) == directories.end()) {
                directories.push_back(Directory::create(vName));
            }
        }
        try {
            aXml.stepOut();
        }
        catch(const Exception&) { }
    }
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string STTH = "TTH";

struct ShareLoader : public SimpleXMLReader::CallBack {
    ShareLoader(ShareManager::DirList& aDirs) : dirs(aDirs), cur(0), depth(0) { }
    virtual void startTag(const string& name, StringPairList& attribs, bool simple) {
        if(name == SDIRECTORY) {
            const string& name = getAttrib(attribs, SNAME, 0);
            if(!name.empty()) {
                if(depth == 0) {
                    cur = 0;
                    if(getAttrib(attribs, "Managed", 0) != "1") {
                        for(auto& i : dirs) {
                            if(!i->managedRoot && Util::stricmp(i->getName(), name) == 0) {
                                cur = i;
                                break;
                            }
                        }
                    }
                } else if(cur) {
                    cur = ShareManager::Directory::create(name, cur);
                    cur->getParent()->directories[cur->getName()] = cur;
                }
                if(cur) cur->modified = shareDate(getAttrib(attribs, "Date", 0));
            }

            if(simple) {
                if(cur) {
                    cur = cur->getParent();
                }
            } else {
                depth++;
            }
        } else if(cur && name == SFILE) {
            const string& fname = getAttrib(attribs, SNAME, 0);
            const string& size = getAttrib(attribs, SSIZE, 1);
            const string& root = getAttrib(attribs, STTH, 2);
            if(fname.empty() || size.empty() || (root.size() != 39)) {
                dcdebug("Invalid file found: %s\n", fname.c_str());
                return;
            }
            ShareManager::Directory::File file(fname, Util::toInt64(size), cur, TTHValue(root));
            file.modified = shareDate(getAttrib(attribs, "Date", 0));
            cur->files.insert(file);
        }
    }
    virtual void endTag(const string& name) {
        if(name == SDIRECTORY) {
            depth--;
            if(cur) {
                cur = cur->getParent();
            }
        }
    }

private:
    ShareManager::DirList& dirs;

    ShareManager::Directory::Ptr cur;
    size_t depth;
};

bool ShareManager::loadCache() {
    Lock l(cs);
    try {
        ShareLoader loader(directories);
        SimpleXMLReader xml(&loader);

        dcpp::File ff(Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2", dcpp::File::READ, dcpp::File::OPEN);
        FilteredInputStream<UnBZFilter, false> f(&ff);

        xml.parse(f);

        for(auto& d : directories) {
            const auto roots = std::count_if(shares.begin(), shares.end(), [&](const auto& share) {
                return Util::stricmp(share.second, d->getName()) == 0;
            });
            if(roots > 1 && d->modified) {
                // A dated cache root had one physical source and is now merged.
                // Already merged cache roots are undated: preserve their known
                // unmerged-child dates as prior observations until normal scan.
                std::function<void(Directory&)> clearDates = [&](Directory& dir) {
                    dir.modified = 0;
                    for(auto& child : dir.directories) clearDates(*child.second);
                };
                clearDates(*d);
            }
            updateIndices(*d);
        }

        return true;
    } catch(const Exception& e) {
        dcdebug("%s\n", e.getError().c_str());
    }
    return false;
}

void ShareManager::save(SimpleXML& aXml) {
    Lock l(cs);

    aXml.addTag("Share");
    aXml.stepIn();
    for(auto& i: shares) {
        aXml.addTag("Directory", i.first);
        aXml.addChildAttrib("Virtual", i.second);
    }
    try {
        aXml.stepOut();
    }
    catch(const Exception&) { }
}

void ShareManager::addDirectory(const string& realPath, const string& virtualName) {
    if(realPath.empty() || virtualName.empty()) {
        throw ShareException(_("No directory specified"));
    }

    {
        Lock l(cs);
        for(const auto& entry : managedShares) {
            if(Util::stricmp(entry.second.virtualName, validateVirtual(virtualName)) == 0)
                throw ShareException("Virtual name is reserved by managed files");
        }
    }

    if (!checkHidden(realPath)) {
        throw ShareException(_("Directory is hidden"));
    }

    if(Util::stricmp(CTX_SETTING(TEMP_DOWNLOAD_DIRECTORY), realPath) == 0) {
        throw ShareException(_("The temporary download directory cannot be shared"));
    }
    list<string> removeMap;
    {
        Lock l(cs);

        for(auto& i: shares) {
            if(Util::strnicmp(realPath, i.first, i.first.length()) == 0) {
                // Trying to share an already shared directory
                //throw ShareException(_("Directory already shared"));
                removeMap.push_front(i.first);
            } else if(Util::strnicmp(realPath, i.first, realPath.length()) == 0) {
                // Trying to share a parent directory
                //throw ShareException(_("Remove all subdirectories before adding this one"));
                removeMap.push_front(i.first);
            }
        }
    }
    for(auto& i : removeMap) {
        removeDirectory(i);
    }

    HashManager::HashPauser pauser(ctx());

    auto dp = buildTree(realPath, Directory::Ptr());

    string vName = validateVirtual(virtualName);
    dp->setName(vName);

    {
        Lock l(cs);

        for(const auto& entry : managedShares) {
            if(Util::stricmp(entry.second.virtualName, vName) == 0)
                throw ShareException("Virtual name is reserved by managed files");
        }
        shares.insert(std::make_pair(realPath, vName));
        ++manualSharesGeneration;
        updateIndices(*merge(dp));

        setDirty();
    }
}

ShareManager::Directory::Ptr ShareManager::merge(const Directory::Ptr& directory) {
    for(auto& i : directories) {
        if(Util::stricmp(i->getName(), directory->getName()) == 0) {
            dcdebug("Merging directory %s\n", directory->getName().c_str());
            i->merge(directory);
            return i;
        }
    }

    dcdebug("Adding new directory %s\n", directory->getName().c_str());

    directories.push_back(directory);
    return directory;
}

void ShareManager::Directory::merge(const Directory::Ptr& source) {
    modified = 0; // A merged virtual directory has no single physical mtime.
    // merge directories
    for(auto& i: source->directories) {
        auto subSource = i.second;

        auto ti = directories.find(subSource->getName());
        if(ti == directories.end()) {
            if(findFile(subSource->getName()) != files.end()) {
                dcdebug("File named the same as directory");
            } else {
                // the directory doesn't exist; create it.
                directories.emplace(subSource->getName(), subSource);
                subSource->parent = this;
            }
        } else {
            // the directory was already existing; merge into it.
            auto subTarget = ti->second;
            subTarget->merge(subSource);
        }
    }

    // All subdirs either deleted or moved to target...
    source->directories.clear();

    // merge files
    for(auto& i: source->files) {
        if(findFile(i.getName()) == files.end()) {
            if(directories.find(i.getName()) != directories.end()) {
                dcdebug("Directory named the same as file");
            } else {
                auto added = files.insert(i);
                if(added.second) {
                    const_cast<File&>(*added.first).setParent(this);
                }
            }
        }
    }
}

void ShareManager::removeDirectory(const string& realPath) {
    if(realPath.empty())
        return;

    ctx().getHashManager()->stopHashing(realPath);

    Lock l(cs);

    auto i = shares.find(realPath);
    if(i == shares.end()) {
        return;
    }

    auto vName = i->second;
    for(auto j = directories.begin(); j != directories.end(); ) {
        if(Util::stricmp((*j)->getName(), vName) == 0) {
            directories.erase(j++);
        } else {
            ++j;
        }
    }

    shares.erase(i);
    ++manualSharesGeneration;

    HashManager::HashPauser pauser(ctx());

    // Readd all directories with the same vName
    for(i = shares.begin(); i != shares.end(); ++i) {
        if(Util::stricmp(i->second, vName) == 0 && checkHidden(i->first)) {
            auto dp = buildTree(i->first, 0);
            dp->setName(i->second);
            merge(dp);
        }
    }

    rebuildIndices();
    setDirty();
}

void ShareManager::renameDirectory(const string& realPath, const string& virtualName) {
    removeDirectory(realPath);
    addDirectory(realPath, virtualName);
}

ShareManager::DirList::const_iterator ShareManager::getByVirtual(const string& virtualName) const {
    for(auto i = directories.begin(); i != directories.end(); ++i) {
        if(Util::stricmp((*i)->getName(), virtualName) == 0) {
            return i;
        }
    }
    return directories.end();
}

int64_t ShareManager::getShareSize(const string& realPath) const {
    Lock l(cs);
    dcassert(!realPath.empty());
    auto i = shares.find(realPath);

    if(i != shares.end()) {
        auto j = getByVirtual(i->second);
        if(j != directories.end()) {
            return (*j)->getSize();
        }
    }
    return -1;
}

int64_t ShareManager::getShareSize() const {
    Lock l(cs);
    int64_t tmp = 0;
    for(auto& i: tthIndex) {
        if(i.second->available()) tmp += i.second->getSize();
    }
    return tmp;
}

size_t ShareManager::getSharedFiles() const {
    Lock l(cs);
    return std::count_if(tthIndex.begin(), tthIndex.end(), [](const auto& i) { return i.second->available(); });
}

ShareManager::Directory::Ptr ShareManager::buildTree(const string& aName, const Directory::Ptr& aParent) {
    auto dir = Directory::create(Util::getLastDir(aName), aParent);
    const auto directoryObservation = observeSharePath(aName, true);

    auto lastFileIter = dir->files.begin();

    FileFindIter end;
    const string l_skip_list = CTX_SETTING(SKIPLIST_SHARE);
#ifdef _WIN32
    for(FileFindIter i(aName + "*"); i != end; ++i) {
#else
    //the fileiter just searches directories for now, not sure if more
    //will be needed later
    //for(FileFindIter i(aName + "*"); i != end; ++i) {
    for(FileFindIter i(aName); i != end; ++i) {
#endif
        string name = i->getFileName();
        if(name.empty()) {
            ctx().getLogManager()->message(str(F_("Invalid file name found while hashing folder %1%") % Util::addBrackets(aName)));
            continue;
        }

        if(name == "." || name == "..")
            continue;
        if(!CTX_BOOLSETTING(SHARE_HIDDEN) && i->isHidden())
            continue;
        if(!CTX_BOOLSETTING(FOLLOW_LINKS) && i->isLink())
            continue;

        int64_t size = i->getSize();

        string fileName = aName + name;

        if (l_skip_list.size())
        {
            if (Wildcard::patternMatch(fileName , l_skip_list, '|'))
            {
                ctx().getLogManager()->message(str(F_("Skip share file: %1% (Size: %2%)")
                                                       % Util::addBrackets(fileName) % Util::formatBytes(size)));
                continue;
            }
        }
        if(i->isDirectory()) {
            string newName = aName + name + PATH_SEPARATOR;
            if((::strcmp(newName.c_str(), CTX_SETTING(TEMP_DOWNLOAD_DIRECTORY).c_str()) != 0)
                    && (::strcmp(newName.c_str(), Util::getPath(Util::PATH_USER_CONFIG).c_str()) != 0)
                    && (::strcmp(newName.c_str(), CTX_SETTING(LOG_DIRECTORY).c_str()) != 0)) {
                dir->directories[name] = buildTree(newName, dir);
            }
        } else {
            // Not a directory, assume it's a file...make sure we're not sharing the settings file...
            const string l_ext = Util::getFileExt(name);
            if ((name != "Thumbs.db") &&
                    (name != "desktop.ini") &&
                    (name != "folder.htt")
                    ) {
                if (!CTX_BOOLSETTING(SHARE_TEMP_FILES) &&
                        (::strcmp(l_ext.c_str(), ".dctmp") == 0)) {
                    ctx().getLogManager()->message(str(F_("Skip share temp file: %1% (Size: %2%)")
                                                           % Util::addBrackets(fileName) % Util::formatBytes(size)));
                    continue;
                }
                if (CTX_BOOLSETTING(SHARE_SKIP_ZERO_BYTE) && size == 0)
                    continue;
                if(Util::stricmp(fileName, CTX_SETTING(TLS_PRIVATE_KEY_FILE)) == 0) {
                    continue;
                }
                try {
                    const auto observed = observeSharePath(fileName, false);
                    const auto timestamp = i->getLastWriteTime();
                    if(ctx().getHashManager()->checkTTH(fileName, size, timestamp)) {
                        Directory::File file(name, size, dir, ctx().getHashManager()->getTTH(fileName, size));
                        if(!observed.identity.empty() && observed.size == size && observed.modified == timestamp &&
                           observed.identity == observeSharePath(fileName, false).identity)
                            file.modified = observed.modified;
                        lastFileIter = dir->files.insert(lastFileIter, file);
                    }
                } catch(const HashException&) {
                }
            }
        }
    }

    if(!directoryObservation.identity.empty() &&
       directoryObservation.identity == observeSharePath(aName, true).identity)
        dir->modified = directoryObservation.modified;
    return dir;
}

//NOTE: freedcpp [+
#ifdef _WIN32
bool ShareManager::checkHidden(const string& aName) const {
    FileFindIter ff = FileFindIter(aName.substr(0, aName.size() - 1));

    if (ff != FileFindIter()) {
        return (CTX_BOOLSETTING(SHARE_HIDDEN) || !ff->isHidden());
    }

    return true;
}

#else // !_WIN32

bool ShareManager::checkHidden(const string& aName) const
{
    // check open a directory
    if (!(FileFindIter(aName) != FileFindIter()))
        return true;

    // check hidden directory
    bool hidden = false;
    string path = aName.substr(0, aName.size() - 1);
    string::size_type i = path.rfind(PATH_SEPARATOR);

    if (i != string::npos)
    {
        string dir = path.substr(i + 1);
        if (dir[0] == '.')
            hidden = true;
    }

    return (CTX_BOOLSETTING(SHARE_HIDDEN) || !hidden);
}
#endif // !_WIN32
//NOTE: freedcpp +]

void ShareManager::updateIndices(Directory& dir) {
    bloom.add(Text::toLower(dir.getName()));

    for(auto& i : dir.directories) {
        updateIndices(*i.second);
    }

    dir.size = 0;

    for(auto i = dir.files.begin(); i != dir.files.end(); ) {
        auto current = i++;
        updateIndices(dir, current);
    }
}

void ShareManager::rebuildIndices() {
    tthIndex.clear();
    bloom.clear();

    for(auto& i: directories) {
        updateIndices(*i);
    }
}

void ShareManager::updateIndices(Directory& dir, const decltype(std::declval<Directory>().files.begin())& i) {
    const Directory::File& f = *i;

    auto j = tthIndex.find(f.getTTH());
    if(j == tthIndex.end()) {
        dir.size+=f.getSize();
    } else {
        if(!CTX_SETTING(LIST_DUPES) && !f.managed && !j->second->managed) {
            try {
                ctx().getLogManager()->message(str(F_("Duplicate file will not be shared: %1% (Size: %2% B) Dupe matched against: %3%")
                                                       % Util::addBrackets(dir.getRealPath(f.getName())) % Util::toString(f.getSize()) % Util::addBrackets(j->second->getParent()->getRealPath(j->second->getName()))));
                dir.files.erase(i);
            } catch (const ShareException&) {
            }
            return;
        }
    }

    dir.addType(getType(f.getName()));

    tthIndex.emplace(f.getTTH(), i);
    bloom.add(Text::toLower(f.getName()));
#ifdef WITH_DHT
    dht::DHT* dhtInst = ctx().getDHT();
    if(dhtInst && dhtInst->getIndexManager().isTimeForPublishing())
        dhtInst->getIndexManager().publishFile(f.getTTH(), f.getSize());
#endif
}

void ShareManager::refresh(bool dirs /* = false */, bool aUpdate /* = true */, bool block /* = false */) {
    if(refreshing.exchange(true) == true) {
        ctx().getLogManager()->message(_("File list refresh in progress, please wait for it to finish before trying to refresh again"));
        return;
    }
    ctx().getUploadManager()->updateLimits();

    update = aUpdate;
    refreshDirs = dirs;
    join();
    bool cached = false;
    if(initial) {
        cached = loadCache();
        initial = false;
    }
    try {
        start();
        if(block && !cached) {
            join();
        } else {
            setThreadPriority(Thread::LOW);
        }
    } catch(const ThreadException& e) {
        ctx().getLogManager()->message(str(F_("File list refresh failed: %1%") % e.getError()));
    }
}

StringPairList ShareManager::getDirectories() const {
    Lock l(cs);
    StringPairList ret;
    for(auto& i: shares) {
        ret.emplace_back(i.second, i.first);
    }
    return ret;
}

int ShareManager::run() {
    StringPairList dirs;
    uint64_t generation;
    {
        Lock l(cs);
        dirs = getDirectories();
        generation = manualSharesGeneration;
    }
    // Don't need to refresh if no directories are shared
    if(dirs.empty())
        refreshDirs = false;

    if(refreshDirs) {
        HashManager::HashPauser pauser(ctx());
        ctx().getLogManager()->message(_("File list refresh initiated"));

        DirList newDirs;
        for(auto& i: dirs) {
            if (checkHidden(i.second)) {
                auto dp = buildTree(i.second, Directory::Ptr());
                dp->setName(i.first);
                newDirs.emplace_back(dp);
            }
        }

        bool committed = false;
        {
            Lock l(cs);
            // A removed/renamed manual root must not return from an older scan,
            // especially after its virtual name has been reused by managed files.
            if(generation == manualSharesGeneration) {
                directories.clear();
                for(auto& i: newDirs) {
                    merge(i);
                }
                rebuildManagedDirectories();
                lastFullUpdate = GET_TICK();
                committed = true;
            }
        }
        refreshDirs = false;

        ctx().getLogManager()->message(committed ? _("File list refresh finished") :
            _("File list refresh discarded because shared directories changed"));
    }

    if(update) {
        ctx().getClientManager()->infoUpdated();
    }
    refreshing = false;
#ifdef WITH_DHT
    dht::DHT* dhtInst = ctx().getDHT();
    if(dhtInst && dhtInst->getIndexManager().isTimeForPublishing())
        dhtInst->getIndexManager().setNextPublishing();
#endif
    return 0;
}

void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h) const {
    dcdebug("Creating bloom filter, k=%u, m=%u, h=%u\n",
            static_cast<unsigned int>(k), static_cast<unsigned int>(m), static_cast<unsigned int>(h));
    Lock l(cs);

    HashBloom bloom;
    bloom.reset(k, m, h);
    for(auto& i: tthIndex) {
        if(i.second->available()) bloom.add(i.first);
    }
    bloom.copy_to(v);
}

void ShareManager::generateXmlList() {
    Lock l(cs);
    // Revalidate managed identities even when the ordinary list cache is young.
    if(!managedShares.empty()) forceXmlRefresh = true;
    if(forceXmlRefresh || (xmlDirty && (lastXmlUpdate + 15 * 60 * 1000 < GET_TICK() || lastXmlUpdate < lastFullUpdate))) {
        listN++;

        try {
            string tmp2;
            string indent;

            string newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files" + Util::toString(listN) + ".xml.bz2";
            {
                File f(newXmlName, File::WRITE, File::TRUNCATE | File::CREATE);
                // We don't care about the leaves...
                CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
                FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
                CountOutputStream<false> count(&bzipper);
                CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&count);

                newXmlFile.write(SimpleXML::utf8Header);
                newXmlFile.write("<FileListing Version=\"1\" CID=\"" + ctx().getClientManager()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n");
                for(auto& i: directories) {
                    i->toXml(newXmlFile, indent, tmp2, true);
                }
                newXmlFile.write("</FileListing>");
                newXmlFile.flush();

                xmlListLen = count.getCount();

                newXmlFile.getFilter().getTree().finalize();
                bzTree.getFilter().getTree().finalize();

                xmlRoot = newXmlFile.getFilter().getTree().getRoot();
                bzXmlRoot = bzTree.getFilter().getTree().getRoot();
            }
            const string XmlListFileName = Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2";
            if(bzXmlRef.get()) {
                bzXmlRef.reset();
                try {
                    File::renameFile(XmlListFileName, XmlListFileName + ".bak");
                } catch(const FileException&) { }
            }

            try {
                File::renameFile(newXmlName, XmlListFileName);
                newXmlName = XmlListFileName;
            } catch(const FileException&) {
                // Ignore, this is for caching only...
            }
            try {
                File::copyFile(XmlListFileName, XmlListFileName + ".bak");
            } catch(const FileException&) { }
            bzXmlRef = unique_ptr<File>(new File(newXmlName, File::READ, File::OPEN));
            setBZXmlFile(newXmlName);
            bzXmlListLen = File::getSize(newXmlName);
            ctx().getLogManager()->message(str(F_("File list %1% generated") % Util::addBrackets(bzXmlFile)));
        } catch(const Exception&) {
            // No new file lists...
        }

        xmlDirty = false;
        forceXmlRefresh = false;
        lastXmlUpdate = GET_TICK();
    }
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse) const {
    if(dir.empty() || dir[0] != '/' || dir[dir.size()-1] != '/')
        return 0;

    string xml = SimpleXML::utf8Header;
    string tmp;
    xml += "<FileListing Version=\"1\" CID=\"" + ctx().getClientManager()->getMe()->getCID().toBase32() + "\" Base=\"" + SimpleXML::escape(dir, tmp, false) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\"";
    StringRefOutputStream sos(xml);
    string indent = "\t";

    Lock l(cs);
    if(dir == "/") {
        sos.write(">\r\n");
        for(auto& i: directories) {
            tmp.clear();
            i->toXml(sos, indent, tmp, recurse);
        }
    } else {
        string::size_type i = 1, j = 1;

        Directory::Ptr root;

        bool first = true;
        while( (i = dir.find('/', j)) != string::npos) {
            if(i == j) {
                j++;
                continue;
            }

            if(first) {
                first = false;
                auto it = getByVirtual(dir.substr(j, i-j));

                if(it == directories.end())
                    return 0;
                root = *it;

            } else {
                auto it2 = root->directories.find(dir.substr(j, i-j));
                if(it2 == root->directories.end()) {
                    return 0;
                }
                root = it2->second;
            }
            j = i + 1;
        }

        if(!root)
            return 0;

        if(root->modified) sos.write(" BaseDate=\"" + Util::toString(root->modified) + "\"");
        sos.write(">\r\n");
        for(auto& it2: root->directories) {
            it2.second->toXml(sos, indent, tmp, recurse);
        }
        root->filesToXml(sos, indent, tmp);
    }

    xml += "</FileListing>";
    return new MemoryInputStream(xml);
}

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::Directory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const {
    xmlFile.write(indent);
    xmlFile.write(LITERAL("<Directory Name=\""));
    xmlFile.write(SimpleXML::escape(name, tmp2, true));
    // The public list is also the startup cache. Never reload owned snapshots
    // as manual shares, even if a manual root later reuses this virtual name.
    if(managedRoot) xmlFile.write(LITERAL("\" Managed=\"1"));
    if(modified) xmlFile.write("\" Date=\"" + Util::toString(modified));

    if(fullList) {
        xmlFile.write(LITERAL("\">\r\n"));

        indent += '\t';
        for(auto& i: directories) {
            i.second->toXml(xmlFile, indent, tmp2, fullList);
        }

        filesToXml(xmlFile, indent, tmp2);

        indent.erase(indent.length()-1);
        xmlFile.write(indent);
        xmlFile.write(LITERAL("</Directory>\r\n"));
    } else {
        if(directories.empty() && files.empty()) {
            xmlFile.write(LITERAL("\" />\r\n"));
        } else {
            xmlFile.write(LITERAL("\" Incomplete=\"1\" />\r\n"));
        }
    }
}

void ShareManager::Directory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2) const {
    for(auto& f: files) {
        if(!f.available()) continue;
        xmlFile.write(indent);
        xmlFile.write(LITERAL("<File Name=\""));
        xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
        xmlFile.write(LITERAL("\" Size=\""));
        xmlFile.write(Util::toString(f.getSize()));
        xmlFile.write(LITERAL("\" TTH=\""));
        tmp2.clear();
        xmlFile.write(f.getTTH().toBase32(tmp2));
        if(f.modified) xmlFile.write("\" Date=\"" + Util::toString(f.modified));
        xmlFile.write(LITERAL("\"/>\r\n"));
    }
}

// These ones we can look up as ints (4 bytes...)...

static const char* typeAudio[] = { ".mp3", ".mp2", ".mid", ".wav", ".ogg", ".wma", ".669", ".aac", ".aif", ".amf", ".ams", ".ape", ".dbm", ".dmf", ".dsm", ".far", ".mdl", ".med", ".mod", ".mol", ".mp1", ".mpa", ".mpc", ".mpp", ".mtm", ".nst", ".okt", ".psm", ".ptm", ".rmi", ".s3m", ".stm", ".ult", ".umx", ".wow" };
static const char* typeCompressed[] = { ".rar", ".zip", ".ace", ".arj", ".hqx", ".lha", ".sea", ".tar", ".tgz", ".uc2" };
static const char* typeDocument[] = { ".htm", ".doc", ".txt", ".nfo", ".pdf", ".chm", ".rtf",
                                      ".xls", ".ppt", ".odt", ".ods", ".odf", ".odp" };
static const char* typeExecutable[] = { ".exe", ".com", ".msi" };
static const char* typePicture[] = { ".jpg", ".gif", ".png", ".eps", ".img", ".pct", ".psp", ".pic", ".tif", ".rle", ".bmp", ".pcx", ".jpe", ".dcx", ".emf", ".ico", ".psd", ".tga", ".wmf", ".xif" };
static const char* typeVideo[] = { ".avi", ".mpg", ".mov", ".flv", ".asf",  ".pxp", ".wmv", ".ogm", ".mkv", ".m1v", ".m2v", ".mpe", ".mps", ".mpv", ".ram", ".vob", ".mp4" };
static const char* typeCDImage[] = {".iso", ".mdf", ".mds", ".nrg", ".vcd", ".bwt", ".ccd", ".cdi", ".pdi", ".cue", ".isz", ".img", ".vc4"};

static const string type2Audio[] = { ".au", ".it", ".ra", ".xm", ".aiff", ".flac", ".midi" };
static const string type2Picture[] = { ".ai", ".ps", ".pict", ".jpeg", ".tiff" };
static const string type2Video[] = { ".rm", ".divx", ".mpeg", ".mp1v", ".mp2v", ".mpv1", ".mpv2", ".qt", ".rv", ".vivo", ".ts", ".ps" };

#define IS_TYPE(x) ( type == (*((uint32_t*)x)) )
#define IS_TYPE2(x) (Util::stricmp(aString.c_str() + aString.length() - x.length(), x.c_str()) == 0)

static bool checkType(const string& aString, int aType) {
    if(aType == SearchManager::TYPE_ANY)
        return true;

    if(aString.length() < 5)
        return false;

    const char* c = aString.c_str() + aString.length() - 3;
    if(!Text::isAscii(c))
        return false;

    uint32_t type = '.' | (Text::asciiToLower(c[0]) << 8) | (Text::asciiToLower(c[1]) << 16) | (((uint32_t)Text::asciiToLower(c[2])) << 24);

    switch(aType) {
    case SearchManager::TYPE_AUDIO:
    {
        for(size_t i = 0; i < (sizeof(typeAudio) / sizeof(typeAudio[0])); i++) {
            if(IS_TYPE(typeAudio[i])) {
                return true;
            }
        }
        if( IS_TYPE2(type2Audio[0]) || IS_TYPE2(type2Audio[1]) || IS_TYPE2(type2Audio[2]) ) {
            return true;
        }
    }
        break;
    case SearchManager::TYPE_CD_IMAGE:
        for(size_t i = 0; i < (sizeof(typeCDImage) / sizeof(typeCDImage[0])); i++) {
            if(IS_TYPE(typeCDImage[i])) {
                return true;
            }
        }

        break;
    case SearchManager::TYPE_COMPRESSED:
        if( IS_TYPE(typeCompressed[0]) || IS_TYPE(typeCompressed[1]) || IS_TYPE(typeCompressed[2]) ) {
            return true;
        }
        break;
    case SearchManager::TYPE_DOCUMENT:
        if( IS_TYPE(typeDocument[0]) || IS_TYPE(typeDocument[1]) ||
                IS_TYPE(typeDocument[2]) || IS_TYPE(typeDocument[3]) ) {
            return true;
        }
        break;
    case SearchManager::TYPE_EXECUTABLE:
        if(IS_TYPE(typeExecutable[0]) ) {
            return true;
        }
        break;
    case SearchManager::TYPE_PICTURE:
    {
        for(size_t i = 0; i < (sizeof(typePicture) / sizeof(typePicture[0])); i++) {
            if(IS_TYPE(typePicture[i])) {
                return true;
            }
        }
        if( IS_TYPE2(type2Picture[0]) || IS_TYPE2(type2Picture[1]) || IS_TYPE2(type2Picture[2]) ) {
            return true;
        }
    }
        break;
    case SearchManager::TYPE_VIDEO:
    {
        for(size_t i = 0; i < (sizeof(typeVideo) / sizeof(typeVideo[0])); i++) {
            if(IS_TYPE(typeVideo[i])) {
                return true;
            }
        }
        if( IS_TYPE2(type2Video[0]) || IS_TYPE2(type2Video[1]) || IS_TYPE2(type2Video[2]) ) {
            return true;
        }
    }
        break;
    default:
        dcassert(0);
        break;
    }
    return false;
}

SearchManager::TypeModes ShareManager::getType(const string& aFileName) const {
    if(aFileName[aFileName.length() - 1] == PATH_SEPARATOR) {
        return SearchManager::TYPE_DIRECTORY;
    }

    if(checkType(aFileName, SearchManager::TYPE_VIDEO))
        return SearchManager::TYPE_VIDEO;
    else if(checkType(aFileName, SearchManager::TYPE_AUDIO))
        return SearchManager::TYPE_AUDIO;
    else if(checkType(aFileName, SearchManager::TYPE_COMPRESSED))
        return SearchManager::TYPE_COMPRESSED;
    else if(checkType(aFileName, SearchManager::TYPE_DOCUMENT))
        return SearchManager::TYPE_DOCUMENT;
    else if(checkType(aFileName, SearchManager::TYPE_EXECUTABLE))
        return SearchManager::TYPE_EXECUTABLE;
    else if(checkType(aFileName, SearchManager::TYPE_PICTURE))
        return SearchManager::TYPE_PICTURE;
    else if(checkType(aFileName, SearchManager::TYPE_CD_IMAGE))
        return SearchManager::TYPE_CD_IMAGE;

    return SearchManager::TYPE_ANY;
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults, ShareManager& sm) const {
    // Skip everything if there's nothing to find here (doh! =)
    if(!hasType(aFileType))
        return;

    StringSearch::List* cur = &aStrings;
    unique_ptr<StringSearch::List> newStr;

    // Find any matches in the directory name
    for(auto& k : aStrings) {
        if(k.match(name)) {
            if(!newStr.get()) {
                newStr = unique_ptr<StringSearch::List>(new StringSearch::List(aStrings));
            }
            newStr->erase(remove(newStr->begin(), newStr->end(), k), newStr->end());
        }
    }

    if(newStr.get() != 0) {
        cur = newStr.get();
    }

    bool sizeOk = (aSearchType != SearchManager::SIZE_ATLEAST) || (aSize == 0);
    if( (cur->empty()) &&
            (((aFileType == SearchManager::TYPE_ANY) && sizeOk) || (aFileType == SearchManager::TYPE_DIRECTORY)) ) {
        // We satisfied all the search words! Add the directory...(NMDC searches don't support directory size)
        SearchResultPtr sr(new SearchResult(sm.ctx(), SearchResult::TYPE_DIRECTORY, 0, getFullName(), TTHValue()));
        aResults.push_back(sr);
        sm.setHits(sm.getHits()+1);
    }

    if(aFileType != SearchManager::TYPE_DIRECTORY) {
        for(auto& i : files) {
            if(!i.available()) continue;

            if(aSearchType == SearchManager::SIZE_ATLEAST && aSize > i.getSize()) {
                continue;
            } else if(aSearchType == SearchManager::SIZE_ATMOST && aSize < i.getSize()) {
                continue;
            }
            auto j = cur->begin();
            for(; j != cur->end() && j->match(i.getName()); ++j)
                ;   // Empty

            if(j != cur->end())
                continue;

            // Check file type...
            if(checkType(i.getName(), aFileType)) {
                SearchResultPtr sr(new SearchResult(sm.ctx(), SearchResult::TYPE_FILE, i.getSize(), getFullName() + i.getName(), i.getTTH()));
                aResults.push_back(sr);
                sm.setHits(sm.getHits()+1);
                if(aResults.size() >= maxResults) {
                    break;
                }
            }
        }
    }

    for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
        l->second->search(aResults, *cur, aSearchType, aSize, aFileType, aClient, maxResults, sm);
    }
}

void ShareManager::search(SearchResultList& results, const string& aString, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) {
    Lock l(cs);
    if(aFileType == SearchManager::TYPE_TTH) {
        if(aString.compare(0, 4, "TTH:") == 0) {
            TTHValue tth(aString.substr(4));
            auto i = tthIndex.find(tth);
            if(i != tthIndex.end() && i->second->available()) {
                SearchResultPtr sr(new SearchResult(ctx(), SearchResult::TYPE_FILE, i->second->getSize(),
                                                    i->second->getParent()->getFullName() + i->second->getName(), i->second->getTTH()));

                results.push_back(sr);
                ctx().getShareManager()->addHits(1);
            }
        }
        return;
    }
    StringTokenizer<string> t(Text::toLower(aString), '$');
    StringList& sl = t.getTokens();
    if(!bloom.match(sl))
        return;

    StringSearch::List ssl;
    for(auto& i : sl) {
        if(!i.empty()) {
            ssl.push_back(StringSearch(i));
        }
    }
    if(ssl.empty())
        return;

    for(auto j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
        (*j)->search(results, ssl, aSearchType, aSize, aFileType, aClient, maxResults, *this);
    }
}

namespace {
inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

ShareManager::AdcSearch::AdcSearch(const StringList& adcParams) :
    include(&includeInit),
    gt(0),
    lt(numeric_limits<int64_t>::max()),
    hasRoot(false),
    isDirectory(false)
{
    for(auto& p: adcParams) {
        if(p.size() <= 2)
            continue;

        auto cmd = toCode(p[0], p[1]);
        if(toCode('T', 'R') == cmd) {
            hasRoot = true;
            root = TTHValue(p.substr(2));
            return;
        } else if(toCode('A', 'N') == cmd) {
            includeInit.emplace_back(p.substr(2));
        } else if(toCode('N', 'O') == cmd) {
            exclude.emplace_back(p.substr(2));
        } else if(toCode('E', 'X') == cmd) {
            ext.push_back(p.substr(2));
        } else if(toCode('G', 'R') == cmd) {
            auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
            ext.insert(ext.begin(), exts.begin(), exts.end());
        } else if(toCode('R', 'X') == cmd) {
            noExt.push_back(p.substr(2));
        } else if(toCode('G', 'E') == cmd) {
            gt = Util::toInt64(p.substr(2));
        } else if(toCode('L', 'E') == cmd) {
            lt = Util::toInt64(p.substr(2));
        } else if(toCode('E', 'Q') == cmd) {
            lt = gt = Util::toInt64(p.substr(2));
        } else if(toCode('T', 'Y') == cmd) {
            isDirectory = (p[2] == '2');
        }
    }
}

bool ShareManager::AdcSearch::isExcluded(const string& str) {
    for(auto& i : exclude) {
        if(i.match(str))
            return true;
    }
    return false;
}

bool ShareManager::AdcSearch::hasExt(const string& name) {
    if(ext.empty())
        return true;
    if(!noExt.empty()) {
        ext = StringList(ext.begin(), set_difference(ext.begin(), ext.end(), noExt.begin(), noExt.end(), ext.begin()));
        noExt.clear();
    }
    for(auto& i : ext) {
        if(name.length() >= i.length() && Util::stricmp(name.c_str() + name.length() - i.length(), i.c_str()) == 0)
            return true;
    }
    return false;
}

void ShareManager::Directory::search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ShareManager& sm) const {
    StringSearch::List* cur = aStrings.include;
    StringSearch::List* old = aStrings.include;

    unique_ptr<StringSearch::List> newStr;

    // Find any matches in the directory name
    for(auto k = cur->begin(); k != cur->end(); ++k) {
        if(k->match(name) && !aStrings.isExcluded(name)) {
            if(!newStr.get()) {
                newStr = unique_ptr<StringSearch::List>(new StringSearch::List(*cur));
            }
            newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
        }
    }

    if(newStr.get() != 0) {
        cur = newStr.get();
    }

    bool sizeOk = (aStrings.gt == 0);
    if( cur->empty() && aStrings.ext.empty() && sizeOk ) {
        // We satisfied all the search words! Add the directory...
        SearchResultPtr sr(new SearchResult(sm.ctx(), SearchResult::TYPE_DIRECTORY, getSize(), getFullName(), TTHValue()));
        aResults.push_back(sr);
        sm.setHits(sm.getHits()+1);
    }

    if(!aStrings.isDirectory) {
        for(auto& i : files) {
            if(!i.available()) continue;

            if(!(i.getSize() >= aStrings.gt)) {
                continue;
            } else if(!(i.getSize() <= aStrings.lt)) {
                continue;
            }

            if(aStrings.isExcluded(i.getName()))
                continue;

            auto j = cur->begin();
            for(; j != cur->end() && j->match(i.getName()); ++j)
                ;   // Empty

            if(j != cur->end())
                continue;

            // Check file type...
            if(aStrings.hasExt(i.getName())) {

                SearchResultPtr sr(new SearchResult(sm.ctx(), SearchResult::TYPE_FILE,
                                                    i.getSize(), getFullName() + i.getName(), i.getTTH()));
                aResults.push_back(sr);
                sm.addHits(1);
                if(aResults.size() >= maxResults) {
                    return;
                }
            }
        }
    }

    for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
        l->second->search(aResults, aStrings, maxResults, sm);
    }
    aStrings.include = old;
}

void ShareManager::search(SearchResultList& results, const StringList& params, StringList::size_type maxResults) {
    AdcSearch srch(params);

    Lock l(cs);

    if(srch.hasRoot) {
        auto i = tthIndex.find(srch.root);
        if(i != tthIndex.end() && i->second->available()) {
            SearchResultPtr sr(new SearchResult(ctx(), SearchResult::TYPE_FILE,
                                                i->second->getSize(), i->second->getParent()->getFullName() + i->second->getName(),
                                                i->second->getTTH()));
            results.push_back(sr);
            addHits(1);
        }
        return;
    }

    for(auto i = srch.includeInit.begin(); i != srch.includeInit.end(); ++i) {
        if(!bloom.match(i->getPattern()))
            return;
    }

    for(auto j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
        (*j)->search(results, srch, maxResults, *this);
    }
}

ShareManager::Directory::Ptr ShareManager::getDirectory(const string& fname) {
    for(auto& mi : shares) {
        if(Util::strnicmp(fname, mi.first, mi.first.length()) == 0) {
            Directory::Ptr d;
            for(auto& i: directories) {
                if(Util::stricmp(i->getName(), mi.second) == 0) {
                    d = i;
                }
            }

            if(!d) {
                return Directory::Ptr();
            }

            string::size_type i;
            string::size_type j = mi.first.length();
            while((i = fname.find(PATH_SEPARATOR, j)) != string::npos) {
                auto dmi = d->directories.find(fname.substr(j, i-j));
                j = i + 1;
                if(dmi == d->directories.end())
                    return Directory::Ptr();
                d = dmi->second;
            }
            return d;
        }
    }
    return Directory::Ptr();
}

void ShareManager::on(QueueManagerListener::FileMoved, const string& realPath) noexcept {
    if(CTX_BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
        // Check if finished download is supposed to be shared
        Lock l(cs);
        for(auto& i: shares) {
            if(Util::strnicmp(i.first, realPath, i.first.size()) == 0 && realPath[i.first.size() - 1] == PATH_SEPARATOR) {
                try {
                    // Schedule for hashing, it'll be added automatically later on...
                    ctx().getHashManager()->checkTTH(realPath, File::getSize(realPath), 0);
                } catch(const Exception&) {
                    // Not a vital feature...
                }
                break;
            }
        }
    }
}

void ShareManager::on(HashManagerListener::TTHDone, const string& realPath, const TTHValue& root) noexcept {
    Lock l(cs);
    Directory::Ptr d = getDirectory(realPath);
    if(d) {
        auto i = d->findFile(Util::getFileName(realPath));
        if(i != d->files.end()) {
            if(root != i->getTTH())
                tthIndex.erase(i->getTTH());
            // Get rid of false constness...
            auto f = const_cast<Directory::File*>(&(*i));
            f->setTTH(root);
            // TTHDone supplies no matching file identity/mtime. A later scan
            // may restore Date, but never pair a new timestamp with this hash.
            f->modified = 0;
            tthIndex.emplace(f->getTTH(), i);
        } else {
            string name = Util::getFileName(realPath);
            int64_t size = File::getSize(realPath);
            auto it = d->files.insert(Directory::File(name, size, d, root)).first;
            updateIndices(*d, it);
        }
        setDirty();
        forceXmlRefresh = true;
    }
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) {
    if (CTX_SETTING(AUTO_REFRESH_TIME) > 0) {
        if(lastFullUpdate + CTX_SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
            refresh(true, true);
        }
    }
}

} // namespace dcpp
