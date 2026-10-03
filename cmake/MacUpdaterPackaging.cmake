# Packaging is not activation: production factory/main/normal-quit remain gated.
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0126)
    cmake_policy(SET CMP0126 NEW)
endif()
option(EISKALT_MAC_UPDATER_PACKAGE "Package audited updater dependencies (does not enable updates)" OFF)
set(EISKALT_MAC_BUNDLE_PACKAGING_HASH "")
if(NOT EISKALT_MAC_UPDATER_PACKAGE)
    if(EISKALT_MAC_UPDATER_PACKAGED_BUILD)
        message(FATAL_ERROR "Disabling updater packaging requires a fresh build tree; preserve the existing signed bundle")
    endif()
    return()
endif()
if(NOT APPLE OR NOT EISKALT_MAC_UPDATER_METADATA OR CMAKE_VERSION VERSION_LESS "3.23")
    message(FATAL_ERROR "EISKALT_MAC_UPDATER_PACKAGE requires macOS, CMake 3.23+ and validated EISKALT_MAC_UPDATER_METADATA")
endif()
if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    set(_updater_profile intel)
elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(_updater_profile apple-silicon)
else()
    message(FATAL_ERROR "Updater packaging requires one release CPU")
endif()
set(EISKALT_MAC_UPDATER_STAGE "" CACHE PATH "Audited flavor-specific local updater stage")
set(EISKALT_MAC_UPDATER_STAGE_SHA256 "" CACHE STRING "Explicit canonical digest of complete audited stage tree")
set(EISKALT_MAC_UPDATER_CODESIGN_IDENTITY "-" CACHE STRING "Existing final bundle codesign identity; default ad-hoc")
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(_updater_package_tool "${CMAKE_CURRENT_LIST_DIR}/../macos/updater/package-framework.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${_updater_package_tool}"
    --audit-stage --stage "${EISKALT_MAC_UPDATER_STAGE}"
    --stage-sha256 "${EISKALT_MAC_UPDATER_STAGE_SHA256}" --profile "${_updater_profile}"
    RESULT_VARIABLE _updater_stage_result OUTPUT_QUIET ERROR_VARIABLE _updater_stage_error)
if(NOT _updater_stage_result EQUAL 0)
    message(FATAL_ERROR "Audited updater stage rejected: ${_updater_stage_error}")
endif()
if(EISKALT_MAC_UPDATER_CODESIGN_IDENTITY STREQUAL "" OR EISKALT_MAC_UPDATER_CODESIGN_IDENTITY MATCHES "^--")
    message(FATAL_ERROR "Explicit existing bundle codesign identity required")
endif()
set(EISKALT_MAC_UPDATER_PACKAGED_BUILD ON CACHE INTERNAL "This build tree owns packaged updater content" FORCE)
# Bind recipe and stage changes into the existing timestamp-independent relink gate.
file(SHA256 "${_updater_package_tool}" _updater_recipe_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _updater_cmake_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../macos/updater/sparkle.lock.json" _updater_lock_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../macos/release.py" _updater_audit_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../macos/updater/release_feeds.py" _updater_input_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/../macos/check-bundle-external-links.sh" _updater_links_hash)
# Caller-controlled paths/identity can contain separators. Hash fields separately
# so different tuples cannot alias without an actual SHA256 collision.
string(SHA256 _updater_stage_path_hash "${EISKALT_MAC_UPDATER_STAGE}")
string(SHA256 _updater_identity_hash "${EISKALT_MAC_UPDATER_CODESIGN_IDENTITY}")
string(SHA256 EISKALT_MAC_BUNDLE_PACKAGING_HASH
    "${EISKALT_MAC_UPDATER_STAGE_SHA256}|${_updater_stage_path_hash}|${_updater_identity_hash}|${_updater_recipe_hash}|${_updater_cmake_hash}|${_updater_lock_hash}|${_updater_audit_hash}|${_updater_input_hash}|${_updater_links_hash}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_updater_package_tool}" "${CMAKE_CURRENT_LIST_DIR}/../macos/updater/sparkle.lock.json"
    "${CMAKE_CURRENT_LIST_DIR}/../macos/release.py" "${CMAKE_CURRENT_LIST_DIR}/../macos/updater/release_feeds.py"
    "${CMAKE_CURRENT_LIST_DIR}/../macos/check-bundle-external-links.sh")

function(eiskalt_package_updater target)
    # This function must be called after all deployment/resource/normalization commands.
    add_custom_target(${target}-updater-stage-audit
        COMMAND "${Python3_EXECUTABLE}" "${_updater_package_tool}"
            --audit-stage --stage "${EISKALT_MAC_UPDATER_STAGE}"
            --stage-sha256 "${EISKALT_MAC_UPDATER_STAGE_SHA256}" --profile "${_updater_profile}"
        VERBATIM)
    add_dependencies(${target} ${target}-updater-stage-audit)
    if(TARGET ${target}-bundle-metadata-gate)
        # Audit before the gate can invalidate the previous signed executable.
        add_dependencies(${target}-bundle-metadata-gate ${target}-updater-stage-audit)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" "${_updater_package_tool}"
            --stage "${EISKALT_MAC_UPDATER_STAGE}" --stage-sha256 "${EISKALT_MAC_UPDATER_STAGE_SHA256}"
            --profile "${_updater_profile}" --identity "${EISKALT_MAC_UPDATER_CODESIGN_IDENTITY}"
            --build-root "${PROJECT_BINARY_DIR}" --app "$<TARGET_BUNDLE_DIR:${target}>"
        VERBATIM)
endfunction()
