cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(eiskalt-torrent LANGUAGES CXX)
    include(CTest)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(LibtorrentRasterbar 2.1 CONFIG REQUIRED)
find_package(Threads REQUIRED)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/ProxyTrust.cmake)

function(eiskalt_torrent_match_dependency_abi target)
    get_target_property(configs LibtorrentRasterbar::torrent-rasterbar IMPORTED_CONFIGURATIONS)
    if(configs AND NOT "DEBUG" IN_LIST configs)
        # Release-only packages may export a Debug-only assert define. In 2.1
        # this changes bitfield/alert layout, so consumers must match the binary.
        if(MSVC)
            target_compile_options(${target} PRIVATE "$<$<CONFIG:Debug>:/UTORRENT_USE_ASSERTS>")
        else()
            target_compile_options(${target} PRIVATE "$<$<CONFIG:Debug>:-UTORRENT_USE_ASSERTS>")
        endif()
    endif()
endfunction()

add_library(eiskalt-torrent STATIC
    TorrentTypes.h TorrentSettings.h TorrentSettings.cpp
    ProxyTrust.h ProxyTrust.cpp
    SocksUdpBootstrap.h SocksUdpBootstrap.cpp
    TorrentEngine.h TorrentEngine.cpp
    TorrentCreator.h TorrentCreator.cpp)
add_library(eiskalt::torrent ALIAS eiskalt-torrent)
set_target_properties(eiskalt-torrent PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_compile_features(eiskalt-torrent PUBLIC cxx_std_20)
target_include_directories(eiskalt-torrent PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(eiskalt-torrent PUBLIC Qt6::Core
    PRIVATE Qt6::Network LibtorrentRasterbar::torrent-rasterbar Threads::Threads eiskalt-proxy-trust)
eiskalt_torrent_match_dependency_abi(eiskalt-torrent)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND BUILD_TESTING)
    add_subdirectory(../tests/torrent tests)
endif()
