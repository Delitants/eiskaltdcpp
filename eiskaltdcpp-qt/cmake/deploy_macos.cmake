if(DEFINED TORRENT_LIBRARY)
    if(NOT EXISTS "${TORRENT_LIBRARY}" OR
       NOT TORRENT_SONAME MATCHES "^[A-Za-z0-9_.+-]+\\.dylib$")
        message(FATAL_ERROR "Invalid linked Torrent provider or install-name filename")
    endif()
    # Search-directory order is not sufficient when another provider shares its SONAME.
    file(MAKE_DIRECTORY "${APP_BUNDLE}/Contents/Frameworks")
    set(staged_torrent "${APP_BUNDLE}/Contents/Frameworks/${TORRENT_SONAME}")
    if(IS_SYMLINK "${staged_torrent}" OR IS_DIRECTORY "${staged_torrent}")
        message(FATAL_ERROR "Linked Torrent staging destination must be a regular file")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${TORRENT_LIBRARY}" "${staged_torrent}"
        RESULT_VARIABLE copy_result)
    if(NOT "${copy_result}" STREQUAL "0")
        message(FATAL_ERROR "Could not stage the linked Torrent provider (${copy_result})")
    endif()
    # Package-manager libraries can be read-only. Qt edits only this owned copy.
    execute_process(COMMAND /bin/chmod u+w "${staged_torrent}" RESULT_VARIABLE writable_result)
    if(NOT "${writable_result}" STREQUAL "0")
        message(FATAL_ERROR "Could not make the staged Torrent provider writable")
    endif()
endif()

set(args "${APP_BUNDLE}" -no-codesign)
foreach(path IN LISTS LIBRARY_PATHS)
    if(IS_DIRECTORY "${path}")
        list(APPEND args "-libpath=${path}")
    endif()
endforeach()
execute_process(COMMAND "${MACDEPLOYQT}" ${args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT "${result}" STREQUAL "0" OR "${output}\n${errors}" MATCHES "(^|\n)ERROR:")
    message(FATAL_ERROR "macdeployqt failed (${result}):\n${output}\n${errors}")
endif()
message(STATUS "${output}${errors}")
