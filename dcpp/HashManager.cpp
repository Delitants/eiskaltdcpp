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

#include "HashManager.h"

#include <memory>
#include <filesystem>

#include "File.h"
#include "LogManager.h"
#include "ScopedFunctor.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "SFVReader.h"
#include "ZUtils.h"

#ifndef _WIN32
#include <sys/mman.h> // mmap, munmap, madvise
#include <signal.h>  // for handling read errors from previous trio
#include <setjmp.h>
#endif

#ifdef USE_XATTR
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/xattr.h>
#include "DCPlusPlus.h"
#endif

namespace dcpp {

#define HASH_FILE_VERSION_STRING "2"
static const uint32_t HASH_FILE_VERSION = 2;
const int64_t HashManager::MIN_BLOCK_SIZE = 64 * 1024;
const string HashManager::StreamStore::g_streamName(".gltth");

inline void HashManager::StreamStore::setCheckSum(TTHStreamHeader& p_header) {
    p_header.magic = g_MAGIC;
    uint32_t l_sum = 0;

    for (size_t i = 0; i < sizeof(TTHStreamHeader) / sizeof(uint32_t); i++)
        l_sum ^= ((uint32_t*) & p_header)[i];

    p_header.checksum ^= l_sum;
}

inline bool HashManager::StreamStore::validateCheckSum(const TTHStreamHeader& p_header) {
    if (p_header.magic != g_MAGIC)
        return false;

    uint32_t l_sum = 0;

    for (size_t i = 0; i < sizeof(TTHStreamHeader) / sizeof(uint32_t); i++)
        l_sum ^= ((uint32_t*) & p_header)[i];

    return (l_sum == 0);
}

#ifdef USE_XATTR
static const uint64_t SIGNIFIC_VALUE    = 10000000;
static const uint64_t NTFS_TIME_OFFSET  = ((uint64_t)(369 * 365 + 89) * 24 * 3600 * SIGNIFIC_VALUE);

static uint64_t getTimeStamp(const string &fname){
    struct stat st;

    /* WARNING: this is not completely portable conversion!
       For more information about portable conversion of linux time to windows filetime see
       ntfs-3g_ntfsprogs/include/ntfs-3g/ntfstime.h from NTFS-3G sources. */
    if (::stat(fname.c_str(), &st) == 0)
        return (uint64_t)st.st_mtime * SIGNIFIC_VALUE + NTFS_TIME_OFFSET + st.st_mtim.tv_nsec/100;

    return 0;
}

#endif // USE_XATTR

bool HashManager::StreamStore::loadTree(const string& p_filePath, TigerTree &tree, int64_t p_aFileSize)
{
#ifdef USE_XATTR
    const int64_t fileSize  = (p_aFileSize == -1) ? File::getSize(p_filePath) : p_aFileSize;
    const size_t hdrSz      = sizeof(TTHStreamHeader);
    const size_t totalSz    = ATTR_MAX_VALUELEN;
    const size_t blockSize  = totalSz;

    std::unique_ptr<uint8_t[]> buf(new uint8_t[blockSize]);

    if (getxattr(p_filePath.c_str(), g_streamName.c_str(), (char*)(void*)buf.get(), blockSize) == 0) {
        const TTHStreamHeader& h = reinterpret_cast<const TTHStreamHeader&>(*buf.get());

        printf("%s: timestamps header=0x%llx, current=0x%llx, difference(should be zero)=%llx\n",
               p_filePath.c_str(),
               (long long unsigned int)h.timeStamp,
               (long long unsigned int)getTimeStamp(p_filePath),
               (long long unsigned int)(h.timeStamp - getTimeStamp(p_filePath)));

        if (!(h.timeStamp == getTimeStamp(p_filePath) && validateCheckSum(h))) { // File was modified and we should reset attr.
            deleteStream(p_filePath);

            return false;
        }

        const size_t datalen = blockSize - hdrSz;
        std::unique_ptr<uint8_t[]> tail(new uint8_t[datalen]);

        memcpy(tail.get(), (uint8_t*)buf.get() + hdrSz, datalen);

        TigerTree p_Tree = TigerTree(fileSize, h.blockSize, tail.get());

        if (p_Tree.getRoot() == h.root){
            tree = p_Tree;

            return true;
        }
        else
            return false;
    }
#else
    (void)p_filePath;
    (void)tree;
    (void)p_aFileSize;
#endif //USE_XATTR
    return false;
}

bool HashManager::StreamStore::saveTree(const string& p_filePath, const TigerTree& p_Tree)
{
#ifdef USE_XATTR
    TTHStreamHeader h;

    h.fileSize = File::getSize(p_filePath);
    h.timeStamp = getTimeStamp(p_filePath);
    h.root = p_Tree.getRoot();
    h.blockSize = p_Tree.getBlockSize();

    setCheckSum(h);
    {
        const size_t sz = sizeof(TTHStreamHeader) + p_Tree.getLeaves().size() * TTHValue::BYTES;
        std::unique_ptr<uint8_t[]> buf(new uint8_t[sz]);

        memcpy(buf.get(), &h, sizeof(TTHStreamHeader));
        memcpy(buf.get() + sizeof(TTHStreamHeader), p_Tree.getLeaves()[0].data, p_Tree.getLeaves().size() * TTHValue::BYTES);

        return (setxattr(p_filePath.c_str(), g_streamName.c_str(), (char*)(void*)buf.get(), sz, 0) == 0);
    }
#else // USE_XATTR
    (void)p_filePath;
    (void)p_Tree;
#endif // USE_XATTR
    return false;
}

void HashManager::StreamStore::deleteStream(const string& p_filePath)
{
#ifdef USE_XATTR
    printf("Resetting Xattr for %s\n", p_filePath.c_str());
    removexattr(p_filePath.c_str(), g_streamName.c_str());
#else
    (void)p_filePath;
#endif //USE_XATTR
}

bool HashManager::beginOperation() {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(shutdownRequested) return false;
    ++operations;
    return true;
}

void HashManager::finishOperation() {
    std::lock_guard<std::mutex> lock(operationMutex);
    if(--operations == 0) operationsChanged.notify_all();
}

void HashManager::stopForShutdown() {
    {
        std::unique_lock<std::mutex> lock(operationMutex);
        if(shutdownRequested) {
            operationsChanged.wait(lock, [this] { return shutdownComplete; });
            return;
        }
        shutdownRequested = true;
    }
    // Wake paused reads before waiting for publication callbacks. No manager
    // lock is held across the timer drain, operation drain or worker join.
    hasher.shutdown();
    ctx().getTimerManager()->removeListener(this);
    {
        std::unique_lock<std::mutex> lock(operationMutex);
        operationsChanged.wait(lock, [this] { return operations == 0; });
    }
    hasher.join();
    {
        std::lock_guard<std::mutex> lock(operationMutex);
        shutdownComplete = true;
        operationsChanged.notify_all();
    }
}

void HashManager::startup() {
    if(!beginOperation()) return;
    ScopedFunctor([this] { finishOperation(); });
    hasher.start();
}

void HashManager::addTree(const TigerTree& tree) {
    if(!beginOperation()) return;
    ScopedFunctor([this] { finishOperation(); });
    Lock l(cs);
    store.addTree(tree);
}

void HashManager::doRebuild() {
    if(!beginOperation()) return;
    ScopedFunctor([this] { finishOperation(); });
    Lock l(cs);
    store.rebuild();
}

void HashManager::on(TimerManagerListener::Minute, uint64_t) {
    if(!beginOperation()) return;
    ScopedFunctor([this] { finishOperation(); });
    Lock l(cs);
    store.save();
}

bool HashManager::checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) {
    const bool admitted = beginOperation();
    ScopedFunctor([&] { if(admitted) finishOperation(); });
    Lock l(cs);
    if(!admitted) return store.checkTTH(aFileName, aSize, aTimeStamp, false);

    const TTHValue* tthold = getFileTTHif(Text::toLower(aFileName));
    const TTHValue* tth = getFileTTHif(aFileName);
    if (tthold != NULL && tth == NULL) {
        TigerTree tt(MIN_BLOCK_SIZE);
        store.getTree(*tthold, tt);
        hashDone(aFileName, aTimeStamp, tt, 0, aSize);

        m_streamstore.saveTree(aFileName, tt);

        return true;
    }
    else if (!store.checkTTH(aFileName, aSize, aTimeStamp)) {
        hasher.hashFile(aFileName, aSize);
        return false;
    }
    return true;
}

