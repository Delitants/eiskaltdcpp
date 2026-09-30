cmake_minimum_required(VERSION 3.23)
# Standalone configuration keeps the feasibility gate independent of app builds.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(eiskalt-updater-probe LANGUAGES CXX OBJCXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD 17)
    if(NOT CATCH2_SOURCE_DIR)
        message(FATAL_ERROR "Provide the existing pinned Catch2 source")
    endif()
    add_subdirectory("${CATCH2_SOURCE_DIR}" catch2 EXCLUDE_FROM_ALL)
endif()

if(NOT APPLE)
    return()
endif()
enable_language(OBJCXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/MacUpdater.cmake")
add_executable(eiskalt-updater-native-tests main.mm ../../../macos/updater/BundleArchiveVerifier.mm)
target_compile_options(eiskalt-updater-native-tests PRIVATE -fobjc-arc)
target_include_directories(eiskalt-updater-native-tests PRIVATE
    "${EISKALT_SPARKLE_ROOT}/Sparkle.framework/Headers")
target_compile_options(eiskalt-updater-native-tests PRIVATE "-F${EISKALT_SPARKLE_ROOT}")
target_link_options(eiskalt-updater-native-tests PRIVATE "-F${EISKALT_SPARKLE_ROOT}")
target_link_libraries(eiskalt-updater-native-tests PRIVATE Catch2::Catch2
    "-framework AppKit" Sparkle::Sparkle "-lz")
set_target_properties(eiskalt-updater-native-tests PROPERTIES
    BUILD_RPATH "@executable_path/../Frameworks;${EISKALT_SPARKLE_ROOT}")
