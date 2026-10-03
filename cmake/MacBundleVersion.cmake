# Display versions are not update-ordering identities. Development builds use
# 1; release presets must supply an explicitly allocated number for both CPUs.
set(EISKALT_MACOS_BUILD_NUMBER "1" CACHE STRING "Allocated numeric macOS build; 1 is development-only")
if(NOT EISKALT_MACOS_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "EISKALT_MACOS_BUILD_NUMBER must be a canonical positive decimal build number")
endif()
string(LENGTH "${EISKALT_MACOS_BUILD_NUMBER}" _eiskalt_build_length)
if(_eiskalt_build_length GREATER 19 OR
   (_eiskalt_build_length EQUAL 19 AND EISKALT_MACOS_BUILD_NUMBER STRGREATER "9223372036854775807"))
    message(FATAL_ERROR "EISKALT_MACOS_BUILD_NUMBER exceeds the signed-feed numeric range")
endif()
set(MACOSX_BUNDLE_BUNDLE_VERSION "${EISKALT_MACOS_BUILD_NUMBER}")
