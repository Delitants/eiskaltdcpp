// Disposable test-process interposer. Never linked into the application.
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <pthread.h>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
char prefix[PATH_MAX]{};
std::atomic_int mode{0}, removalCall{0};
std::atomic<unsigned long long> threadId{0};
std::atomic_bool entered{false}, released{true};
void hold(int fd, int operation) {
    if(mode.load() != operation) return;
    char path[PATH_MAX]{};
    if(fcntl(fd, F_GETPATH, path) != 0 || std::strncmp(path, prefix, std::strlen(prefix)) != 0) return;
    if(operation == 2) {
        const auto length = std::strlen(path);
        const char suffix[] = "/update.zip";
        if(length < sizeof(suffix) - 1 || std::strcmp(path + length - (sizeof(suffix) - 1), suffix) != 0) return;
    }
    entered.store(true);
    while(!released.load()) usleep(1000);
}
ssize_t heldWrite(int fd, const void* data, size_t size) { hold(fd, 1); return ::write(fd, data, size); }
ssize_t heldRead(int fd, void* data, size_t size) { hold(fd, 2); return ::read(fd, data, size); }
int heldSync(int fd) { hold(fd, 3); return ::fsync(fd); }
void* heldMap(void* address, size_t size, int protection, int flags, int fd, off_t offset) {
    hold(fd, 2); return ::mmap(address, size, protection, flags, fd, offset);
}
void holdPath(const char* path, int operation, int syscall) {
    const int saved = errno;
    const auto size = std::strlen(prefix);
    if(mode.load() == operation && path && size &&
        std::strncmp(path, prefix, size) == 0 && (!path[size] || path[size] == '/')) {
        uint64_t id = 0; pthread_threadid_np(nullptr, &id);
        removalCall.store(syscall); threadId.store(id); entered.store(true);
        std::fprintf(stderr, "REMOVAL_HOLD call=%d thread=%llu path=%s\n", syscall,
            static_cast<unsigned long long>(id), path);
        while(!released.load()) usleep(1000);
    }
    errno = saved;
}
int heldUnlink(const char* path) { holdPath(path, 4, 1); return ::unlink(path); }
int heldUnlinkAt(int fd, const char* path, int flags) {
    const int saved = errno;
    char absolute[PATH_MAX]{}, parent[PATH_MAX]{};
    if(path && path[0] == '/') holdPath(path, 4, 2);
    else if(path && ((fd == AT_FDCWD && getcwd(parent, sizeof(parent))) ||
        (fd != AT_FDCWD && fcntl(fd, F_GETPATH, parent) == 0))) {
        const auto size = std::snprintf(absolute, sizeof(absolute), "%s/%s", parent, path);
        if(size > 0 && size < sizeof(absolute)) holdPath(absolute, 4, 2);
    }
    errno = saved;
    return ::unlinkat(fd, path, flags);
}
int heldRmdir(const char* path) { holdPath(path, 5, 3); return ::rmdir(path); }
}
extern "C" void eiskalt_qa_hold_arm(const char* path, int operation) {
    mode.store(0);
    std::strncpy(prefix, path, sizeof(prefix) - 1); prefix[sizeof(prefix) - 1] = 0;
    entered.store(false); released.store(false); removalCall.store(0); threadId.store(0); mode.store(operation);
}
extern "C" int eiskalt_qa_hold_released() { return released.load(); }
extern "C" int eiskalt_qa_remove_call() { return removalCall.load(); }
extern "C" unsigned long long eiskalt_qa_remove_thread() { return threadId.load(); }
extern "C" int eiskalt_qa_hold_entered() { return entered.load(); }
extern "C" void eiskalt_qa_hold_release() { released.store(true); mode.store(0); }
__attribute__((used, section("__DATA,__interpose"))) static const struct {
    const void* replacement;
    const void* original;
} replacements[] = {
    {reinterpret_cast<const void*>(heldWrite), reinterpret_cast<const void*>(::write)},
    {reinterpret_cast<const void*>(heldRead), reinterpret_cast<const void*>(::read)},
    {reinterpret_cast<const void*>(heldSync), reinterpret_cast<const void*>(::fsync)},
    {reinterpret_cast<const void*>(heldMap), reinterpret_cast<const void*>(::mmap)},
    {reinterpret_cast<const void*>(heldUnlink), reinterpret_cast<const void*>(::unlink)},
    {reinterpret_cast<const void*>(heldUnlinkAt), reinterpret_cast<const void*>(::unlinkat)},
    {reinterpret_cast<const void*>(heldRmdir), reinterpret_cast<const void*>(::rmdir)}
};
