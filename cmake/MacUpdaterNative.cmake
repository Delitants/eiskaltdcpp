# Native selection and qualified-package installation are separate opt-ins.
# Neither option starts scheduling or substitutes for runtime/consent checks.
include_guard(GLOBAL)
option(EISKALT_MAC_UPDATER_NATIVE "Compile the gated native updater (does not start scheduling)" OFF)
option(EISKALT_MAC_UPDATER_INSTALLATION "Admit installation through the qualified production package factory" OFF)
if(EISKALT_MAC_UPDATER_INSTALLATION)
    if(NOT EISKALT_MAC_UPDATER_NATIVE OR NOT APPLE OR
       NOT EISKALT_MAC_UPDATER_METADATA OR NOT EISKALT_MAC_UPDATER_PACKAGE)
        message(FATAL_ERROR "EISKALT_MAC_UPDATER_INSTALLATION requires native selection, macOS, validated metadata and audited packaging")
    endif()
    string(LENGTH "${EISKALT_MAC_UPDATER_STAGE_SHA256}" _installation_stage_hash_length)
    string(LENGTH "${EISKALT_MAC_BUNDLE_PACKAGING_HASH}" _installation_package_hash_length)
    # The cache's packaged-build marker alone is not a fresh audit receipt.
    if(NOT EISKALT_MAC_UPDATER_PACKAGED_BUILD OR NOT DEFINED _updater_stage_result OR
       NOT "${_updater_stage_result}" STREQUAL "0" OR
       NOT IS_DIRECTORY "${EISKALT_MAC_UPDATER_STAGE}" OR
       NOT _installation_stage_hash_length EQUAL 64 OR
       NOT "${EISKALT_MAC_UPDATER_STAGE_SHA256}" MATCHES "^[0-9a-f]+$" OR
       NOT _installation_package_hash_length EQUAL 64 OR
       NOT "${EISKALT_MAC_BUNDLE_PACKAGING_HASH}" MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "EISKALT_MAC_UPDATER_INSTALLATION requires a fresh audited package with explicit stage pins")
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" AND
       NOT CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        message(FATAL_ERROR "EISKALT_MAC_UPDATER_INSTALLATION requires one release CPU")
    endif()
endif()
if(EISKALT_MAC_UPDATER_NATIVE)
    if(NOT APPLE OR NOT EISKALT_MAC_UPDATER_METADATA OR NOT EISKALT_MAC_UPDATER_PACKAGE)
        message(FATAL_ERROR "EISKALT_MAC_UPDATER_NATIVE requires macOS, validated updater metadata and audited packaging")
    endif()
    enable_language(OBJCXX)
    include("${CMAKE_CURRENT_LIST_DIR}/MacUpdater.cmake")
endif()

if(EISKALT_MAC_UPDATER_PACKAGE AND EISKALT_MAC_BUNDLE_PACKAGING_HASH)
    # Must precede MacBundleMetadata.cmake so admission changes invalidate its
    # content-generation/relink receipt, including an ON -> OFF transition.
    file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _installation_recipe_hash)
    string(SHA256 EISKALT_MAC_BUNDLE_PACKAGING_HASH
        "${EISKALT_MAC_BUNDLE_PACKAGING_HASH}|${_installation_recipe_hash}|${EISKALT_MAC_UPDATER_INSTALLATION}")
endif()

function(eiskalt_add_native_updater target)
    if(NOT EISKALT_MAC_UPDATER_NATIVE)
        return()
    endif()
    set(_qt "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../eiskaltdcpp-qt/src/updates")
    set(_archive "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../macos/updater/BundleArchiveVerifier.mm")
    target_sources(${target} PRIVATE
        "${_qt}/MacUpdateBackend.mm" "${_qt}/MacUpdateBackend.h"
        "${_qt}/MacUpdateDiscovery.mm" "${_qt}/MacUpdateDiscovery.h"
        "${_qt}/MacUpdateInstaller.mm" "${_qt}/MacUpdateInstaller.h"
        "${_archive}")
    set_source_files_properties("${_qt}/MacUpdateBackend.mm" "${_qt}/MacUpdateDiscovery.mm"
        "${_qt}/MacUpdateInstaller.mm" "${_archive}" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(${target} PRIVATE EISKALT_MAC_UPDATER_NATIVE)
    if(EISKALT_MAC_UPDATER_INSTALLATION)
        target_compile_definitions(${target} PRIVATE EISKALT_MAC_UPDATER_INSTALLATION)
    endif()
    target_link_libraries(${target} PRIVATE Sparkle::Sparkle "-framework AppKit")
    # The application uses explicit MOC, not AUTOMOC. Keep outputs separate for
    # each consumer so the full GUI fixture can coexist with the real app.
    set(_moc_dir "${CMAKE_CURRENT_BINARY_DIR}/native-updater-${target}")
    file(MAKE_DIRECTORY "${_moc_dir}")
    foreach(_class MacUpdateBackend MacUpdateDiscovery MacUpdateInstaller)
        set(_moc "${_moc_dir}/moc_${_class}.cpp")
        qt6_generate_moc("${_qt}/${_class}.h" "${_moc}" TARGET ${target})
        target_sources(${target} PRIVATE "${_moc}")
    endforeach()
endfunction()
