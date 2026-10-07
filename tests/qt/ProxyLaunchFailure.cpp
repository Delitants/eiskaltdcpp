// Disposable macOS test-process interposer; never linked into the application.
#include <atomic>
#include <cerrno>
#include <pthread.h>
#include <cstdint>

namespace {
std::atomic<uint64_t> armedThread{0};
void (*observe)(void*) = nullptr;
void* context = nullptr;
int failLaunch(pthread_t* thread, const pthread_attr_t* attributes,
               void* (*entry)(void*), void* argument) {
    uint64_t current = 0;
    pthread_threadid_np(nullptr, &current);
    auto expected = current;
    if(armedThread.compare_exchange_strong(expected, 0)) {
        observe(context);
        return EAGAIN;
    }
    return ::pthread_create(thread, attributes, entry, argument);
}
}
extern "C" void eiskalt_qa_proxy_launch_arm(void (*callback)(void*), void* value) {
    observe = callback;
    context = value;
    uint64_t current = 0;
    pthread_threadid_np(nullptr, &current);
    armedThread.store(current);
}
extern "C" void eiskalt_qa_proxy_launch_reset() { armedThread.store(0); }
__attribute__((used, section("__DATA,__interpose"))) static const struct {
    const void* replacement;
    const void* original;
} replacement{reinterpret_cast<const void*>(failLaunch), reinterpret_cast<const void*>(::pthread_create)};