TTHValue HashManager::getTTH(const string& aFileName, int64_t aSize) {
    const bool admitted = beginOperation();
    ScopedFunctor([&] { if(admitted) finishOperation(); });
    Lock l(cs);
    const TTHValue* tth = store.getTTH(aFileName, admitted);
    if (tth == NULL) {
        if(admitted) hasher.hashFile(aFileName, aSize);
        throw HashException();
    }
    return *tth;
}

const TTHValue* HashManager::getFileTTHif(const string& aFileName) {
    const bool admitted = beginOperation();
    ScopedFunctor([&] { if(admitted) finishOperation(); });
    Lock l(cs);
    return store.getTTH(aFileName, admitted);
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) {
    Lock l(cs);
    return store.getTree(root, tt);
}

int64_t HashManager::getBlockSize(const TTHValue& root) {
    Lock l(cs);
    return store.getBlockSize(root);
}

void HashManager::hashDone(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, int64_t speed, int64_t size) {
    if(!beginOperation()) return;
    ScopedFunctor([this] { finishOperation(); });
    try {
        Lock l(cs);
        store.addFile(aFileName, aTimeStamp, tth, true);
        m_streamstore.saveTree(aFileName, tth);
    } catch (const Exception& e) {
        ctx().getLogManager()->message(str(F_("Hashing failed: %1%") % e.getError()));
        return;
    }

    fire(HashManagerListener::TTHDone(), aFileName, tth.getRoot());

    if (speed > 0) {
        ctx().getLogManager()->message(str(F_("Finished hashing: %1% (%2% at %3%/s)") % Util::addBrackets(aFileName) %
                                               Util::formatBytes(size) % Util::formatBytes(speed)));
    } else if(size >= 0) {
        ctx().getLogManager()->message(str(F_("Finished hashing: %1% (%2%)") % Util::addBrackets(aFileName) %
                                               Util::formatBytes(size)));
    } else {
        ctx().getLogManager()->message(str(F_("Finished hashing: %1%") % Util::addBrackets(aFileName)));
    }
}

void HashManager::HashStore::addFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aUsed) {
    addTree(tth);

    auto fname = Util::getFileName(aFileName), fpath = Util::getFilePath(aFileName);

    auto& fileList = fileIndex[fpath];

    auto j = find(fileList.begin(), fileList.end(), fname);
    if (j != fileList.end()) {
        fileList.erase(j);
    }

    fileList.emplace_back(fname, tth.getRoot(), aTimeStamp, aUsed);
    dirty = true;
}

void HashManager::HashStore::addTree(const TigerTree& tt) {
    if (treeIndex.find(tt.getRoot()) == treeIndex.end()) {
        if(dataInsertionFailed)
            throw HashException(_("Hash data storage failed; restart before adding new hashes"));
        try {
            if(!storageReady()) throw HashException(_("Hash data storage is unavailable"));
            File f(activeDataFile, File::READ | File::WRITE, File::OPEN);
            int64_t index = saveTree(f, tt, dataPositionValidated ? sizeof(int64_t) : minimumDataPosition());
            treeIndex.emplace(tt.getRoot(), TreeInfo(tt.getFileSize(), index, tt.getBlockSize()));
            if(index != SMALL_TREE) dataPositionValidated = true;
            dirty = true;
        } catch(const FileException& e) {
            dataInsertionFailed = true;
            ctx().getLogManager()->message(str(F_("Error saving hash data: %1%") % e.getError()));
            throw;
        } catch(...) {
            dataInsertionFailed = true;
            throw;
        }
    }
}

int64_t HashManager::HashStore::treeBytes(int64_t size, int64_t blockSize) {
    if(size < 0 || blockSize < 1024 || size > std::numeric_limits<int64_t>::max() - (blockSize - 1))
        throw HashException(_("Invalid hash data tree"));
    const int64_t blocks = max(int64_t(1), size / blockSize + (size % blockSize != 0));
    if(blocks > std::numeric_limits<int64_t>::max() / TTHValue::BYTES ||
            static_cast<uint64_t>(blocks) > std::numeric_limits<size_t>::max() / TTHValue::BYTES)
        throw HashException(_("Invalid hash data tree"));
    return blocks * TTHValue::BYTES;
}

