#include <dlfcn.h>
#include <pthread.h>

namespace {
struct CoreWriteProbe {
    void (*arm)(const char*, int) = reinterpret_cast<void(*)(const char*, int)>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_arm"));
    int (*entered)() = reinterpret_cast<int(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_entered"));
    int (*released)() = reinterpret_cast<int(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_released"));
    unsigned long long (*writer)() = reinterpret_cast<unsigned long long(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_remove_thread"));
    void (*release)() = reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT, "eiskalt_qa_hold_release"));
    uint64_t gui = 0;
    CoreWriteProbe() {
        REQUIRE(arm); REQUIRE(entered); REQUIRE(released); REQUIRE(writer); REQUIRE(release);
        REQUIRE(pthread_threadid_np(nullptr, &gui) == 0);
    }
    ~CoreWriteProbe() { release(); }
    std::future<bool> watchdog(std::atomic_int& ticks, std::atomic_bool& advanced) {
        return std::async(std::launch::async, [&, this] {
            QElapsedTimer timer; timer.start();
            while(!entered() && timer.elapsed() < 3000) QThread::msleep(1);
            const bool hit = entered();
            if(hit) {
                const auto before = ticks.load();
                QThread::msleep(350);
                advanced = ticks.load() > before;
            }
            release(); return hit;
        });
    }
};
}
