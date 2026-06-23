cmake_minimum_required (VERSION 3.10)
project (extra)
include_directories (${PROJECT_SOURCE_DIR}
                     ${GETTEXT_INCLUDE_DIR}
                     ${MINIUPNP_INCLUDE_DIR}
                     ${LUA_INCLUDE_DIR})
aux_source_directory (${PROJECT_SOURCE_DIR} extra_SRCS)

set (BLAKE3_SRCS
    ${PROJECT_SOURCE_DIR}/blake3/blake3.c
    ${PROJECT_SOURCE_DIR}/blake3/blake3_dispatch.c
    ${PROJECT_SOURCE_DIR}/blake3/blake3_portable.c)
list (APPEND extra_SRCS ${BLAKE3_SRCS})
set_property (SOURCE ${BLAKE3_SRCS} APPEND PROPERTY COMPILE_DEFINITIONS
    BLAKE3_NO_SSE2
    BLAKE3_NO_SSE41
    BLAKE3_NO_AVX2
    BLAKE3_NO_AVX512
    BLAKE3_NO_NEON
    BLAKE3_USE_NEON=0)
if (FREE_SPACE_BAR_C)
    if (WIN32)
        list (REMOVE_ITEM extra_SRCS ${PROJECT_SOURCE_DIR}/fsusage.c)
    endif (WIN32)
else (FREE_SPACE_BAR_C)
    list (REMOVE_ITEM extra_SRCS ${PROJECT_SOURCE_DIR}/fsusage.c)
    list (REMOVE_ITEM extra_SRCS ${PROJECT_SOURCE_DIR}/freespace.cpp)
endif (FREE_SPACE_BAR_C)

if (NOT USE_MINIUPNP)
    list (REMOVE_ITEM extra_SRCS ${PROJECT_SOURCE_DIR}/upnpc.cpp)
endif (NOT USE_MINIUPNP)

add_library (extra STATIC ${extra_SRCS})
target_include_directories (extra PUBLIC ${PROJECT_SOURCE_DIR}/blake3)

if (USE_MINIUPNP AND WIN32)
    set (WIN32_LIBS "ws2_32" "iphlpapi")
endif (USE_MINIUPNP AND WIN32)

if (DEBUG_IPFILTER)
    add_definitions (-D_DEBUG_IPFILTER)
    message (STATUS "Enabled debug output from filter of IP addresses!")
endif (DEBUG_IPFILTER)

if (WIN32 OR CYGWIN)
    set_property(TARGET extra PROPERTY COMPILE_FLAGS)
else ()
    set_property(TARGET extra PROPERTY COMPILE_FLAGS -fPIC)
endif ()

target_link_libraries (extra ${MINIUPNP_LIBRARY} ${WIN32_LIBS} ${GETTEXT_LIBRARIES} ${ICONV_LIBRARIES})