int64_t HashManager::HashStore::minimumDataPosition() const {
    int64_t end = sizeof(int64_t);
    // Scan loaded extents once per generation, not once per append. A damaged
    // cursor must not overwrite leaves still referenced by the persisted index.
    for(const auto& entry : treeIndex) {
        const auto& ti = entry.second;
        if(ti.getIndex() == SMALL_TREE) continue;
        const int64_t bytes = treeBytes(ti.getSize(), ti.getBlockSize());
        if(ti.getIndex() < static_cast<int64_t>(sizeof(int64_t)) ||
                ti.getIndex() > std::numeric_limits<int64_t>::max() - bytes)
            throw HashException(_("Invalid hash data tree"));
        end = max(end, ti.getIndex() + bytes);
    }
    return end;
}

int64_t HashManager::HashStore::saveTree(File& f, const TigerTree& tt, int64_t minimumPosition) {
    const int64_t bytes = treeBytes(tt.getFileSize(), tt.getBlockSize());
    if(tt.getLeaves().size() != static_cast<size_t>(bytes / TTHValue::BYTES))
        throw HashException(_("Invalid hash data tree"));
    if (tt.getLeaves().size() == 1)
        return SMALL_TREE;

    f.setPos(0);
    int64_t pos = 0;
    size_t n = sizeof(pos);
    if (f.read(&pos, n) != sizeof(pos))
        throw HashException(_("Unable to read hash data file"));

    const int64_t datsz = f.getSize();
    if(pos < minimumPosition || pos > datsz || pos > std::numeric_limits<int64_t>::max() - bytes)
        throw HashException(_("Invalid hash data write position"));
    const int64_t end = pos + bytes;
    if(end > datsz) {
        const int64_t growth = min(std::numeric_limits<int64_t>::max() - datsz, int64_t(1024 * 1024));
        f.setSize(max(end, datsz + growth));
    }
    f.setPos(pos);
    if(f.getPos() != pos || f.write(tt.getLeaves()[0].data, static_cast<size_t>(bytes)) != static_cast<size_t>(bytes))
        throw FileException(_("Unable to write hash data file"));
    f.flush();
    f.setPos(0);
    if(f.getPos() != 0 || f.write(&end, sizeof(end)) != sizeof(end))
        throw FileException(_("Unable to write hash data file"));
    f.flush();
    return pos;
}

bool HashManager::HashStore::loadTree(File& f, const TreeInfo& ti, const TTHValue& root, TigerTree& tt) {
    if (ti.getIndex() == SMALL_TREE) {
        tt = TigerTree(ti.getSize(), ti.getBlockSize(), root);
        return true;
    }
    try {
        const int64_t bytes = treeBytes(ti.getSize(), ti.getBlockSize());
        const int64_t size = f.getSize();
        if(ti.getIndex() < static_cast<int64_t>(sizeof(int64_t)) || size < bytes || ti.getIndex() > size - bytes)
            return false;
        f.setPos(ti.getIndex());
        if(f.getPos() != ti.getIndex()) return false;
        size_t datalen = static_cast<size_t>(bytes);
        std::unique_ptr<uint8_t[]> buf(new uint8_t[datalen]);
        if(f.read(&buf[0], datalen) != static_cast<size_t>(bytes)) return false;
        TigerTree loaded(ti.getSize(), ti.getBlockSize(), &buf[0]);
        if (!(loaded.getRoot() == root))
            return false;
        tt = std::move(loaded);
    } catch (const Exception&) {
        return false;
    }

    return true;
}

bool HashManager::HashStore::getTree(const TTHValue& root, TigerTree& tt) {
    auto i = treeIndex.find(root);
    if (i == treeIndex.end())
        return false;
    try {
        File f(activeDataFile, File::READ, File::OPEN);
        return loadTree(f, i->second, root, tt);
    } catch (const Exception&) {
        return false;
    }
}

int64_t HashManager::HashStore::getBlockSize(const TTHValue& root) const {
    auto i = treeIndex.find(root);
    return i == treeIndex.end() ? 0 : i->second.getBlockSize();
}

bool HashManager::HashStore::checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp, bool invalidate) {
    auto fname = Util::getFileName(aFileName), fpath = Util::getFilePath(aFileName);
    auto i = fileIndex.find(fpath);
    if (i != fileIndex.end()) {
        auto j = find(i->second.begin(), i->second.end(), fname);
        if (j != i->second.end()) {
            FileInfo& fi = *j;
            auto ti = treeIndex.find(fi.getRoot());
            if (ti == treeIndex.end() || ti->second.getSize() != aSize || fi.getTimeStamp() != aTimeStamp) {
                if(invalidate) {
                    i->second.erase(j);
                    dirty = true;
                }
                return false;
            }
            return true;
        }
    }
    return false;
}

const TTHValue* HashManager::HashStore::getTTH(const string& aFileName, bool markUsed) {
    string fname = Util::getFileName(aFileName);
    string fpath = Util::getFilePath(aFileName);

    auto i = fileIndex.find(fpath);
    if (i != fileIndex.end()) {
        auto j = find(i->second.begin(), i->second.end(), fname);
        if (j != i->second.end()) {
            if(markUsed) j->setUsed(true);
            return &(j->getRoot());
        }
    }
    return NULL;
}

void HashManager::HashStore::rebuild() {
    (void)rebuildChecked();
}

bool HashManager::HashStore::rebuildChecked() {
    try {
        if(!storageReady() || dataInsertionFailed) throw HashException(_("Hash data storage is unavailable"));
        auto state = prepareRebuild();
        commitRebuild(state);
        // Reclamation is optional after commit, never installer consent. Keep
        // the legacy file as migration evidence, but do not accumulate old
        // committed generations. Interrupted, unreferenced stages are retained.
        try {
            if(isGenerationName(Util::getFileName(state.dataFile))) File::deleteFile(state.dataFile);
        } catch(...) { }
        return true;
    } catch (const Exception& e) {
        rebuildFailed = true;
        ctx().getLogManager()->message(str(F_("Hashing failed: %1%") % e.getError()));
        return false;
    } catch(const std::filesystem::filesystem_error& e) {
        rebuildFailed = true;
        ctx().getLogManager()->message(str(F_("Hashing failed: %1%") % e.what()));
        return false;
    } catch(...) {
        rebuildFailed = true;
        throw;
    }
}

