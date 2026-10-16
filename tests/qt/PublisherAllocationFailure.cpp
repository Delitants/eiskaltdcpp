// Disposable test-process interposer; never linked into the application.
#include <atomic>
#include <cstdlib>
#include <pthread.h>
#include <cstdint>

namespace {
std::atomic<uint64_t> armedThread{0};
std::atomic<unsigned> failures{0};
void* failAllocation(size_t size) {
    uint64_t current = 0;
    pthread_threadid_np(nullptr, &current);
    if (current != 0 && armedThread.load() == current) {
        ++failures;
        return nullptr;
    }
    return ::malloc(size);
}
}
extern "C" void eiskalt_qa_publisher_allocation_arm() {
    uint64_t current = 0;
    pthread_threadid_np(nullptr, &current);
    failures = 0;
    armedThread = current;
}
extern "C" unsigned eiskalt_qa_publisher_allocation_reset() {
    armedThread = 0;
    return failures.load();
}
__attribute__((used, section("__DATA,__interpose"))) static const struct {
    const void* replacement;
    const void* original;
} replacement{reinterpret_cast<const void*>(failAllocation), reinterpret_cast<const void*>(::malloc)};
