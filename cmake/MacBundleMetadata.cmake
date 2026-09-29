# A metadata change must relink the app and rerun its post-build signing, also
# with Xcode, which does not support the LINK_DEPENDS target property.
get_target_property(_bundle_plist_template ${PROJECT_NAME} MACOSX_BUNDLE_INFO_PLIST)
configure_file("${_bundle_plist_template}" "${PROJECT_BINARY_DIR}/MacBundleMetadata.plist")
file(SHA256 "${PROJECT_BINARY_DIR}/MacBundleMetadata.plist" EISKALT_MAC_BUNDLE_METADATA_HASH)
configure_file("${CMAKE_CURRENT_LIST_DIR}/MacBundleMetadata.cpp.in"
               "${PROJECT_BINARY_DIR}/MacBundleMetadata.cpp" @ONLY)
target_sources(${PROJECT_NAME} PRIVATE "${PROJECT_BINARY_DIR}/MacBundleMetadata.cpp")