bool HashManager::HashStore::isGenerationName(const string& name) {
    const string prefix = "HashData.rebuild.", suffix = ".dat";
    if(name.size() <= prefix.size() + suffix.size() || name.size() > prefix.size() + 20 + suffix.size()
        || name.compare(0, prefix.size(), prefix) != 0
        || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    return all_of(name.begin() + prefix.size(), name.end() - suffix.size(), [](char c) { return c >= '0' && c <= '9'; });
}

HashManager::HashStore::RebuildState HashManager::HashStore::prepareRebuild() {
    RebuildState state;
    auto& newTreeIndex = state.trees;
    auto& newFileIndex = state.files;

    for (auto& i: fileIndex) {
        for (auto& j : i.second) {
            if (!j.getUsed())
                continue;

            auto k = treeIndex.find(j.getRoot());
            if(k == treeIndex.end()) throw HashException(_("Hash data storage is unavailable"));
            newTreeIndex[j.getRoot()] = k->second;
        }
    }

    // A generation is never the currently referenced data. The flushed
    // XML below is the only commit point; an interrupted stage is an orphan.
    do {
        const uint64_t id = (uint64_t(Util::rand()) << 32) | Util::rand();
        state.dataFile = Util::getPath(Util::PATH_USER_CONFIG) + "HashData.rebuild." + Util::toString(id) + ".dat";
    } while(std::filesystem::symlink_status(std::filesystem::u8path(state.dataFile)).type() != std::filesystem::file_type::not_found);
    bool prepared = false;
    ScopedFunctor([&] { if(!prepared) File::deleteFile(state.dataFile); });
    createDataFile(state.dataFile);

    {
        File in(activeDataFile, File::READ, File::OPEN);
        File out(state.dataFile, File::READ | File::WRITE, File::OPEN);

        for (auto i = newTreeIndex.begin(); i != newTreeIndex.end();) {
            TigerTree tree;
            if (loadTree(in, i->second, i->first, tree)) {
                i->second.setIndex(saveTree(out, tree));
                ++i;
            } else {
                throw HashException(_("Hash data storage is unavailable"));
            }
        }
    }

    for (auto& i: fileIndex) {
        auto fi = newFileIndex.emplace(i.first, vector<FileInfo>()).first;

        for (auto& j: i.second) {
            if (j.getUsed() && newTreeIndex.find(j.getRoot()) != newTreeIndex.end()) {
                fi->second.push_back(j);
            }
        }

        if (fi->second.empty())
            newFileIndex.erase(fi);
    }

    writeIndex(getIndexFile() + ".tmp", state.trees, state.files, state.dataFile);
    prepared = true;
    return state;
}

void HashManager::HashStore::commitRebuild(RebuildState& state) {
    File::replaceFile(getIndexFile() + ".tmp", getIndexFile());
    // No allocation or throwing I/O after the commit point. Optional old-data
    // reclamation is done by the caller, outside the committed state switch.
    treeIndex.swap(state.trees);
    fileIndex.swap(state.files);
    activeDataFile.swap(state.dataFile);
    dataPositionValidated = false;
    dirty = false;
}

bool HashManager::saveIndexChecked() {
    Lock l(cs);
    return store.saveChecked();
}

bool HashManager::dataInsertionsSucceeded() const {
    Lock l(cs);
    return store.dataInsertionsSucceeded();
}

bool HashManager::dataStorageSucceeded() const {
    Lock l(cs);
    return store.storageReady() && store.dataInsertionsSucceeded() && store.rebuildsSucceeded();
}

void HashManager::HashStore::save() {
    (void)saveChecked();
}

bool HashManager::HashStore::saveChecked() {
    if(!storageReady()) return false;
    if (dirty) {
        try {
            writeIndex(getIndexFile() + ".tmp", treeIndex, fileIndex, activeDataFile);
            File::replaceFile(getIndexFile() + ".tmp", getIndexFile());
            dirty = false;
        } catch (const FileException& e) {
            ctx().getLogManager()->message(str(F_("Error saving hash data: %1%") % e.getError()));
            return false;
        }
    }
    return true;
}

void HashManager::HashStore::writeIndex(const string& path, const TreeIndex& trees, const FileIndex& files, const string& dataFile) {
    File ff(path, File::WRITE, File::CREATE | File::TRUNCATE);
    BufferedOutputStream<false> f(&ff);

    string tmp;
    string b32tmp;

    f.write(SimpleXML::utf8Header);
    if(dataFile == getDataFile()) {
        f.write(LIT("<HashStore Version=\"" HASH_FILE_VERSION_STRING "\">\r\n"));
    } else {
        f.write("<HashStore Version=\"3\" DataFile=\"" + Util::getFileName(dataFile) + "\">\r\n");
    }

    f.write(LIT("\t<Trees>\r\n"));

    for (auto& i: trees) {
        const TreeInfo& ti = i.second;
        f.write(LIT("\t\t<Hash Type=\"TTH\" Index=\""));
        f.write(Util::toString(ti.getIndex()));
        f.write(LIT("\" BlockSize=\""));
        f.write(Util::toString(ti.getBlockSize()));
        f.write(LIT("\" Size=\""));
        f.write(Util::toString(ti.getSize()));
        f.write(LIT("\" Root=\""));
        b32tmp.clear();
        f.write(i.first.toBase32(b32tmp));
        f.write(LIT("\"/>\r\n"));
    }

    f.write(LIT("\t</Trees>\r\n\t<Files>\r\n"));

    for (auto& i: files) {
        const string& dir = i.first;
        for (auto& fi: i.second) {
            f.write(LIT("\t\t<File Name=\""));
            f.write(SimpleXML::escape(dir + fi.getFileName(), tmp, true));
            f.write(LIT("\" TimeStamp=\""));
            f.write(Util::toString(fi.getTimeStamp()));
            f.write(LIT("\" Root=\""));
            b32tmp.clear();
            f.write(fi.getRoot().toBase32(b32tmp));
            f.write(LIT("\"/>\r\n"));
        }
    }
    f.write(LIT("\t</Files>\r\n</HashStore>"));
    f.flush();
    ff.close();
}

string HashManager::HashStore::getIndexFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml"; }
string HashManager::HashStore::getDataFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat"; }

class HashLoader: public SimpleXMLReader::CallBack {
public:
    HashLoader(HashManager::HashStore& s) :
        store(s),
        size(0),
        timeStamp(0),
        version(HASH_FILE_VERSION),
        inTrees(false),
        inFiles(false),
        inHashStore(false)
    { }
    void startTag(const string& name, StringPairList& attribs, bool simple);
    void endTag(const string& name) override {
        if(name == "Trees") inTrees = false;
        if(name == "Files") inFiles = false;
        if(name == "HashStore") { inHashStore = false; complete = true; }
    }
    bool valid() const { return seenRoot && complete; }

private:
    HashManager::HashStore& store;

    string file;
    int64_t size;
    uint32_t timeStamp;
    int version;

    bool inTrees;
    bool inFiles;
    bool inHashStore;
    bool seenRoot = false;
    bool complete = false;
};

void HashManager::HashStore::load() {
    if(initializationFailed) return;
    dataPositionValidated = false;
    dataReady = false;
    treeIndex.clear(); fileIndex.clear();
    activeDataFile = getDataFile();
    try {
        Util::migrate(getIndexFile());
        const bool indexed = std::filesystem::exists(std::filesystem::u8path(getIndexFile()));
        if(indexed) {
            HashLoader l(*this);
            File f(getIndexFile(), File::READ, File::OPEN);
            SimpleXMLReader(&l).parse(f);
            if(!l.valid()) throw HashException(_("Hash data storage is unavailable"));
        } else if(!std::filesystem::exists(std::filesystem::u8path(activeDataFile))) {
            createDataFile(activeDataFile + ".tmp");
            File::replaceFile(activeDataFile + ".tmp", activeDataFile);
        }
        if(activeDataFile != getDataFile() && !std::filesystem::is_regular_file(std::filesystem::symlink_status(std::filesystem::u8path(activeDataFile))))
            throw HashException(_("Hash data storage is unavailable"));
        File f(activeDataFile, File::READ, File::OPEN);
        int64_t position = 0; size_t length = sizeof(position);
        if(f.read(&position, length) != sizeof(position) || position < minimumDataPosition() || position > f.getSize())
            throw HashException(_("Hash data storage is unavailable"));
        dataReady = true;
    } catch (const Exception& e) {
        initializationFailed = true;
        ctx().getLogManager()->message(str(F_("Error creating hash data file: %1%") % e.getError()));
    } catch(const std::filesystem::filesystem_error& e) {
        initializationFailed = true;
        ctx().getLogManager()->message(str(F_("Error creating hash data file: %1%") % e.what()));
    } catch(...) {
        initializationFailed = true;
        throw;
    }
}

static const string sHashStore = "HashStore";
static const string sversion = "version"; // Oops, v1 was like this
static const string sVersion = "Version";
static const string sTrees = "Trees";
static const string sFiles = "Files";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sHash = "Hash";
static const string sType = "Type";
static const string sTTH = "TTH";
static const string sIndex = "Index";
static const string sLeafSize = "LeafSize"; // Residue from v1 as well
static const string sBlockSize = "BlockSize";
static const string sTimeStamp = "TimeStamp";
static const string sRoot = "Root";

void HashLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
    if (name == sHashStore) {
        if(seenRoot) throw HashException(_("Hash data storage is unavailable"));
        seenRoot = true;
        complete = simple;
        version = Util::toInt(getAttrib(attribs, sVersion, 0));
        if (version == 0) {
            version = Util::toInt(getAttrib(attribs, sversion, 0));
        }
        if(version != 2 && version != 3) throw HashException(_("Hash data storage is unavailable"));
        const auto& dataFile = getAttrib(attribs, "DataFile", 0);
        if(version == 3) {
            if(!HashManager::HashStore::isGenerationName(dataFile)) throw HashException(_("Hash data storage is unavailable"));
            store.activeDataFile = Util::getPath(Util::PATH_USER_CONFIG) + dataFile;
        } else if(!dataFile.empty()) {
            throw HashException(_("Hash data storage is unavailable"));
        }
        inHashStore = !simple;
    } else if (inHashStore && (version == 2 || version == 3)) {
        if (inTrees && name == sHash) {
            const string& type = getAttrib(attribs, sType, 0);
            int64_t index = Util::toInt64(getAttrib(attribs, sIndex, 1));
            int64_t blockSize = Util::toInt64(getAttrib(attribs, sBlockSize, 2));
            int64_t size = Util::toInt64(getAttrib(attribs, sSize, 3));
            const string& root = getAttrib(attribs, sRoot, 4);
            if (!root.empty() && type == sTTH && (index >= 8 || index == HashManager::SMALL_TREE) && blockSize >= 1024) {
                store.treeIndex[TTHValue(root)] = HashManager::HashStore::TreeInfo(size, index, blockSize);
            }
        } else if (inFiles && name == sFile) {
            file = getAttrib(attribs, sName, 0);
            timeStamp = Util::toUInt32(getAttrib(attribs, sTimeStamp, 1));
            const string& root = getAttrib(attribs, sRoot, 2);

            if (!file.empty() && size >= 0 && timeStamp > 0 && !root.empty()) {
                string fname = Util::getFileName(file);
                string fpath = Util::getFilePath(file);

                store.fileIndex[fpath].emplace_back(fname, TTHValue(root), timeStamp, false);
            }
        } else if (name == sTrees) {
            inTrees = !simple;
        } else if (name == sFiles) {
            inFiles = !simple;
        }
    }
}

