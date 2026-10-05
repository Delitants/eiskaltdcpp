include(CheckCXXSourceRuns)
include(CMakePushCheckState)
option(EISKALT_REQUIRE_SELECTED_RECHECK "Require the maintained libtorrent recheck extension" OFF)
# Neither a linked marker nor a cached run proves the current library's ABI.
foreach(_suffix "" "_COMPILED" "_EXITCODE" "_EXITCODE__TRYRUN_OUTPUT")
    unset(EISKALT_SELECTED_RECHECK_AVAILABLE${_suffix} CACHE)
    unset(EISKALT_SELECTED_RECHECK_AVAILABLE${_suffix})
endforeach()
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(STATUS "Selected rechecking unavailable: cross-compilation requires a runnable CMAKE_CROSSCOMPILING_EMULATOR")
    set(EISKALT_SELECTED_RECHECK_AVAILABLE 0)
    set(EISKALT_SELECTED_RECHECK_AVAILABLE 0 CACHE INTERNAL "Verified selected-recheck ABI")
else()
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES LibtorrentRasterbar::torrent-rasterbar)
    check_cxx_source_runs("#include <libtorrent/eiskalt_selected_recheck.hpp>
static_assert(LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI == 2, \"Selected-recheck ABI mismatch\");
int main() { return libtorrent::eiskalt_selected_recheck_abi() != 2; }"
        EISKALT_SELECTED_RECHECK_AVAILABLE)
    cmake_pop_check_state()
    # FAILED_TO_RUN is a nonempty string, not a capability receipt.
    if(NOT "${EISKALT_SELECTED_RECHECK_AVAILABLE}" STREQUAL "1")
        set(EISKALT_SELECTED_RECHECK_AVAILABLE 0)
        set(EISKALT_SELECTED_RECHECK_AVAILABLE 0 CACHE INTERNAL "Verified selected-recheck ABI" FORCE)
    endif()
endif()
if(EISKALT_REQUIRE_SELECTED_RECHECK AND NOT EISKALT_SELECTED_RECHECK_AVAILABLE)
    message(FATAL_ERROR "Selected rechecking requires the matching maintained libtorrent extension (ABI 2)")
endif()

function(eiskalt_torrent_selected_recheck_capability target)
    target_compile_definitions(${target} PRIVATE
        EISKALT_SELECTED_RECHECK_AVAILABLE=$<BOOL:${EISKALT_SELECTED_RECHECK_AVAILABLE}>)
endfunction()
