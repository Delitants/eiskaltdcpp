# A metadata change must relink the app and rerun its post-build signing, also
# with Xcode, which does not support the LINK_DEPENDS target property.
get_target_property(_bundle_plist_template ${PROJECT_NAME} MACOSX_BUNDLE_INFO_PLIST)
configure_file("${_bundle_plist_template}" "${PROJECT_BINARY_DIR}/MacBundleMetadata.plist")
file(SHA256 "${PROJECT_BINARY_DIR}/MacBundleMetadata.plist" EISKALT_MAC_BUNDLE_METADATA_HASH)
if(EISKALT_MAC_BUNDLE_PACKAGING_HASH)
    string(SHA256 EISKALT_MAC_BUNDLE_METADATA_HASH
        "${EISKALT_MAC_BUNDLE_METADATA_HASH}|${EISKALT_MAC_BUNDLE_PACKAGING_HASH}")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/MacBundleMetadata.cpp.in"
               "${PROJECT_BINARY_DIR}/MacBundleMetadata.cpp" @ONLY)

# Make may miss changed contents at equal timestamps. A new source identity on
# each metadata change also handles returning to an earlier hash; no-op builds
# keep the same identity. The fixed filename above remains the audit receipt.
if(NOT DEFINED EISKALT_MAC_BUNDLE_METADATA_GENERATION)
    set(EISKALT_MAC_BUNDLE_METADATA_GENERATION 0 CACHE INTERNAL "Bundle metadata source generation")
endif()
if(NOT EISKALT_MAC_BUNDLE_METADATA_PREVIOUS_HASH STREQUAL EISKALT_MAC_BUNDLE_METADATA_HASH)
    math(EXPR _bundle_metadata_generation "${EISKALT_MAC_BUNDLE_METADATA_GENERATION} + 1")
    set(EISKALT_MAC_BUNDLE_METADATA_GENERATION "${_bundle_metadata_generation}" CACHE INTERNAL "Bundle metadata source generation" FORCE)
    set(EISKALT_MAC_BUNDLE_METADATA_PREVIOUS_HASH "${EISKALT_MAC_BUNDLE_METADATA_HASH}" CACHE INTERNAL "Last configured bundle metadata hash" FORCE)
endif()
set(_bundle_metadata_source "${PROJECT_BINARY_DIR}/MacBundleMetadata-${EISKALT_MAC_BUNDLE_METADATA_GENERATION}.cpp")
configure_file("${CMAKE_CURRENT_LIST_DIR}/MacBundleMetadata.cpp.in" "${_bundle_metadata_source}" @ONLY)
target_sources(${PROJECT_NAME} PRIVATE "${_bundle_metadata_source}")

# A fresh object can still have the old executable's coarse timestamp. Run an
# ordered content-generation gate so Make cannot skip linking/signing that case.
set(_bundle_link_receipt "${PROJECT_BINARY_DIR}/MacBundleMetadata.linked-generation")
# File components avoid TARGET_FILE's automatic reverse dependency (a cycle).
# Release metadata requires modern CMake; preserve the legacy path on old hosts.
if(POLICY CMP0112)
    cmake_policy(SET CMP0112 NEW)
    add_custom_target(${PROJECT_NAME}-bundle-metadata-gate
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
            "-DEXECUTABLE=$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
            "-DRECEIPT=${_bundle_link_receipt}"
            "-DGENERATION=${EISKALT_MAC_BUNDLE_METADATA_GENERATION}"
            -P "${CMAKE_CURRENT_LIST_DIR}/MacBundleMetadataGate.cmake"
        VERBATIM)
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}-bundle-metadata-gate)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DBUILD_ROOT=${PROJECT_BINARY_DIR}"
            "-DEXECUTABLE=$<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
            "-DRECEIPT=${_bundle_link_receipt}"
            "-DGENERATION=${EISKALT_MAC_BUNDLE_METADATA_GENERATION}"
            -DRECORD=TRUE -P "${CMAKE_CURRENT_LIST_DIR}/MacBundleMetadataGate.cmake"
        VERBATIM)
endif()