HashManager::HashStore::HashStore(DCContext& ctx) :
    dirty(false), ctx_(ctx) {

    Util::migrate(getDataFile());

    load();
}

/**
 * Creates the data files for storing hash values.
 * The data file is very simple in its format. The first 8 bytes
 * are filled with an int64_t (little endian) of the next write position
 * in the file counting from the start (so that file can be grown in chunks).
 * We start with a 1 mb file, and then grow it as needed to avoid fragmentation.
 * To find data inside the file, use the corresponding index file.
 * Rebuild compacts into a fresh generation selected by one XML replacement.
 */
void HashManager::HashStore::createDataFile(const string& name) {
    File dat(name, File::WRITE, File::CREATE | File::TRUNCATE);
    dat.setPos(1024 * 1024);
    dat.setEOF();
    dat.setPos(0);
    int64_t start = sizeof(start);
    if(dat.getPos() != 0 || dat.getSize() != 1024 * 1024 || dat.write(&start, sizeof(start)) != sizeof(start))
        throw HashException(_("Hash data storage is unavailable"));
    dat.flush();
    dat.close();
}

void HashManager::Hasher::hashFile(const string& fileName, int64_t size) {
    Lock l(cs);
    if(stop) return;
    if(w.emplace(fileName, size).second) {
        if(paused > 0)
            paused = 1 ;
        else
            s.signal();
    }
}

