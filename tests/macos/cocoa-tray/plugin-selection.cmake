get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EISKALT_QCOCOA_PLUGIN original)
set(EISKALT_QCOCOA_PLUGIN_OVERRIDE "")
include("${root}/cmake/MacCocoaPluginOverride.cmake")
if(NOT EISKALT_QCOCOA_PLUGIN STREQUAL "original")
    message(FATAL_ERROR "No override should leave the discovered plugin alone")
endif()
set(Qt6Core_VERSION 6.11.1)
if(NOT PLUGIN OR NOT SHA)
    message(FATAL_ERROR "Pass -DPLUGIN=/path/to/plugin -DSHA=verified_sha256")
endif()
set(EISKALT_QCOCOA_PLUGIN_OVERRIDE "${PLUGIN}")
set(EISKALT_QCOCOA_PLUGIN_SHA256 "${SHA}")
set(CMAKE_OSX_ARCHITECTURES arm64)
include("${root}/cmake/MacCocoaPluginOverride.cmake")
if(NOT EISKALT_QCOCOA_PLUGIN STREQUAL EISKALT_QCOCOA_PLUGIN_OVERRIDE)
    message(FATAL_ERROR "Explicit override must win over discovered plugin")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    -DQt6Core_VERSION=6.11.1 "-DEISKALT_QCOCOA_PLUGIN_OVERRIDE=${PLUGIN}"
    -DEISKALT_QCOCOA_PLUGIN_SHA256=wrong
    -P "${root}/cmake/MacCocoaPluginOverride.cmake"
    RESULT_VARIABLE wrongHash ERROR_VARIABLE hashError OUTPUT_QUIET)
if(wrongHash EQUAL 0 OR NOT hashError MATCHES "SHA256 mismatch")
    message(FATAL_ERROR "Wrong artifact hash was not rejected")
endif()
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" notBinaryHash)
execute_process(COMMAND "${CMAKE_COMMAND}"
    -DQt6Core_VERSION=6.11.1 "-DEISKALT_QCOCOA_PLUGIN_OVERRIDE=${CMAKE_CURRENT_LIST_FILE}"
    "-DEISKALT_QCOCOA_PLUGIN_SHA256=${notBinaryHash}" -DCMAKE_OSX_ARCHITECTURES=arm64
    -P "${root}/cmake/MacCocoaPluginOverride.cmake"
    RESULT_VARIABLE notBinary ERROR_VARIABLE binaryError OUTPUT_QUIET)
if(notBinary EQUAL 0 OR NOT binaryError MATCHES "does not contain architecture")
    message(FATAL_ERROR "Non-binary artifact was not rejected")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    -DQt6Core_VERSION=6.12.0 "-DEISKALT_QCOCOA_PLUGIN_OVERRIDE=${CMAKE_CURRENT_LIST_FILE}"
    -P "${root}/cmake/MacCocoaPluginOverride.cmake"
    RESULT_VARIABLE wrongVersion ERROR_VARIABLE versionError OUTPUT_QUIET)
if(wrongVersion EQUAL 0 OR NOT versionError MATCHES "requires Qt 6.11.1")
    message(FATAL_ERROR "Mismatched Qt version was not rejected")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    -DQt6Core_VERSION=6.11.1 "-DEISKALT_QCOCOA_PLUGIN_OVERRIDE=${CMAKE_CURRENT_LIST_FILE}/missing"
    -P "${root}/cmake/MacCocoaPluginOverride.cmake"
    RESULT_VARIABLE missingPath ERROR_VARIABLE pathError OUTPUT_QUIET)
if(missingPath EQUAL 0 OR NOT pathError MATCHES "plugin is missing")
    message(FATAL_ERROR "Missing plugin was not rejected")
endif()
message(STATUS "Plugin override selection passed")
