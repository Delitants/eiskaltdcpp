#include "TorrentCreator.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/load_torrent.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#ifdef Q_OS_UNIX
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace eiskalt::torrent {
namespace {
namespace lt = libtorrent;
constexpr int blockSize = 16 * 1024;
constexpr int pieceSize = 1024 * 1024;
constexpr qint64 maxSourceBytes = qint64(1) << 40;
constexpr std::size_t maxEntries = 10000;
constexpr std::size_t maxPathBytes = 16 * 1024 * 1024;
constexpr int maxDepth = 128;
// Match TorrentEngine's load and layout limits, including hybrid pad files.
constexpr int maxLayoutFiles = 10000;
constexpr int maxPieces = 524288;
constexpr std::size_t maxMetadataBytes = 16 * 1024 * 1024;

struct Failure { QString message; };
struct Cancelled {};
[[noreturn]] void fail(const QString &message) { throw Failure{message}; }
void checkpoint(const std::atomic<bool> &cancelled)
{
    if (cancelled.load()) throw Cancelled{};
}

bool safeName(const QString &name)
{
    if (name.isEmpty() || name == "." || name == ".." || name == ".pad" ||
        name.endsWith('.') || name.endsWith(' ')) return false;
    for (const QChar c : name) {
        if (c.unicode() < 32 || c.unicode() == 127 || QStringLiteral("/\\:<>\"|?*").contains(c))
            return false;
    }
    const auto stem = name.section('.', 0, 0).toUpper();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    if (stem.size() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) &&
        stem[3] >= '1' && stem[3] <= '9') return false;
    return true;
}

QString checkedPath(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path) || path.toUtf8().size() > 4096)
        fail(QStringLiteral("Select an absolute local path of at most 4096 bytes."));
    const auto parts = path.split('/');
    for (const auto &part : parts) {
        if (!part.isEmpty() && !safeName(part))
            fail(QStringLiteral("The path contains an unsafe name or traversal component: %1").arg(path));
    }
    const auto result = QDir::cleanPath(path);
    if (QFileInfo(result).fileName().isEmpty())
        fail(QStringLiteral("A filesystem root cannot be used as a source or output."));
    return result;
}

QStringList checkedTrackers(const QStringList &trackers)
{
    if (trackers.size() > 128) fail(QStringLiteral("At most 128 trackers are allowed."));
    QStringList result;
    for (const auto &value : trackers) {
        const auto text = value.trimmed();
        if (text.isEmpty()) continue;
        const QUrl url(text, QUrl::StrictMode);
        const auto scheme = url.scheme().toLower();
        if (text.size() > 4096 || !url.isValid() || url.host().isEmpty() || url.hasFragment() ||
            (scheme != "http" && scheme != "https" && scheme != "udp") || url.port() == 0 ||
            std::any_of(text.begin(), text.end(), [](QChar c) { return c.isSpace() || c.unicode() < 32; }))
            fail(QStringLiteral("Trackers must be valid HTTP, HTTPS or UDP URLs with a host."));
        const auto encoded = QString::fromLatin1(url.toEncoded());
        if (!result.contains(encoded)) result.append(encoded);
    }
    return result;
}

#ifdef Q_OS_UNIX
struct Handle {
    int fd = -1;
    explicit Handle(int value = -1) : fd(value) {}
    ~Handle() { if (fd >= 0) ::close(fd); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : fd(std::exchange(other.fd, -1)) {}
    Handle &operator=(Handle &&other) noexcept {
        std::swap(fd, other.fd);
        return *this;
    }
};

[[noreturn]] void fileError(const QString &operation)
{
    fail(operation + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno)));
}

struct Stamp {
    struct stat value {};
    bool sameIdentity(const Stamp &other) const {
        return value.st_dev == other.value.st_dev && value.st_ino == other.value.st_ino &&
            value.st_mode == other.value.st_mode;
    }
    bool operator==(const Stamp &other) const {
#ifdef Q_OS_MACOS
        const auto mtime = value.st_mtimespec, otherMtime = other.value.st_mtimespec;
        const auto ctime = value.st_ctimespec, otherCtime = other.value.st_ctimespec;
#else
        const auto mtime = value.st_mtim, otherMtime = other.value.st_mtim;
        const auto ctime = value.st_ctim, otherCtime = other.value.st_ctim;
#endif
        return sameIdentity(other) && value.st_size == other.value.st_size &&
            value.st_nlink == other.value.st_nlink &&
            mtime.tv_sec == otherMtime.tv_sec && mtime.tv_nsec == otherMtime.tv_nsec &&
            ctime.tv_sec == otherCtime.tv_sec && ctime.tv_nsec == otherCtime.tv_nsec;
    }
};