bool HashManager::Hasher::pause() {
    Lock l(cs);
    if(stop) return false;
    paused = 1;
    //    printf("pause::paused: %d\n", paused);fflush(stdout);
    return true;
}

void HashManager::Hasher::resume() {
    Lock l(cs);
    while(paused > 0) {
        paused = 0;
        //        printf("resume::paused: %d\n", paused);fflush(stdout);
        s.signal();
    }
}

void HashManager::Hasher::shutdown() {
    Lock l(cs);
    stop = true;
    rebuild = false;
    w.clear();
    paused = 0;
    s.signal();
}

void HashManager::Hasher::scheduleRebuild() {
    Lock l(cs);
    if(stop) return;
    rebuild = true;
    s.signal();
}

bool HashManager::Hasher::isPaused() const {
    Lock l(cs);
    return paused > 0;
}

void HashManager::Hasher::stopHashing(const string& baseDir) {
    Lock l(cs);
    for (auto i = w.begin(); i != w.end();) {
        if (Util::strnicmp(baseDir, i->first, baseDir.length()) == 0) {
            w.erase(i++);
        } else {
            ++i;
        }
    }
}

void HashManager::Hasher::getStats(string& curFile, uint64_t& bytesLeft, size_t& filesLeft) const {
    Lock l(cs);
    curFile = currentFile;
    filesLeft = w.size();
    if (running)
        filesLeft++;
    bytesLeft = 0;
    for (auto& i: w) {
        bytesLeft += i.second;
    }
    bytesLeft += currentSize;
}

void HashManager::Hasher::instantPause() {
    for(;;) {
        {
            Lock l(cs);
            if(stop || paused == 0) return;
        }
        s.wait();
    }
}

#ifdef _WIN32
#define BUF_SIZE (256*1024)

bool HashManager::Hasher::fastHash(const string& fname, uint8_t* buf, TigerTree& tth, int64_t size, CRC32Filter* xcrc32) {
    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD x, y;
    if (!GetDiskFreeSpaceW(Text::utf8ToWide(Util::getFilePath(fname)).c_str(), &y, &x, &y, &y)) {
        return false;
    } else {
        if ((BUF_SIZE % x) != 0) {
            return false;
        } else {
            h = ::CreateFileW(Text::utf8ToWide(fname).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);
            if (h == INVALID_HANDLE_VALUE)
                return false;
        }
    }
    DWORD hn = 0;
    DWORD rn = 0;
    uint8_t* hbuf = buf + BUF_SIZE;
    uint8_t* rbuf = buf;

    OVERLAPPED over = { 0 };
    BOOL res = TRUE;
    over.hEvent = CreateEvent(NULL, FALSE, TRUE, NULL);

    bool ok = false;

    uint64_t lastRead = GET_TICK();
    if (!::ReadFile(h, hbuf, BUF_SIZE, &hn, &over)) {
        if (GetLastError() == ERROR_HANDLE_EOF) {
            hn = 0;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            if (!GetOverlappedResult(h, &over, &hn, TRUE)) {
                if (GetLastError() == ERROR_HANDLE_EOF) {
                    hn = 0;
                } else {
                    goto cleanup;
                }
            }
        } else {
            goto cleanup;
        }
    }

    over.Offset = hn;
    size -= hn;
    while (!stop) {
        if (size > 0) {
            // Start a new overlapped read
            ResetEvent(over.hEvent);
            if (CTX_SETTING(MAX_HASH_SPEED) > 0) {
                uint64_t now = GET_TICK();
                uint64_t minTime = hn * 1000LL / (CTX_SETTING(MAX_HASH_SPEED) * 1024LL * 1024LL);
                if (lastRead + minTime > now) {
                    uint64_t diff = now - lastRead;
                    Thread::sleep(minTime - diff);
                }
                lastRead = lastRead + minTime;
            } else {
                lastRead = GET_TICK();
            }
            res = ReadFile(h, rbuf, BUF_SIZE, &rn, &over);
        } else {
            rn = 0;
        }

        tth.update(hbuf, hn);
        if (xcrc32)
            (*xcrc32)(hbuf, hn);

        {
            Lock l(cs);
            currentSize = max(currentSize - hn, _LL(0));
        }

        if (size == 0) {
            ok = true;
            break;
        }

        if (!res) {
            // deal with the error code
            switch (GetLastError()) {
            case ERROR_IO_PENDING:
                if (!GetOverlappedResult(h, &over, &rn, TRUE)) {
                    dcdebug("Error 0x%x: %s\n", GetLastError(), Util::translateError(GetLastError()).c_str());
                    goto cleanup;
                }
                break;
            default:
                dcdebug("Error 0x%x: %s\n", GetLastError(), Util::translateError(GetLastError()).c_str());
                goto cleanup;
            }
        }

        instantPause();

        *((uint64_t*)&over.Offset) += rn;
        size -= rn;

        swap(rbuf, hbuf);
        swap(rn, hn);
    }

cleanup:
    ::CloseHandle(over.hEvent);
    ::CloseHandle(h);
    return ok;
}

#else // !_WIN32

static sigjmp_buf sb_env;

