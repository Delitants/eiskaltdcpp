#include "PartfileWriteFault.h"
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct PartfileWriteFault {
    std::mutex mutex;
    dev_t device = 0;
    ino_t inode = 0;
    int error = 0;
    int failures = 0;
    bool buffered = false;
} fault;
bool failMetadataWrite(int fd, off_t offset, bool buffered = false) {
    struct stat file{};
    std::lock_guard lock(fault.mutex);
    if(fault.error && fault.buffered == buffered && offset == 0 && fstat(fd, &file) == 0 &&
       file.st_dev == fault.device && file.st_ino == fault.inode) {
        ++fault.failures;
        errno = fault.error;
        return true;
    }
    return false;
}
}

namespace selected_recheck_test {
ScopedPartfileWriteFault::ScopedPartfileWriteFault(int fd, int error) {
    struct stat file{};
    if(fstat(fd, &file) != 0) throw std::runtime_error("Cannot identify disposable partfile inode");
    std::lock_guard lock(fault.mutex);
    fault.device = file.st_dev;
    fault.inode = file.st_ino;
    fault.buffered = error < 0;
    fault.error = error < 0 ? -error : error;
    fault.failures = 0;
}
void ScopedPartfileWriteFault::clear() {
    std::lock_guard lock(fault.mutex);
    fault.error = 0;
}
int ScopedPartfileWriteFault::failures() const {
    std::lock_guard lock(fault.mutex);
    return fault.failures;
}
ScopedPartfileWriteFault::~ScopedPartfileWriteFault() { clear(); }
}

// Mach-O interposition must reside in a test-only dylib. Only metadata writes to
// the explicitly armed disposable inode fail; payload and unrelated I/O pass.
extern "C" ssize_t eiskalt_test_pwrite(int fd, void const* data, size_t size, off_t offset) {
    if(failMetadataWrite(fd, offset)) return -1;
    return ::pwrite(fd, data, size, offset);
}
extern "C" size_t eiskalt_test_fwrite(void const* data, size_t size, size_t count, FILE* stream) {
    if(failMetadataWrite(fileno(stream), ftello(stream))) return 0;
    return ::fwrite(data, size, count, stream);
}
extern "C" int eiskalt_test_fflush(FILE* stream) {
    if(stream && failMetadataWrite(fileno(stream), 0, true)) {
        const int error = errno;
        // Discard the failed staging buffer so fclose cannot retry it behind
        // the caller's back; this models a write that never reached the file.
        fpurge(stream);
        errno = error;
        return EOF;
    }
    return ::fflush(stream);
}
__attribute__((used)) static struct {
    void const* replacement;
    void const* original;
} const partfileWriteInterpose[] __attribute__((section("__DATA,__interpose"))) = {
    {reinterpret_cast<void const*>(&eiskalt_test_pwrite), reinterpret_cast<void const*>(&::pwrite)},
    {reinterpret_cast<void const*>(&eiskalt_test_fwrite), reinterpret_cast<void const*>(&::fwrite)},
    {reinterpret_cast<void const*>(&eiskalt_test_fflush), reinterpret_cast<void const*>(&::fflush)}
};
