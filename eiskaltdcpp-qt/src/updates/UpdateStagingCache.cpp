#include "UpdateStagingCache.h"
#include "UpdateArchiveVerifier.h"
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace updates {
namespace {
bool bound(const QByteArray& bytes, const QByteArray& key, const SignedUpdate& selected,
    const std::function<bool()>& cancelled) {
    const auto feed = verifyUpdateFeed(bytes, key, cancelled);
    if(!feed) return false;
    for(const auto& item : feed->releases) {
        const auto& a = item.offer; const auto& b = selected.offer;
        if(a.build == b.build && a.version == b.version && a.notes == b.notes &&
            a.bundleId == b.bundleId && a.cpu == b.cpu && a.minimumOs == b.minimumOs &&
            a.channel == b.channel && a.size == b.size && a.sha256 == b.sha256 &&
            item.archiveUrl == selected.archiveUrl && item.archiveSignatureBase64 == selected.archiveSignatureBase64)
            return true;
    }
    return false;
}
#ifndef _WIN32
struct Descriptor {
    int value = -1;
    explicit Descriptor(int fd = -1) : value(fd) {}
    Descriptor(const Descriptor&) = delete;
    ~Descriptor() { if(value >= 0) ::close(value); }
};
bool privateNode(const struct stat& node, bool directory) {
    return (directory ? S_ISDIR(node.st_mode) : S_ISREG(node.st_mode)) &&
        node.st_uid == getuid() && !(node.st_mode & 0077) && (directory || node.st_nlink == 1);
}
bool sameNode(const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode &&
        a.st_uid == b.st_uid;
}
bool sameFile(const struct stat& a, const struct stat& b) {
#ifdef __APPLE__
    const bool times = a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
        a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
#else
    const bool times = a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
        a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
#endif
    return sameNode(a, b) && a.st_size == b.st_size && times && b.st_nlink == 1;
}
struct Directory {
    QString path;
    Descriptor fd;
    struct stat before{};
    bool valid = false;
    explicit Directory(const QString& name, bool lock = true) : path(name), fd(
        QFileInfo(name).isAbsolute() && !name.contains(QChar('\0')) ?
        ::open(QFile::encodeName(name).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1) {
        valid = fd.value >= 0 && ::fstat(fd.value, &before) == 0 && privateNode(before, true) &&
            (!lock || ::flock(fd.value, LOCK_EX | LOCK_NB) == 0) && stillNamed();
    }
    bool stillNamed() const {
        struct stat current{}, named{};
        return fd.value >= 0 && ::fstat(fd.value, &current) == 0 && privateNode(current, true) &&
            sameNode(before, current) && ::lstat(QFile::encodeName(path).constData(), &named) == 0 && sameNode(before, named);
    }
    bool privateEntry(const QByteArray& name, bool missingAllowed = false) const {
        struct stat node{};
        if(::fstatat(fd.value, name.constData(), &node, AT_SYMLINK_NOFOLLOW) != 0)
            return missingAllowed && errno == ENOENT;
        return privateNode(node, false);
    }
    bool removeTemporary(const char* name) const {
        struct stat node{};
        if(::fstatat(fd.value, name, &node, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT;
        return privateNode(node, false) && stillNamed() && ::unlinkat(fd.value, name, 0) == 0;
    }
};
struct Temporary {
    Directory& directory;
    QByteArray name;
    Descriptor fd;
    explicit Temporary(Directory& parent, const char* slot = ".stage-archive") : directory(parent), name(slot), fd(::openat(parent.fd.value, name.constData(),
        O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)) {}
    ~Temporary() { if(fd.value >= 0 && !name.isEmpty()) ::unlinkat(directory.fd.value, name.constData(), 0); }
    QString path() const { return directory.path + "/" + QString::fromLatin1(name); }
    bool replace(const char* target) {
        if(!directory.stillNamed() || !directory.privateEntry(target, true) ||
            ::renameat(directory.fd.value, name.constData(), directory.fd.value, target) != 0) return false;
        name.clear(); return true;
    }
};
bool stopped(const std::function<bool()>& callback) { return callback && callback(); }
bool writeAll(int fd, const char* bytes, size_t size) {
    while(size) {
        const auto written = ::write(fd, bytes, size);
        if(written < 0 && errno == EINTR) continue;
        if(written <= 0) return false;
        bytes += written; size -= static_cast<size_t>(written);
    }
    return true;
}
bool copyPrivate(int source, Temporary& target, qint64 size, const std::function<bool()>& cancelled) {
    struct stat before{}, after{};
    if(target.fd.value < 0 || ::fstat(source, &before) != 0 || !privateNode(before, false) || before.st_size != size) return false;
    QByteArray buffer(64 * 1024, '\0');
    for(qint64 offset = 0; offset < size;) {
        if(stopped(cancelled)) return false;
        const auto count = ::read(source, buffer.data(), static_cast<size_t>(std::min<qint64>(buffer.size(), size - offset)));
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0 || !writeAll(target.fd.value, buffer.constData(), static_cast<size_t>(count))) return false;
        offset += count;
    }
    return !stopped(cancelled) && ::fstat(source, &after) == 0 && sameFile(before, after) && ::fsync(target.fd.value) == 0;
}
QByteArray readFeed(Directory& directory, const std::function<bool()>& cancelled) {
    Descriptor input(::openat(directory.fd.value, "appcast.xml", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat before{}, after{};
    if(input.value < 0 || ::fstat(input.value, &before) != 0 || !privateNode(before, false) ||
        before.st_size <= 0 || before.st_size > 1024 * 1024) return {};
    QByteArray bytes(static_cast<int>(before.st_size), '\0');
    for(qint64 offset = 0; offset < before.st_size;) {
        if(stopped(cancelled)) return {};
        const auto count = ::read(input.value, bytes.data() + offset, static_cast<size_t>(before.st_size - offset));
        if(count < 0 && errno == EINTR) continue;
        if(count <= 0) return {};
        offset += count;
    }
    return ::fstat(input.value, &after) == 0 && sameFile(before, after) && directory.stillNamed() ? bytes : QByteArray{};
}
ArchiveExpectation expectation(const SignedUpdate& selected) {
    return {selected.offer.size, selected.offer.sha256, selected.archiveSignatureBase64};
}
#endif
}

bool saveVerifiedStage(const QString& cacheDirectory, const QByteArray& feed, const SignedUpdate& selected,
    const QByteArray& key, const QString& archive, const std::function<bool()>& cancelled) {
#ifdef _WIN32
    return false; // Private staging ACL support is not yet accepted.
#else
    try {
        if(stopped(cancelled) || !bound(feed, key, selected, cancelled) ||
            !verifyUpdateArchive(archive, expectation(selected), key, cancelled)) return false;
        Directory cache(cacheDirectory);
        const QFileInfo info(archive);
        Directory source(info.absolutePath(), false);
        if(!cache.valid || !source.valid || !cache.privateEntry("appcast.xml", true) || !cache.privateEntry("update.zip", true) ||
            !cache.removeTemporary(".stage-archive") || !cache.removeTemporary(".stage-feed")) return false;
        Descriptor input(::openat(source.fd.value, QFile::encodeName(info.fileName()).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        Temporary copied(cache), metadata(cache, ".stage-feed");
        if(input.value < 0 || metadata.fd.value < 0 || !copyPrivate(input.value, copied, selected.offer.size, cancelled) ||
            !source.stillNamed() || !cache.stillNamed() || !verifyUpdateArchive(copied.path(), expectation(selected), key, cancelled) ||
            !writeAll(metadata.fd.value, feed.constData(), static_cast<size_t>(feed.size())) || ::fsync(metadata.fd.value) != 0 ||
            stopped(cancelled) || !copied.replace("update.zip")) return false;
        // A crash between these atomic replacements can leave a mixed pair.
        // Restore authenticates the feed and archive together and refuses it.
        return !stopped(cancelled) && metadata.replace("appcast.xml") && ::fsync(cache.fd.value) == 0 && cache.stillNamed();
    } catch(...) { return false; }
#endif
}

bool restoreVerifiedStage(const QString& cacheDirectory, const QByteArray& currentFeed, const SignedUpdate& selected,
    const QByteArray& key, const QString& destination, const std::function<bool()>& cancelled) {
#ifdef _WIN32
    return false;
#else
    try {
        if(stopped(cancelled) || destination.contains(QChar('\0')) || !QFileInfo(destination).isAbsolute() ||
            !bound(currentFeed, key, selected, cancelled)) return false;
        Directory cache(cacheDirectory);
        if(!cache.valid || !bound(readFeed(cache, cancelled), key, selected, cancelled) ||
            !verifyUpdateArchive(cache.path + "/update.zip", expectation(selected), key, cancelled)) return false;
        const QFileInfo info(destination);
        Directory output(info.absolutePath());
        const auto name = QFile::encodeName(info.fileName());
        struct stat existing{};
        if(!output.valid || name.isEmpty() || ::fstatat(output.fd.value, name.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) return false;
        Descriptor input(::openat(cache.fd.value, "update.zip", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        Temporary copied(output);
        if(input.value < 0 || !copyPrivate(input.value, copied, selected.offer.size, cancelled) || !cache.stillNamed() ||
            !output.stillNamed() || !verifyUpdateArchive(copied.path(), expectation(selected), key, cancelled) || stopped(cancelled)) return false;
        // Atomic no-overwrite publication, anchored to the opened private root.
        if(::linkat(output.fd.value, copied.name.constData(), output.fd.value, name.constData(), 0) != 0) return false;
        const bool unlinked = ::unlinkat(output.fd.value, copied.name.constData(), 0) == 0;
        if(unlinked) copied.name.clear();
        if(unlinked && ::fsync(output.fd.value) == 0 && output.stillNamed()) return true;
        ::unlinkat(output.fd.value, name.constData(), 0);
        return false;
    } catch(...) { return false; }
#endif
}
}
