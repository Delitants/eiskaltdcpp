#pragma once

#ifdef EISKALT_TEST_INTERPOSE_PARTFILE_PWRITE
#include <cerrno>
namespace selected_recheck_test {
struct ScopedPartfileWriteFault {
    ScopedPartfileWriteFault(int fd, int error);
    ~ScopedPartfileWriteFault();
    void clear();
    int failures() const;
    ScopedPartfileWriteFault(ScopedPartfileWriteFault const&) = delete;
    ScopedPartfileWriteFault& operator=(ScopedPartfileWriteFault const&) = delete;
};
}
#endif
