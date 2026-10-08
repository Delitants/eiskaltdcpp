# CMAKE_PROJECT_libtorrent_INCLUDE hook: leave the pinned upstream tree intact.
if(APPLE)
    find_package(OpenSSL REQUIRED)
    include("${CMAKE_CURRENT_LIST_DIR}/OpenSSLProviderHeaders.cmake")
    eiskalt_isolate_openssl_headers("${OPENSSL_INCLUDE_DIR}")
    cmake_language(DEFER CALL eiskalt_attach_openssl_headers)
endif()
