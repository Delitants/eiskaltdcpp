include(CheckCXXSourceCompiles)
option(EISKALT_REQUIRE_SELECTED_RECHECK "Require the maintained libtorrent recheck extension" OFF)
set(CMAKE_REQUIRED_LIBRARIES LibtorrentRasterbar::torrent-rasterbar)
# Cached successes otherwise survive changed headers or a different library prefix.
unset(EISKALT_SELECTED_RECHECK_AVAILABLE CACHE)
unset(EISKALT_SELECTED_RECHECK_AVAILABLE)
check_cxx_source_compiles("#include <libtorrent/eiskalt_selected_recheck.hpp>
static_assert(LIBTORRENT_EISKALT_SELECTED_RECHECK_ABI == 1);
int main() { return libtorrent::eiskalt_selected_recheck_abi() != 1; }"
    EISKALT_SELECTED_RECHECK_AVAILABLE)
unset(CMAKE_REQUIRED_LIBRARIES)
if(EISKALT_REQUIRE_SELECTED_RECHECK AND NOT EISKALT_SELECTED_RECHECK_AVAILABLE)
    message(FATAL_ERROR "Selected rechecking requires the matching maintained libtorrent extension (ABI 1)")
endif()

function(eiskalt_torrent_selected_recheck_capability target)
    target_compile_definitions(${target} PRIVATE
        EISKALT_SELECTED_RECHECK_AVAILABLE=$<BOOL:${EISKALT_SELECTED_RECHECK_AVAILABLE}>)
endfunction()
