if(NOT TARGET Catch2::Catch2WithMain)
    if(TORRENT_CATCH2_SOURCE_DIR)
        add_subdirectory("${TORRENT_CATCH2_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/catch2" EXCLUDE_FROM_ALL)
    else()
        find_package(Catch2 3 CONFIG REQUIRED)
    endif()
endif()

# Compile the engine in this test translation unit for the private test probe.
# The public library contains neither a peer-injection API nor test hooks.
add_executable(eiskalt-torrent-tests test_torrent.cpp test_torrentcreator.cpp
    ../../torrent/SocksUdpBootstrap.cpp
    ../../torrent/ProxyTrust.cpp
    ../../torrent/TorrentSettings.cpp ../../torrent/TorrentEngine.h
    ../../torrent/TorrentCreator.cpp ../../torrent/TorrentCreator.h)
set_target_properties(eiskalt-torrent-tests PROPERTIES AUTOMOC ON)
target_compile_features(eiskalt-torrent-tests PRIVATE cxx_std_20)
target_include_directories(eiskalt-torrent-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(eiskalt-torrent-tests PRIVATE Qt6::Core Qt6::Network
    LibtorrentRasterbar::torrent-rasterbar Threads::Threads Catch2::Catch2WithMain eiskalt-proxy-trust)
eiskalt_torrent_match_dependency_abi(eiskalt-torrent-tests)
add_test(NAME torrent-backend COMMAND eiskalt-torrent-tests)
set_tests_properties(torrent-backend PROPERTIES TIMEOUT 180)