Stamp stamp(int fd)
{
    Stamp result;
    if (::fstat(fd, &result.value) != 0) fileError(QStringLiteral("Cannot inspect source handle"));
    return result;
}

// Walk every ancestor through an anchored descriptor. O_NONBLOCK also prevents
// a source swapped for a FIFO from trapping the worker in open().
Handle openRelative(int parent, const std::string &path, bool directory = false)
{
    Handle current(::dup(parent));
    if (current.fd < 0) fileError(QStringLiteral("Cannot duplicate directory handle"));
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(begin, end == std::string::npos ? end : end - begin);
        const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
            ((directory || end != std::string::npos) ? O_DIRECTORY : 0);
        Handle next(::openat(current.fd, part.c_str(), flags));
        if (next.fd < 0) fileError(QStringLiteral("Cannot open path without following symlinks: %1").arg(QString::fromUtf8(path)));
        current = std::move(next);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return current;
}

Handle openDirectory(const QString &path)
{
    Handle root(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (root.fd < 0) fileError(QStringLiteral("Cannot open filesystem root"));
    return openRelative(root.fd, QFile::encodeName(path.mid(1)).toStdString(), true);
}

void requireAbsent(int parent, const std::string &name)
{
    struct stat value {};
    if (::fstatat(parent, name.c_str(), &value, AT_SYMLINK_NOFOLLOW) == 0)
        fail(QStringLiteral("The output already exists. Choose a new .torrent filename; files are never overwritten."));
    if (errno != ENOENT) fileError(QStringLiteral("Cannot inspect output path"));
}

void requireOutsideSource(int outputParent, const Stamp &source)
{
    if (!S_ISDIR(source.value.st_mode)) return;
    Handle current(::dup(outputParent));
    if (current.fd < 0) fileError(QStringLiteral("Cannot inspect output directory"));
    for (int depth = 0; depth < 1024; ++depth) {
        const auto here = stamp(current.fd);
        if (source.sameIdentity(here))
            fail(QStringLiteral("The .torrent output must be outside the selected source folder."));
        auto parent = openRelative(current.fd, "..", true);
        if (here.sameIdentity(stamp(parent.fd))) return;
        current = std::move(parent);
    }
    fail(QStringLiteral("The output directory hierarchy is too deep."));
}

struct Snapshot {
    std::map<std::string, Stamp> entries;
    std::vector<lt::create_file_entry> files;
    qint64 totalBytes = 0;
    std::size_t pathBytes = 0;
};

void scanNode(int parent, const std::string &name, const std::string &relative, int depth,
              Snapshot &result, const std::atomic<bool> &cancelled)
{
    checkpoint(cancelled);
    const auto decoded = QString::fromUtf8(name);
    if (decoded.toUtf8().toStdString() != name || !safeName(decoded))
        fail(QStringLiteral("Source contains an unsafe or non-UTF-8 filename."));
    result.pathBytes += relative.size();
    if (depth > maxDepth || result.entries.size() >= maxEntries ||
        relative.size() > 4096 || result.pathBytes > maxPathBytes)
        fail(QStringLiteral("Source scan limit exceeded (10000 entries, depth 128, 16 MiB of paths)."));
    Stamp before;
    if (::fstatat(parent, name.c_str(), &before.value, AT_SYMLINK_NOFOLLOW) != 0)
        fileError(QStringLiteral("Cannot inspect source: %1").arg(QString::fromUtf8(relative)));
    if (!S_ISDIR(before.value.st_mode) && !S_ISREG(before.value.st_mode))
        fail(QStringLiteral("Only regular files and directories are allowed; symlinks and special files are rejected."));
    result.entries.emplace(relative, before);
    if (S_ISREG(before.value.st_mode)) {
        if (before.value.st_size < 0 || before.value.st_size > maxSourceBytes - result.totalBytes)
            fail(QStringLiteral("Source data exceeds the 1 TiB creation limit."));
        result.totalBytes += before.value.st_size;
        result.files.emplace_back(relative, before.value.st_size);
        return;
    }
    auto directory = openRelative(parent, name, true);
    if (!(before == stamp(directory.fd))) fail(QStringLiteral("Source changed during scanning."));
    const int copy = ::dup(directory.fd);
    if (copy < 0) fileError(QStringLiteral("Cannot enumerate source directory"));
    DIR *raw = ::fdopendir(copy);
    if (!raw) { ::close(copy); fileError(QStringLiteral("Cannot enumerate source directory")); }
    const std::unique_ptr<DIR, decltype(&::closedir)> listing(raw, ::closedir);
    while (true) {
        checkpoint(cancelled);
        errno = 0;
        const auto *entry = ::readdir(listing.get());
        if (!entry) {
            if (errno != 0) fileError(QStringLiteral("Cannot read source directory"));
            break;
        }
        const std::string child(entry->d_name);
        if (child == "." || child == "..") continue;
        scanNode(directory.fd, child, relative + '/' + child, depth + 1, result, cancelled);
    }
    if (!(before == stamp(directory.fd))) fail(QStringLiteral("Source changed during scanning."));
}

Snapshot scan(int parent, const std::string &name, const std::atomic<bool> &cancelled)
{
    Snapshot result;
    scanNode(parent, name, name, 0, result, cancelled);
    if (result.totalBytes == 0) fail(QStringLiteral("Select a source containing at least one non-empty regular file."));
    return result;
}

lt::sha256_hash pieceRoot(std::vector<lt::sha256_hash> leaves, std::size_t leafCount)
{
    std::size_t width = 1;
    while (width < leafCount) width *= 2;
    leaves.resize(width);
    while (width > 1) {
        for (std::size_t i = 0; i < width / 2; ++i) {
            lt::hasher256 hash;
            hash.update(leaves[2 * i].data(), 32);
            hash.update(leaves[2 * i + 1].data(), 32);
            leaves[i] = hash.final();
        }
        width /= 2;
    }
    return leaves.front();
}

void validateMetadata(const std::vector<char> &metadata)
{
    if (metadata.size() > maxMetadataBytes)
        fail(QStringLiteral("Metadata exceeds the engine's 16 MiB limit. Select a smaller source."));
    lt::load_torrent_limits limits;
    limits.max_buffer_size = int(maxMetadataBytes);
    limits.max_pieces = maxPieces;
    limits.max_decode_tokens = 500000;
    const auto loaded = lt::load_torrent_buffer(lt::span<char const>(metadata.data(), metadata.size()), limits);
    if (!loaded.ti || loaded.ti->layout().num_files() > maxLayoutFiles)
        fail(QStringLiteral("The Torrent exceeds the engine's 10000-file limit, including padding."));
}

void preflight(lt::create_torrent &torrent, const Snapshot &snapshot, const QStringList &trackers,
               const std::atomic<bool> &cancelled)
{
    checkpoint(cancelled);
    const auto count = int(torrent.end_file());
    if (count > maxLayoutFiles || torrent.num_pieces() > maxPieces)
        fail(QStringLiteral("Source exceeds the engine's 10000-file or 524288-piece limit, including padding."));
    std::size_t estimate = std::size_t(torrent.num_pieces()) * 52 + snapshot.pathBytes * 3 + count * 512 + 65536;
    for (const auto &tracker : trackers) estimate += tracker.toUtf8().size() * 2;
    if (estimate > maxMetadataBytes)
        fail(QStringLiteral("Estimated metadata exceeds the engine's 16 MiB limit. Select a smaller source."));
    // In-memory placeholders exercise the real parser's depth/token limits before
    // reading payload. Distinct per-file roots bound the piece-layer dictionary;
    // every placeholder is replaced by hashSources before anything is published.
    const auto placeholder = lt::hasher("preflight", 9).final();
    for (const auto piece : torrent.piece_range()) {
        checkpoint(cancelled);
        torrent.set_hash(piece, placeholder);
    }
    for (const auto index : torrent.file_range()) {
        if (torrent.file_at(index).flags & lt::file_storage::flag_pad_file) continue;
        for (const auto piece : torrent.file_piece_range(index)) {
            checkpoint(cancelled);
            const std::array<int, 2> key{int(index), int(piece)};
            torrent.set_hash2(index, piece, lt::hasher256(reinterpret_cast<const char *>(key.data()), sizeof(key)).final());
        }
    }
    validateMetadata(torrent.generate_buf());
}

void hashSources(lt::create_torrent &torrent, int parent, const Snapshot &snapshot,
                 const std::atomic<bool> &cancelled, const std::function<void(qint64, qint64)> &progress)
{
    std::array<char, blockSize> buffer {};
    lt::hasher v1;
    int v1Bytes = 0, v1Piece = 0;
    qint64 completed = 0, reported = 0;
    auto feedV1 = [&](const char *data, int length) {
        while (length > 0) {
            const int count = std::min(length, pieceSize - v1Bytes);
            v1.update(data, count);
            v1Bytes += count; data += count; length -= count;
            if (v1Bytes == pieceSize) {
                torrent.set_hash(lt::piece_index_t(v1Piece++), v1.final());
                v1.reset(); v1Bytes = 0;
            }
        }
    };
    progress(0, snapshot.totalBytes);
    for (const auto index : torrent.file_range()) {
        checkpoint(cancelled);
        const auto &file = torrent.file_at(index);
        if (file.flags & lt::file_storage::flag_pad_file) {
            buffer.fill(0);
            for (qint64 left = file.size; left > 0;) {
                checkpoint(cancelled);
                const auto count = int(std::min<qint64>(left, buffer.size()));
                feedV1(buffer.data(), count);
                left -= count;
            }
            continue;
        }
        const auto expected = snapshot.entries.find(file.filename);
        if (expected == snapshot.entries.end()) fail(QStringLiteral("Unexpected file in generated Torrent layout."));
        auto input = openRelative(parent, file.filename);
        if (!(expected->second == stamp(input.fd))) fail(QStringLiteral("Source changed before hashing."));
        std::vector<lt::sha256_hash> leaves;
        leaves.reserve(pieceSize / blockSize);
        int filePiece = 0;
        for (qint64 remaining = file.size; remaining > 0;) {
            checkpoint(cancelled);
            const int count = int(std::min<qint64>(remaining, buffer.size()));
            int offset = 0;
            while (offset < count) {
                checkpoint(cancelled);
                const auto bytes = ::read(input.fd, buffer.data() + offset, count - offset);
                if (bytes < 0 && errno == EINTR) continue;
                if (bytes < 0) fileError(QStringLiteral("Cannot read source"));
                if (bytes == 0) fail(QStringLiteral("Source size changed while hashing."));
                offset += int(bytes);
            }
            feedV1(buffer.data(), count);
            leaves.push_back(lt::hasher256(buffer.data(), count).final());
            remaining -= count;
            completed += count;
            if (leaves.size() == pieceSize / blockSize || remaining == 0) {
                // BEP 52: a file shorter than one piece uses the smallest complete
                // block tree. A final piece of a larger file uses a full piece tree.
                const auto blocks = file.size < pieceSize ? (file.size + blockSize - 1) / blockSize : pieceSize / blockSize;
                torrent.set_hash2(index, lt::piece_index_t::diff_type(filePiece++), pieceRoot(std::move(leaves), blocks));
                leaves.clear();
                leaves.reserve(pieceSize / blockSize);
            }
            if (completed - reported >= pieceSize) {
                progress(completed, snapshot.totalBytes);
                reported = completed;
            }
        }
        if (!(expected->second == stamp(input.fd))) fail(QStringLiteral("Source changed while hashing."));
    }
    if (v1Bytes > 0) torrent.set_hash(lt::piece_index_t(v1Piece++), v1.final());
    if (v1Piece != torrent.num_pieces()) fail(QStringLiteral("Unexpected Torrent piece layout."));
    progress(completed, snapshot.totalBytes);
    checkpoint(cancelled);
}

struct TemporaryOutput {
    int parent;
    std::string name;
    Handle handle;
    explicit TemporaryOutput(int directory) : parent(directory),
        name(".torrent-create-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".tmp"),
        handle(::openat(parent, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)) {
        if (handle.fd < 0) fileError(QStringLiteral("Cannot create temporary metadata file"));
    }
    ~TemporaryOutput() { ::unlinkat(parent, name.c_str(), 0); }
};

std::pair<QString, QString> create(const CreateTorrentOptions &options, const std::atomic<bool> &cancelled,
    const std::function<void(qint64, qint64)> &progress, std::mutex &publicationMutex, bool &committed)
{
    const auto source = checkedPath(options.sourcePath), output = checkedPath(options.outputPath);
    const auto trackers = checkedTrackers(options.trackers);
    const QFileInfo sourceInfo(source), outputInfo(output);
    const auto sourceName = QFile::encodeName(sourceInfo.fileName()).toStdString();
    const auto outputName = QFile::encodeName(outputInfo.fileName()).toStdString();
    auto sourceParent = openDirectory(sourceInfo.absolutePath());
    auto outputParent = openDirectory(outputInfo.absolutePath());
    const auto originalSourceParent = stamp(sourceParent.fd), originalOutputParent = stamp(outputParent.fd);
    requireAbsent(outputParent.fd, outputName);
    progress(0, 0);
    const auto snapshot = scan(sourceParent.fd, sourceName, cancelled);
    requireOutsideSource(outputParent.fd, snapshot.entries.at(sourceName));
    lt::create_torrent torrent(snapshot.files, pieceSize);
    torrent.set_priv(options.privateTorrent);
    torrent.set_creator("EiskaltDC++");
    for (qsizetype i = 0; i < trackers.size(); ++i) torrent.add_tracker(trackers[i].toStdString(), int(i));
    preflight(torrent, snapshot, trackers, cancelled);
    hashSources(torrent, sourceParent.fd, snapshot, cancelled, progress);
    const auto metadata = torrent.generate_buf();
    validateMetadata(metadata);
    checkpoint(cancelled);
    TemporaryOutput temporary(outputParent.fd);
    for (std::size_t offset = 0; offset < metadata.size();) {
        checkpoint(cancelled);
        const auto bytes = ::write(temporary.handle.fd, metadata.data() + offset,
                                   std::min<std::size_t>(pieceSize, metadata.size() - offset));
        if (bytes < 0 && errno == EINTR) continue;
        if (bytes <= 0) fileError(QStringLiteral("Cannot write metadata"));
        offset += std::size_t(bytes);
    }
    if (::fsync(temporary.handle.fd) != 0) fileError(QStringLiteral("Cannot flush metadata"));
    if (scan(sourceParent.fd, sourceName, cancelled).entries != snapshot.entries)
        fail(QStringLiteral("Source files or directory entries changed during creation. No metadata was published."));
    auto currentSourceParent = openDirectory(sourceInfo.absolutePath());
    auto currentOutputParent = openDirectory(outputInfo.absolutePath());
    if (!originalSourceParent.sameIdentity(stamp(currentSourceParent.fd)) ||
        !originalOutputParent.sameIdentity(stamp(currentOutputParent.fd)))
        fail(QStringLiteral("A source or output ancestor changed during creation."));
    requireOutsideSource(outputParent.fd, snapshot.entries.at(sourceName));
    // linkat is the no-replace publication point. Unlike QSaveFile::commit or
    // rename, it cannot clobber a file/symlink created by someone during hashing.
    std::lock_guard lock(publicationMutex);
    checkpoint(cancelled);
    if (::linkat(outputParent.fd, temporary.name.c_str(), outputParent.fd, outputName.c_str(), 0) != 0)
        fileError(QStringLiteral("Cannot publish metadata without overwriting an existing path"));
    committed = true;
    return {output, sourceInfo.absolutePath()};
}
#endif
}

struct TorrentCreator::Impl {
    std::thread worker;
    std::atomic<bool> cancellation{false};
    std::mutex publicationMutex;
    bool running = false;
    bool committed = false;
};

TorrentCreator::TorrentCreator(QObject *parent) : QObject(parent), d(new Impl) {}
TorrentCreator::~TorrentCreator()
{
    cancel();
    if (d->worker.joinable()) d->worker.join();
}

bool TorrentCreator::start(const CreateTorrentOptions &options)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (d->running) return false;
    if (d->worker.joinable()) d->worker.join();
    {
        std::lock_guard lock(d->publicationMutex);
        d->cancellation = false;
        d->committed = false;
    }
    d->running = true;
    try {
        d->worker = std::thread([this, options] {
            QString error;
            bool wasCancelled = false;
            std::pair<QString, QString> result;
            try {
#ifdef Q_OS_UNIX
                result = create(options, d->cancellation, [this](qint64 completed, qint64 total) {
                    emit progress(completed, total);
                }, d->publicationMutex, d->committed);
#else
                fail(QStringLiteral("Secure no-follow Torrent creation is not supported on this platform."));
#endif
            } catch (const Cancelled &) { wasCancelled = true; }
            catch (const Failure &failure) { error = failure.message; }
            catch (const std::exception &failure) { error = QString::fromUtf8(failure.what()); }
            catch (...) { error = QStringLiteral("Unexpected Torrent creation failure."); }
            QMetaObject::invokeMethod(this, [this, result, error, wasCancelled] {
                if (d->worker.joinable()) d->worker.join();
                d->running = false;
                if (wasCancelled) emit cancelled();
                else if (!error.isEmpty()) emit failed(error);
                else emit finished(result.first, result.second);
            }, Qt::QueuedConnection);
        });
    } catch (const std::exception &failure) {
        const auto message = QString::fromUtf8(failure.what());
        QMetaObject::invokeMethod(this, [this, message] {
            d->running = false;
            emit failed(message);
        }, Qt::QueuedConnection);
    }
    return true;
}

bool TorrentCreator::isRunning() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return d->running;
}

void TorrentCreator::cancel()
{
    std::lock_guard lock(d->publicationMutex);
    if (!d->committed) d->cancellation = true;
}
} // namespace eiskalt::torrent
