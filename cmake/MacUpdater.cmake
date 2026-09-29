# Optional dependency only: this module never downloads or installs a framework.
include_guard(GLOBAL)
if(NOT APPLE)
    return()
endif()
set(EISKALT_SPARKLE_ROOT "" CACHE PATH "Extracted, checksum-verified pinned Sparkle distribution")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../macos/updater/sparkle.lock.json" _sparkle_lock)
string(JSON _sparkle_version GET "${_sparkle_lock}" version)
set(_sparkle_framework "${EISKALT_SPARKLE_ROOT}/Sparkle.framework")
if(NOT EXISTS "${_sparkle_framework}/Headers/Sparkle.h")
    message(FATAL_ERROR "Provide EISKALT_SPARKLE_ROOT from the verified Sparkle archive")
endif()
execute_process(COMMAND /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString"
    "${_sparkle_framework}/Resources/Info.plist"
    OUTPUT_VARIABLE _sparkle_actual OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _sparkle_result ERROR_QUIET)
if(NOT _sparkle_result EQUAL 0 OR NOT _sparkle_actual STREQUAL _sparkle_version)
    message(FATAL_ERROR "Sparkle version must match the pinned ${_sparkle_version}")
endif()
add_library(Sparkle::Sparkle SHARED IMPORTED GLOBAL)
set_target_properties(Sparkle::Sparkle PROPERTIES
    FRAMEWORK TRUE
    IMPORTED_LOCATION "${_sparkle_framework}/Sparkle"
    INTERFACE_INCLUDE_DIRECTORIES "${_sparkle_framework}/Headers")