#ifndef __HAIKU__
static void sigbus_handler(int signum, siginfo_t* info, void* context)
#else // __HAIKU__
static void sigbus_handler(int signum)
#endif
{
    // Jump back to the fastHash which will return error. Apparently truncating
    // a file in Solaris sets si_code to BUS_OBJERR
#ifndef __HAIKU__
    (void)context;
    if (signum == SIGBUS && (info->si_code == BUS_ADRERR || info->si_code == BUS_OBJERR))
        siglongjmp(sb_env, 1);
#endif
}

bool HashManager::Hasher::fastHash(const string& filename, uint8_t* , TigerTree& tth, int64_t size, CRC32Filter* xcrc32) {
    instantPause();
    if(stop) return false;

    static StreamStore streamStore;

    if (streamStore.loadTree(filename, tth, -1)){
        printf ("%s: hash [%s] was loaded from Xattr.\n", filename.c_str(), tth.getRoot().toBase32().c_str());
        return true;
    }

    static const int64_t BUF_BYTES = (CTX_SETTING(HASH_BUFFER_SIZE_MB) >= 1)? CTX_SETTING(HASH_BUFFER_SIZE_MB)*1024*1024 : 0x800000;
    static const int64_t BUF_SIZE = BUF_BYTES - (BUF_BYTES % getpagesize());

    int fd = open(Text::fromUtf8(filename).c_str(), O_RDONLY);
    if(fd == -1) {
        dcdebug("Error opening file %s: %s\n", filename.c_str(), Util::translateError(errno).c_str());
        return false;
    }

    const int maxHashSpeed = CTX_SETTING(MAX_HASH_SPEED);
    int64_t pos = 0;
    int64_t size_read = 0;
    void *buf = NULL;
    bool ok = false;

    // Prepare and setup a signal handler in case of SIGBUS during mmapped file reads.
    // SIGBUS can be sent when the file is truncated or in case of read errors.
    struct sigaction act, oldact;
    sigset_t signalset;

    sigemptyset(&signalset);

    act.sa_handler = NULL;
#ifndef __HAIKU__
    act.sa_sigaction = sigbus_handler;
#endif
    act.sa_mask = signalset;
#ifdef SA_SIGINFO
    act.sa_flags = SA_SIGINFO | SA_RESETHAND;
#else
    act.sa_flags = NULL;
#endif
    if (sigaction(SIGBUS, &act, &oldact) == -1) {
        dcdebug("Failed to set signal handler for fastHash\n");
        close(fd);
        return false;   // Better luck with the slow hash.
    }

    uint64_t lastRead = GET_TICK();
    unsigned long mmap_flags = static_cast<bool>(CTX_SETTING(HASH_BUFFER_PRIVATE))? MAP_PRIVATE : MAP_SHARED;
#ifdef MAP_POPULATE
    if (static_cast<bool>(CTX_SETTING(HASH_BUFFER_POPULATE)))
        mmap_flags |= MAP_POPULATE;
#endif
#ifdef MAP_NORESERVE
    if (static_cast<bool>(CTX_SETTING(HASH_BUFFER_NORESERVE)))
        mmap_flags |= MAP_NORESERVE;
#endif
    while (pos < size && !stop) {
        size_read = std::min(size - pos, BUF_SIZE);
        buf = mmap(0, size_read, PROT_READ, mmap_flags, fd, pos);
        if(buf == MAP_FAILED) {
            dcdebug("Error calling mmap for file %s: %s\n", filename.c_str(), Util::translateError(errno).c_str());
            break;
        }

        if (sigsetjmp(sb_env, 1)) {
            dcdebug("Caught SIGBUS for file %s\n", filename.c_str());
            break;
        }

        if (posix_madvise(buf, size_read, POSIX_MADV_SEQUENTIAL | POSIX_MADV_WILLNEED) == -1) {
            dcdebug("Error calling madvise for file %s: %s\n", filename.c_str(), Util::translateError(errno).c_str());
            break;
        }

        if(maxHashSpeed > 0) {
            uint64_t now = GET_TICK();
            uint64_t minTime = size_read * 1000LL / (maxHashSpeed * 1024LL * 1024LL);
            
            if(lastRead + minTime> now) {
                uint64_t diff = now - lastRead;
                Thread::sleep(minTime - diff);
            }
            
            lastRead = lastRead + minTime;
        } else {
            lastRead = GET_TICK();
        }

        tth.update(buf, size_read);
        if(xcrc32)
            (*xcrc32)(buf, size_read);

        {
            Lock l(cs);
            currentSize = max(static_cast<uint64_t>(currentSize - size_read), static_cast<uint64_t>(0));
        }

        if (munmap(buf, size_read) == -1) {
            dcdebug("Error calling munmap for file %s: %s\n", filename.c_str(), Util::translateError(errno).c_str());
            break;
        }

        buf = NULL;
        pos += size_read;

        instantPause();

        if (pos == size) {
            ok = true;
        }
    }

    if (buf != NULL && buf != MAP_FAILED && munmap(buf, size_read) == -1) {
        dcdebug("Error calling munmap for file %s: %s\n", filename.c_str(), Util::translateError(errno).c_str());
    }

    close(fd);

    if (sigaction(SIGBUS, &oldact, NULL) == -1) {
        dcdebug("Failed to reset old signal handler for SIGBUS\n");
    }

    // hashDone persists only the finalized, admitted tree on every platform.
    return ok && !stop;
}

