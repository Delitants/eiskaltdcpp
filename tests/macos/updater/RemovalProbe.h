#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <cerrno>
#include <cstdio>
#include <unistd.h>

namespace {
struct RemovalProbe {
    void (*arm)(const char*, int) = reinterpret_cast<void(*)(const char*, int)>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_arm"));
    int (*entered)() = reinterpret_cast<int(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_entered"));
    int (*released)() = reinterpret_cast<int(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_released"));
    int (*call)() = reinterpret_cast<int(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_remove_call"));
    unsigned long long (*workerThread)() = reinterpret_cast<unsigned long long(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_remove_thread"));
    void (*release)() = reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_release"));
    uint64_t guiThread = 0;
    RemovalProbe() {
        REQUIRE(arm); REQUIRE(entered); REQUIRE(released); REQUIRE(call); REQUIRE(workerThread); REQUIRE(release);
        REQUIRE(pthread_threadid_np(nullptr, &guiThread) == 0);
    }
    ~RemovalProbe() { release(); }
    void start(const QString& path, int operation) { arm(QFile::encodeName(path).constData(), operation); }
    std::future<bool> watchdog() {
        return std::async(std::launch::async, [this] {
            QElapsedTimer deadline; deadline.start();
            while(!entered() && deadline.elapsed() < 2000) QThread::msleep(1);
            const bool hit = entered();
            if(hit) QThread::msleep(350);
            release(); return hit;
        });
    }
    void trace(const char* site, qint64 elapsed) {
        std::fprintf(stderr, "REMOVAL_PROBE site=%s elapsed_ms=%lld call=%d on_gui=%d released=%d\n",
            site, static_cast<long long>(elapsed), call(), workerThread() == guiThread, released());
    }
};
}
