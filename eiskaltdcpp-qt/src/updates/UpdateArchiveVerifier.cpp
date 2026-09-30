#include "UpdateArchiveVerifier.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <openssl/evp.h>
#include <memory>
#include <algorithm>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace updates {
namespace {
QByteArray decode(const QByteArray& encoded, int size) {
    if(encoded.size() != ((size + 2) / 3) * 4) return {};
    const auto raw = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    return raw.size() == size && raw.toBase64() == encoded ? raw : QByteArray{};
}
#ifndef _WIN32
struct Descriptor {
    int value;
    ~Descriptor() { if(value >= 0) ::close(value); }
};
bool sameFile(const struct stat& first, const struct stat& second) {
#ifdef __APPLE__
    const bool times = first.st_mtimespec.tv_sec == second.st_mtimespec.tv_sec &&
        first.st_mtimespec.tv_nsec == second.st_mtimespec.tv_nsec &&
        first.st_ctimespec.tv_sec == second.st_ctimespec.tv_sec &&
        first.st_ctimespec.tv_nsec == second.st_ctimespec.tv_nsec;
#else
    const bool times = first.st_mtim.tv_sec == second.st_mtim.tv_sec &&
        first.st_mtim.tv_nsec == second.st_mtim.tv_nsec &&
        first.st_ctim.tv_sec == second.st_ctim.tv_sec &&
        first.st_ctim.tv_nsec == second.st_ctim.tv_nsec;
#endif
    return times && first.st_dev == second.st_dev && first.st_ino == second.st_ino &&
        first.st_size == second.st_size && first.st_mode == second.st_mode &&
        first.st_uid == second.st_uid && second.st_nlink == 1;
}
#endif
}

bool verifyUpdateArchive(const QString& path, const ArchiveExpectation& expected,
    const QByteArray& pinnedPublicKeyBase64, const std::function<bool()>& cancelled) {
    try {
        const auto stopped = [&] { return cancelled && cancelled(); };
        if(stopped() || expected.size <= 0 || expected.size > 512LL * 1024 * 1024 ||
            expected.sha256.size() != 32 || path.contains(QChar('\0'))) return false;
        const auto key = decode(pinnedPublicKeyBase64, 32);
        const auto signature = decode(expected.signatureBase64, 64);
        if(key.isEmpty() || signature.isEmpty()) return false;
#ifdef _WIN32
        // Self-installation is unsupported until private staging ACLs are verified.
        return false;
#else
        const QFileInfo info(path);
        if(!info.isAbsolute() || info.fileName().isEmpty()) return false;
        Descriptor parent{::open(QFile::encodeName(info.absolutePath()).constData(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        struct stat parentStat{};
        if(parent.value < 0 || ::fstat(parent.value, &parentStat) != 0 ||
            parentStat.st_uid != getuid() || (parentStat.st_mode & 0077)) return false;
        Descriptor source{::openat(parent.value, QFile::encodeName(info.fileName()).constData(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
        struct stat before{};
        if(source.value < 0 || ::fstat(source.value, &before) != 0 || !S_ISREG(before.st_mode) ||
            before.st_uid != getuid() || before.st_nlink != 1 || (before.st_mode & 0077) ||
            before.st_size != expected.size || stopped()) return false;
        QFile file;
        if(!file.open(source.value, QIODevice::ReadOnly, QFileDevice::DontCloseHandle)) return false;
        // Ed25519 verification is one-shot. Map the bounded, exclusively owned
        // staging file instead of collecting a second archive-sized heap buffer.
        auto* bytes = file.map(0, expected.size);
        if(!bytes) return false;
        const auto unmap = [&file](uchar* address) { file.unmap(address); };
        std::unique_ptr<uchar, decltype(unmap)> mapping(bytes, unmap);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for(qint64 offset = 0; offset < expected.size;) {
            if(stopped()) return false;
            const auto count = std::min<qint64>(64 * 1024, expected.size - offset);
            hash.addData(QByteArrayView(reinterpret_cast<const char*>(bytes + offset), count));
            offset += count;
        }
        if(hash.result() != expected.sha256 || stopped()) return false;
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> publicKey(
            EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                reinterpret_cast<const unsigned char*>(key.constData()), key.size()), EVP_PKEY_free);
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if(!publicKey || !context ||
            EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, publicKey.get()) != 1 ||
            EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature.constData()),
                signature.size(), bytes, static_cast<size_t>(expected.size)) != 1 || stopped()) return false;
        struct stat after{}, named{}, parentAfter{}, namedParent{};
        return ::fstat(source.value, &after) == 0 && sameFile(before, after) &&
            ::fstatat(parent.value, QFile::encodeName(info.fileName()).constData(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
            sameFile(before, named) && ::fstat(parent.value, &parentAfter) == 0 &&
            parentAfter.st_uid == getuid() && !(parentAfter.st_mode & 0077) &&
            ::lstat(QFile::encodeName(info.absolutePath()).constData(), &namedParent) == 0 &&
            S_ISDIR(namedParent.st_mode) && namedParent.st_dev == parentStat.st_dev &&
            namedParent.st_ino == parentStat.st_ino;
#endif
    } catch(...) {
        return false;
    }
}
}
