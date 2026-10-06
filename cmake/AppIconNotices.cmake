# Run from the bundle post-build step; provenance must travel with the artwork.
if(NOT DEFINED EISKALT_APP_ICON_SOURCE_DIR OR NOT DEFINED EISKALT_APP_ICON_NOTICE_DIR)
    message(FATAL_ERROR "Provide the app icon source and notice destination directories")
endif()
file(MAKE_DIRECTORY "${EISKALT_APP_ICON_NOTICE_DIR}")
foreach(name READ_ME.txt modern-classic-manifest.json)
    configure_file("${EISKALT_APP_ICON_SOURCE_DIR}/${name}"
                   "${EISKALT_APP_ICON_NOTICE_DIR}/${name}" COPYONLY)
endforeach()
