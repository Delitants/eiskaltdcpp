# Build/selection only. Scheduler and installation capability remain gated by
# routed-network and lifecycle acceptance, not the presence of Sparkle on disk.
include_guard(GLOBAL)
option(EISKALT_MAC_UPDATER_NATIVE "Compile the gated native updater (no scheduler or install capability)" OFF)
if(EISKALT_MAC_UPDATER_NATIVE)
    if(NOT APPLE OR NOT EISKALT_MAC_UPDATER_METADATA OR NOT EISKALT_MAC_UPDATER_PACKAGE)
        message(FATAL_ERROR "EISKALT_MAC_UPDATER_NATIVE requires macOS, validated updater metadata and audited packaging")
    endif()
    enable_language(OBJCXX)
    include("${CMAKE_CURRENT_LIST_DIR}/MacUpdater.cmake")
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
