#include "BundleArchiveVerifier.h"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace updates {
namespace {
using Bytes = std::vector<unsigned char>;
uint16_t u16(const unsigned char* p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t u32(const unsigned char* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
bool stopped(const std::function<bool()>& cancel) { return cancel && cancel(); }

struct File {
    int fd;
    ~File() { if(fd >= 0) close(fd); }
};
bool readAt(int fd, uint64_t offset, void* target, size_t length) {
    auto* out = static_cast<unsigned char*>(target);
    while(length) {
        const auto n = pread(fd, out, length, static_cast<off_t>(offset));
        if(n < 0 && errno == EINTR) continue;
        if(n <= 0) return false;
        out += n; length -= n; offset += n;
    }
    return true;
}

bool safeName(const std::string& name) {
    if(name.empty() || name.size() > 1024 || name.front() == '/') return false;
    for(unsigned char c : name) if(c < 32 || c == 127 || c == '\\') return false;
    NSString* decoded = [[NSString alloc] initWithBytes:name.data() length:name.size() encoding:NSUTF8StringEncoding];
    if(!decoded) return false;
    for(size_t p = 0; p < name.size();) {
        const auto end = name.find('/', p);
        const auto component = name.substr(p, end == std::string::npos ? end : end - p);
        if(component.empty() || component == "." || component == "..") return false;
        if(end == std::string::npos) break;
        p = end + 1;
    }
    return true;
}

struct Entry {
    uint32_t compressed, size, crc, attributes;
    uint16_t method, flags;
    uint64_t local, data;
    bool regular() const { return ((attributes >> 16) & S_IFMT) == S_IFREG; }
};

// Only the release format we produce: a single-disk, non-ZIP64, stored/deflated
// ZIP with one app root. This is an identity preflight, not an extraction API.
bool directory(int fd, uint64_t length, std::map<std::string, Entry>& entries,
               std::string& root, const std::function<bool()>& cancel) {
    if(length < 22) return false;
    Bytes tail(static_cast<size_t>(std::min<uint64_t>(length, 65557)));
    if(!readAt(fd, length - tail.size(), tail.data(), tail.size())) return false;
    size_t end = tail.size() - 22;
    for(;;) {
        if(u32(&tail[end]) == 0x06054b50 && end + 22 + u16(&tail[end + 20]) == tail.size()) break;
        if(!end) return false;
        --end;
    }
    const auto* e = &tail[end];
    const uint32_t count = u16(e + 10), size = u32(e + 12), start = u32(e + 16);
    if(u16(e + 4) || u16(e + 6) || count != u16(e + 8) || !count || count > 32768 ||
       size > 16 * 1024 * 1024 || uint64_t(start) + size != length - tail.size() + end) return false;
    Bytes central(size);
    if(!readAt(fd, start, central.data(), central.size())) return false;
    size_t p = 0;
    std::vector<std::pair<uint64_t, uint64_t>> regions;
    std::set<std::string> namesOnDisk;
    for(uint32_t i = 0; i < count; ++i) {
        if(stopped(cancel) || p + 46 > central.size()) return false;
        const auto* c = &central[p];
        if(u32(c) != 0x02014b50 || u16(c + 34) || u16(c + 6) > 20) return false;
        const size_t names = u16(c + 28), extra = u16(c + 30), comments = u16(c + 32);
        if(p + 46 + names + extra + comments > central.size()) return false;
        std::string name(reinterpret_cast<const char*>(c + 46), names);
        if(!safeName(name)) return false;
        NSString* decoded = [[NSString alloc] initWithBytes:name.data() length:name.size() encoding:NSUTF8StringEncoding];
        if([decoded hasSuffix:@"/"]) decoded = [decoded substringToIndex:decoded.length - 1];
        const std::string diskName = decoded.precomposedStringWithCanonicalMapping.lowercaseString.UTF8String;
        if(!namesOnDisk.insert(diskName).second) return false;
        const auto slash = name.find('/');
        if(slash == std::string::npos) return false;
        const auto top = name.substr(0, slash + 1);
        if(root.empty()) root = top;
        if(top != root || root.size() < 6 || root.substr(root.size() - 5) != ".app/") return false;
        Entry item{u32(c + 20), u32(c + 24), u32(c + 16), u32(c + 38), u16(c + 10), u16(c + 8), u32(c + 42), 0};
        if((item.method != 0 && item.method != 8) || (item.flags & ~uint16_t(0x080e)) ||
           item.compressed == UINT32_MAX || item.size == UINT32_MAX || item.local + 30 > start) return false;
        std::array<unsigned char, 30> local{};
        if(!readAt(fd, item.local, local.data(), local.size()) || u32(local.data()) != 0x04034b50 ||
           u16(local.data() + 6) != item.flags || u16(local.data() + 8) != item.method ||
           u16(local.data() + 26) != names) return false;
        if(!(item.flags & 8) && (u32(local.data() + 14) != item.crc ||
            u32(local.data() + 18) != item.compressed || u32(local.data() + 22) != item.size)) return false;
        item.data = item.local + 30 + names + u16(local.data() + 28);
        if(item.data + item.compressed > start) return false;
        Bytes localName(names);
        if(!readAt(fd, item.local + 30, localName.data(), names) ||
           memcmp(localName.data(), name.data(), names)) return false;
        if(item.method == 0 && item.compressed != item.size) return false;
        if(!entries.emplace(name, item).second) return false;
        regions.emplace_back(item.local, item.data + item.compressed);
        p += 46 + names + extra + comments;
    }
    if(p != central.size()) return false;
    std::sort(regions.begin(), regions.end());
    for(size_t i = 1; i < regions.size(); ++i) if(regions[i - 1].second > regions[i].first) return false;
    return true;
}

bool contents(int fd, const Entry& entry, Bytes& out, size_t limit, bool prefix,
              const std::function<bool()>& cancel) {
    if(!entry.regular() || !entry.size || entry.size > 256 * 1024 * 1024 ||
       (!prefix && (entry.size > limit || entry.compressed > limit + 65536))) return false;
    out.resize(prefix ? std::min<size_t>(limit, entry.size) : entry.size);
    if(stopped(cancel)) return false;
    if(entry.method == 0) {
        if(!readAt(fd, entry.data, out.data(), out.size())) return false;
    } else {
        z_stream z{};
        if(inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
        struct End { z_stream* z; ~End() { inflateEnd(z); } } cleanup{&z};
        std::array<unsigned char, 32768> input{};
        // One spare byte distinguishes exact-size completion from an oversized stream.
        out.resize(out.size() + 1);
        z.next_out = out.data(); z.avail_out = static_cast<uInt>(out.size());
        uint64_t consumed = 0;
        int result = Z_OK;
        while(result == Z_OK) {
            if(stopped(cancel)) return false;
            if(!z.avail_in && consumed < entry.compressed) {
                const auto n = static_cast<size_t>(std::min<uint64_t>(input.size(), entry.compressed - consumed));
                if(!readAt(fd, entry.data + consumed, input.data(), n)) return false;
                z.next_in = input.data(); z.avail_in = static_cast<uInt>(n); consumed += n;
            }
            result = inflate(&z, Z_NO_FLUSH);
            if(prefix && z.total_out >= limit) { out.resize(limit); return true; }
        }
        if(result != Z_STREAM_END || z.total_in != entry.compressed || z.total_out != entry.size) return false;
        out.resize(entry.size);
    }
    return !stopped(cancel) && (prefix || crc32(0, out.data(), static_cast<uInt>(out.size())) == entry.crc);
}

bool sameFile(const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
        a.st_mode == b.st_mode && a.st_uid == b.st_uid && a.st_nlink == b.st_nlink &&
        a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
        a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
}
}

bool bundleArchiveMatches(const std::string& path, const BundleArchiveIdentity& expected,
                          const std::function<bool()>& cancel) {
    @autoreleasepool {
        if(stopped(cancel) || expected.bundleId.empty() || expected.build.empty() || expected.version.empty() ||
           (expected.cpu != "arm64" && expected.cpu != "x86_64")) return false;
        File file{open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
        struct stat before{}, after{}, named{};
        if(file.fd < 0 || fstat(file.fd, &before) || !S_ISREG(before.st_mode) || before.st_uid != getuid() ||
           before.st_nlink != 1 || before.st_size <= 0 || before.st_size > 512 * 1024 * 1024) return false;
        std::map<std::string, Entry> entries;
        std::string root;
        if(!directory(file.fd, before.st_size, entries, root, cancel)) return false;
        for(const auto& directoryName : {root, root + "Contents/", root + "Contents/MacOS/"}) {
            const auto directoryEntry = entries.find(directoryName);
            if(directoryEntry == entries.end() ||
               ((directoryEntry->second.attributes >> 16) & S_IFMT) != S_IFDIR) return false;
        }
        auto plist = entries.find(root + "Contents/Info.plist");
        Bytes bytes;
        if(plist == entries.end() || !contents(file.fd, plist->second, bytes, 1024 * 1024, false, cancel)) return false;
        NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
        id object = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil];
        if(![object isKindOfClass:NSDictionary.class]) return false;
        NSDictionary* info = object;
        const auto matches = [&](NSString* key, const std::string& value) {
            id actual = info[key];
            NSString* wanted = [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
            return [actual isKindOfClass:NSString.class] && [actual isEqualToString:wanted];
        };
        if(!matches(@"CFBundleIdentifier", expected.bundleId) || !matches(@"CFBundleVersion", expected.build) ||
           !matches(@"CFBundleShortVersionString", expected.version) || !matches(@"CFBundlePackageType", "APPL")) return false;
        id executable = info[@"CFBundleExecutable"];
        if(![executable isKindOfClass:NSString.class]) return false;
        NSData* encodedName = [executable dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
        if(!encodedName || !encodedName.length) return false;
        const std::string name(static_cast<const char*>(encodedName.bytes), encodedName.length);
        if(!safeName(name) || name.find('/') != std::string::npos) return false;
        auto binary = entries.find(root + "Contents/MacOS/" + name);
        if(binary == entries.end() || !contents(file.fd, binary->second, bytes, 32, true, cancel) || bytes.size() < 32) return false;
        const uint32_t cpu = expected.cpu == "arm64" ? 0x0100000c : 0x01000007;
        if(u32(bytes.data()) != 0xfeedfacf || u32(bytes.data() + 4) != cpu || u32(bytes.data() + 12) != 2) return false;
        return !stopped(cancel) && !fstat(file.fd, &after) && !lstat(path.c_str(), &named) &&
            sameFile(before, after) && sameFile(before, named);
    }
}
}
