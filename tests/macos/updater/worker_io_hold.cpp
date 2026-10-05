// Disposable test-process interposer. Never linked into the application.
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {
char prefix[PATH_MAX]{};
std::atomic_int mode{0};
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
}
extern "C" void eiskalt_qa_hold_arm(const char* path, int operation) {
    mode.store(0);
    std::strncpy(prefix, path, sizeof(prefix) - 1); prefix[sizeof(prefix) - 1] = 0;
    entered.store(false); released.store(false); mode.store(operation);
}
extern "C" int eiskalt_qa_hold_entered() { return entered.load(); }
extern "C" void eiskalt_qa_hold_release() { released.store(true); mode.store(0); }
__attribute__((used, section("__DATA,__interpose"))) static const struct {
    const void* replacement;
    const void* original;
} replacements[] = {
    {reinterpret_cast<const void*>(heldWrite), reinterpret_cast<const void*>(::write)},
    {reinterpret_cast<const void*>(heldRead), reinterpret_cast<const void*>(::read)},
    {reinterpret_cast<const void*>(heldSync), reinterpret_cast<const void*>(::fsync)},
    {reinterpret_cast<const void*>(heldMap), reinterpret_cast<const void*>(::mmap)}
};
