if(DEFINED ENV{HOMEBREW})
    set(HOMEBREW "$ENV{HOMEBREW}")
elseif(EXISTS "/opt/homebrew")
    set(HOMEBREW "/opt/homebrew")
else()
    set(HOMEBREW "/usr/local")
endif()

if(DEFINED ENV{DEPS_PREFIX})
    set(DEPS_PREFIX "$ENV{DEPS_PREFIX}")
else()
    set(DEPS_PREFIX "${HOMEBREW}")
endif()

set(EISKALT_MACOS_DEPS_PREFIX "${DEPS_PREFIX}"
    CACHE PATH "Prefix that provides vendored macOS build dependencies")

if(DEFINED ENV{OSX_ARCHITECTURES})
    set(OSX_ARCHITECTURES "$ENV{OSX_ARCHITECTURES}")
elseif(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(OSX_ARCHITECTURES "arm64")
else()
    set(OSX_ARCHITECTURES "x86_64")
endif()

if(DEFINED ENV{OSX_DEPLOYMENT_TARGET})
    set(OSX_DEPLOYMENT_TARGET "$ENV{OSX_DEPLOYMENT_TARGET}")
elseif(OSX_ARCHITECTURES STREQUAL "arm64")
    set(OSX_DEPLOYMENT_TARGET "14.0")
else()
    set(OSX_DEPLOYMENT_TARGET "10.15")
endif()

foreach(_prefix "${EISKALT_MACOS_DEPS_PREFIX}" "${HOMEBREW}")
    if(NOT _prefix)
        continue()
    endif()
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/lib/cmake")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/gettext")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/openssl@1.1")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/openssl")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/qtbase")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/qt@5")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/qt5")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};${_prefix}/opt/qt")
endforeach()

set(CMAKE_C_COMPILER   "clang")
set(CMAKE_CXX_COMPILER "clang++")

set(CMAKE_OSX_ARCHITECTURES "${OSX_ARCHITECTURES}"
    CACHE STRING "CMAKE_OSX_ARCHITECTURES")
set(CMAKE_OSX_DEPLOYMENT_TARGET "${OSX_DEPLOYMENT_TARGET}"
    CACHE STRING "CMAKE_OSX_DEPLOYMENT_TARGET")

set(CMAKE_INSTALL_PREFIX "${EISKALT_MACOS_DEPS_PREFIX}" CACHE PATH "Installation Prefix")
#set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug|Release|RelWithDebInfo|MinSizeRel")
