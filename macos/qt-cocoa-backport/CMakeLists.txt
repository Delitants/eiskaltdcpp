cmake_minimum_required(VERSION 3.21)
project(EiskaltCocoaBackport LANGUAGES CXX OBJCXX)
if(NOT APPLE OR NOT EXISTS "${QTBASE_SOURCE}/src/plugins/platforms/cocoa/main.mm")
    message(FATAL_ERROR "Requires macOS and -DQTBASE_SOURCE=/path/to/patched/qtbase-6.11.1")
endif()
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui CorePrivate GuiPrivate)
set(CMAKE_AUTOMOC ON)
set(cocoa "${QTBASE_SOURCE}/src/plugins/platforms/cocoa")
# Keep the original plugin's sources; qnsview includes its fragments itself.
file(GLOB sources CONFIGURE_DEPENDS "${cocoa}/*.mm" "${cocoa}/*.h")
list(FILTER sources EXCLUDE REGEX "/qnsview_[^/]+\\.mm$")
if(NOT QT_FEATURE_opengl)
    list(FILTER sources EXCLUDE REGEX "/qcocoaglcontext\\.(h|mm)$")
endif()
if(NOT QT_FEATURE_vulkan)
    list(FILTER sources EXCLUDE REGEX "/qcocoavulkaninstance\\.(h|mm)$")
endif()
if(NOT QT_FEATURE_accessibility)
    list(FILTER sources EXCLUDE REGEX "/qcocoaaccessibility(element)?\\.(h|mm)$")
endif()
if(QT_FEATURE_sessionmanager)
    list(APPEND sources "${cocoa}/qcocoasessionmanager.cpp")
endif()
add_library(qcocoa MODULE ${sources})
set_target_properties(qcocoa PROPERTIES
    CXX_STANDARD 17 OBJCXX_STANDARD 17
    CXX_VISIBILITY_PRESET hidden OBJCXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    SUFFIX ".dylib"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path/../../Frameworks"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/platforms")
target_compile_definitions(qcocoa PRIVATE QT_PLUGIN QT_NO_FOREACH)
target_include_directories(qcocoa PRIVATE "${cocoa}")
target_link_libraries(qcocoa PRIVATE Qt6::CorePrivate Qt6::GuiPrivate)
foreach(framework Foundation AppKit Carbon CoreServices CoreVideo IOKit IOSurface Metal QuartzCore UniformTypeIdentifiers)
    target_link_libraries(qcocoa PRIVATE "-framework ${framework}")
endforeach()
qt_add_resources(qcocoa qcocoaresources
    PREFIX "/qt-project.org/mac/cursors" BASE "${cocoa}"
    FILES "${cocoa}/images/sizeallcursor.png" "${cocoa}/images/spincursor.png" "${cocoa}/images/waitcursor.png")
foreach(module Core Gui)
    add_custom_command(TARGET qcocoa POST_BUILD
        COMMAND /usr/bin/install_name_tool -change "$<TARGET_FILE:Qt6::${module}>"
            "@rpath/Qt${module}.framework/Versions/A/Qt${module}" "$<TARGET_FILE:qcocoa>"
        VERBATIM)
endforeach()
add_custom_command(TARGET qcocoa POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:qcocoa>" VERBATIM)