#endif // !_WIN32
int HashManager::Hasher::run() {
    setThreadPriority(Thread::IDLE);
    uint8_t* buf = NULL;
    bool virtualBuf = true;
    string fname;
    bool last = false;
    ScopedFunctor([&] {
        if(buf) {
#ifdef _WIN32
            if(virtualBuf) VirtualFree(buf, 0, MEM_RELEASE);
            else
#endif
                delete [] buf;
        }
        Lock l(cs);
        currentFile.clear();
        currentSize = 0;
        running = false;
    });
    pause();
    for(;;) {
        bool wait, rebuilding;
        {
            Lock l(cs);
            if(stop) break;
            wait = w.empty() && !rebuild;
            rebuilding = rebuild;
            rebuild = false;
        }
        if(wait) { s.wait(); continue; }
        if(rebuilding) {
            ctx().getHashManager()->doRebuild();
            if(!stop) ctx().getLogManager()->message(_("Hash database rebuilt"));
            continue;
        }
        {
            Lock l(cs);
            if(stop) break;
            if(!w.empty()) {
                currentFile = fname = w.begin()->first;
                currentSize = w.begin()->second;
                w.erase(w.begin());
                last = w.empty();
            } else {
                last = true;
                fname.clear();
            }
            running = !fname.empty();
        }
        instantPause();
        if(stop) break;

        if(!fname.empty()) {
            int64_t size = File::getSize(fname);
#ifdef _WIN32
            if(buf == NULL) {
                virtualBuf = true;
                buf = (uint8_t*)VirtualAlloc(NULL, 2*BUF_SIZE, MEM_COMMIT, PAGE_READWRITE);
            }
#else
            static const int64_t BUF_BYTES = (CTX_SETTING(HASH_BUFFER_SIZE_MB) >= 1)? CTX_SETTING(HASH_BUFFER_SIZE_MB)*1024*1024 : 0x800000;
            static const int64_t BUF_SIZE = BUF_BYTES - (BUF_BYTES % getpagesize());
#endif

            if(buf == NULL) {
                virtualBuf = false;
                buf = new uint8_t[BUF_SIZE];
            }
            try {
                File f(fname, File::READ, File::OPEN);
                size = f.getSize();
                int64_t bs = max(TigerTree::calcBlockSize(f.getSize(), 10), MIN_BLOCK_SIZE);
                uint64_t start = GET_TICK();
                uint32_t timestamp = f.getLastModified();
                TigerTree slowTTH(bs);
                TigerTree* tth = &slowTTH;

                CRC32Filter crc32;
                SFVReader sfv(fname);
                CRC32Filter* xcrc32 = 0;
                if(sfv.hasCRC())
                    xcrc32 = &crc32;

                TigerTree fastTTH(bs);
                tth = &fastTTH;

#ifdef _WIN32
                if(!virtualBuf || !CTX_BOOLSETTING(FAST_HASH) || !fastHash(fname, buf, fastTTH, size, xcrc32)) {
#else
                if(!CTX_BOOLSETTING(FAST_HASH) || !fastHash(fname, 0, fastTTH, size, xcrc32)) {
#endif
                    if(stop) break;
                    tth = &slowTTH;
                    crc32 = CRC32Filter();
                    uint64_t lastRead = GET_TICK();
                    size_t n = 0;
                    do {
                        size_t bufSize = BUF_SIZE;
                        if(CTX_SETTING(MAX_HASH_SPEED)> 0) {
                            uint64_t now = GET_TICK();
                            uint64_t minTime = n * 1000LL / (CTX_SETTING(MAX_HASH_SPEED) * 1024LL * 1024LL);
                            if(lastRead + minTime> now) {
                                Thread::sleep(minTime - (now - lastRead));
                            }
                            lastRead = lastRead + minTime;
                        } else {
                            lastRead = GET_TICK();
                        }
                        if(stop) break;
                        n = f.read(buf, bufSize);
                        tth->update(buf, n);
                        if(xcrc32)
                            (*xcrc32)(buf, n);

                        {
                            Lock l(cs);
                            currentSize = max(static_cast<uint64_t>(currentSize - n), static_cast<uint64_t>(0));
                        }

                        instantPause();
                    } while (n> 0 && !stop);
                }

                f.close();
                // A stop or early EOF must never promote an incomplete tree.
                if(stop) break;
                if(tth->getFileSize() != size)
                    throw FileException(_("File size changed while hashing"));
                tth->finalize();
                uint64_t end = GET_TICK();
                int64_t speed = 0;
                if(end> start) {
                    speed = size * _LL(1000) / (end - start);
                }
                if(xcrc32 && xcrc32->getValue() != sfv.getCRC()) {
                    ctx().getLogManager()->message(str(F_("%1% not shared; calculated CRC32 does not match the one found in SFV file.") % Util::addBrackets(fname)));
                } else {
                    ctx().getHashManager()->hashDone(fname, timestamp, *tth, speed, size);
                }
            } catch(const FileException& e) {
                ctx().getLogManager()->message(str(F_("Error hashing %1%: %2%") % Util::addBrackets(fname) % e.getError()));
            }
        }
        {
            Lock l(cs);
            currentFile.clear();
            currentSize = 0;
            running = false;
        }
        if(buf != NULL && (last || stop)) {
            if(virtualBuf) {
#ifdef _WIN32
                VirtualFree(buf, 0, MEM_RELEASE);
#endif
            } else {
                delete [] buf;
            }
            buf = NULL;
        }
    }
    return 0;
}

HashManager::HashPauser::HashPauser(DCContext& ctx) : ctx_(ctx) {
    resume = !ctx_.getHashManager()->isHashingPaused();
}

HashManager::HashPauser::~HashPauser() {
    if(resume)
        ctx().getHashManager()->resumeHashing();
}

bool HashManager::pauseHashing() {
    Lock l(cs);
    return hasher.pause();
}

void HashManager::resumeHashing() {
    Lock l(cs);
    hasher.resume();
}

bool HashManager::isHashingPaused() const {
    Lock l(cs);
    return hasher.isPaused();
}

void HashManager::on(TimerManagerListener::Second, uint64_t tick) {
    if(!beginOperation()) return;
    ScopedFunctor([this] { finishOperation(); });
    (void)tick;
    //fprintf(stdout,"%lld\n", tick); fflush(stdout);
    static bool firstcycle = true;
    if (firstcycle){
        int delay = CTX_SETTING(HASHING_START_DELAY);
        SettingsManager *SM = ctx().getSettingsManager();
        if (delay > 1800){
            delay = 1800;
            SM->set(SettingsManager::HASHING_START_DELAY, delay);
        }

        if (!ctx().getShareManager()->isRefreshing()){
            string  curFile;
            uint64_t bytesLeft;
            size_t  filesLeft = -1;
            getStats(curFile, bytesLeft, filesLeft);
            //fprintf(stdout,"filesLeft %d\n", filesLeft); fflush(stdout);

            // if delay is more than -1 hashing process must be resumed
            // if there is nothing to hashing pause is not required
            if (isHashingPaused() && ((delay >= 0 && Util::getUpTime() >= delay) || filesLeft == 0)){
                resumeHashing();
                firstcycle = false;
            }
        }
    }
}

} // namespace dcpp
