project(eiskaltdcpp-qt-tests)

# --- Catch2 (already fetched in parent if BUILD_TESTS is on) ---
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# --- Qt6 ---
find_package(Qt6 6.2.0 COMPONENTS Core Gui Widgets Svg Test Network REQUIRED)
include(${CMAKE_SOURCE_DIR}/cmake/AppIconThemes.cmake)

# Enable automoc for Q_OBJECT classes
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# --- Qt source files under test (compiled into the test binary) ---
set(QT_SRC_DIR ${CMAKE_SOURCE_DIR}/eiskaltdcpp-qt/src)

set(QT_MODEL_SOURCES
    ${QT_SRC_DIR}/QtContext.cpp
    ${QT_SRC_DIR}/FavoriteHubModel.cpp
    ${QT_SRC_DIR}/ADLSModel.cpp
    ${QT_SRC_DIR}/IPFilterModel.cpp
    ${QT_SRC_DIR}/SpyModel.cpp
    ${QT_SRC_DIR}/FlowLayout.cpp
    ${QT_SRC_DIR}/WulforSettings.cpp
    ${QT_SRC_DIR}/AppIconTheme.cpp
    ${QT_SRC_DIR}/ActionToolBar.cpp
    ${QT_SRC_DIR}/ActionToolBar.h
    ${QT_SRC_DIR}/TabButton.cpp
    ${QT_SRC_DIR}/TabButton.h
    ${QT_SRC_DIR}/TabNavigation.cpp
    ${QT_SRC_DIR}/TabNavigation.h
    ${QT_SRC_DIR}/ArenaWidget.cpp
    ${QT_SRC_DIR}/ArenaWidget.h
    ${QT_SRC_DIR}/ChatFormatBar.cpp
    ${QT_SRC_DIR}/ChatFormatBar.h
    ${QT_SRC_DIR}/SearchBlacklist.cpp
    ${QT_SRC_DIR}/Antispam.cpp
)

# --- Test sources ---
add_executable(eiskaltdcpp-qt-tests
    test_qt_main.cpp
    test_favoritehubmodel.cpp
    test_adlsmodel.cpp
    test_ipfiltermodel.cpp
    test_hubuserlistpainting.cpp
    test_spymodel.cpp
    test_flowlayout.cpp
    test_wulforsettings.cpp
    test_appicontheme.cpp
    test_actiontoolbar.cpp
    test_chatformatbar.cpp
    test_tabnavigation.cpp
    test_searchblacklist.cpp
    test_antispam.cpp
    test_downloadtosettings.cpp
    test_settingsconnectionhelpers.cpp
    test_proxytestrunner.cpp
    ${QT_SRC_DIR}/ProxyTestRunner.cpp
    ${QT_SRC_DIR}/ProxyTestRunner.h
    test_socket_shadowsocks.cpp
    test_shadowsocks2022.cpp
    test_xchacha20poly1305.cpp
    test_dht_bootstrap.cpp
    test_client_reconnect_policy.cpp
    test_logmanager_live.cpp
    test_livelogmodel.cpp
    test_applicationstatus.cpp
    test_menuiconhelper.cpp
    test_diagnosticsignalpolicy.cpp
    test_fileopenrequests.cpp
    test_macguiconfigcompatibility.cpp
    test_viewlayout.cpp
    test_fileextensionicons.cpp
    test_filemetadata.cpp
    test_dockcursor.cpp
    test_autofitcolumns.cpp
    ${QT_SRC_DIR}/AutoFitColumns.cpp
    ${QT_SRC_DIR}/AutoFitColumns.h
    stubs_wulforutil.cpp
    ${QT_SRC_DIR}/MenuIconHelper.cpp
    ${QT_SRC_DIR}/LiveLogModel.cpp
    ${QT_SRC_DIR}/DownloadToSettings.cpp
    ${QT_SRC_DIR}/SettingsConnectionHelpers.cpp
    ${QT_MODEL_SOURCES}
)

target_include_directories(eiskaltdcpp-qt-tests PRIVATE
    ${QT_SRC_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/dcpp
    ${OPENSSL_INCLUDE_DIR}
    ${GETTEXT_INCLUDE_DIR}
)
target_compile_definitions(eiskaltdcpp-qt-tests PRIVATE QT_CONTEXT_MINIMAL)
eiskalt_add_icon_themes(eiskaltdcpp-qt-tests)
add_executable(eiskalt-icon-theme-preview EXCLUDE_FROM_ALL
    icon_theme_preview.cpp ${QT_SRC_DIR}/AppIconTheme.cpp)
target_include_directories(eiskalt-icon-theme-preview PRIVATE ${QT_SRC_DIR})
target_link_libraries(eiskalt-icon-theme-preview PRIVATE Qt6::Widgets Qt6::Svg)
eiskalt_add_icon_themes(eiskalt-icon-theme-preview)
add_executable(eiskalt-toolbar-preview EXCLUDE_FROM_ALL
    toolbar_preview.cpp ${QT_SRC_DIR}/ActionToolBar.cpp ${QT_SRC_DIR}/ActionToolBar.h
    ${QT_SRC_DIR}/AppIconTheme.cpp)
target_include_directories(eiskalt-toolbar-preview PRIVATE ${QT_SRC_DIR})
target_link_libraries(eiskalt-toolbar-preview PRIVATE Qt6::Widgets Qt6::Svg)
eiskalt_add_icon_themes(eiskalt-toolbar-preview)
find_package(Qt6 QUIET COMPONENTS UiTools)
if (Qt6UiTools_FOUND)
    add_executable(eiskalt-chat-format-preview EXCLUDE_FROM_ALL
        chatformat_preview.cpp ${QT_SRC_DIR}/ChatFormatBar.cpp ${QT_SRC_DIR}/ChatFormatBar.h)
    target_include_directories(eiskalt-chat-format-preview PRIVATE ${QT_SRC_DIR})
    target_link_libraries(eiskalt-chat-format-preview PRIVATE Qt6::Widgets Qt6::Test Qt6::UiTools)
endif ()
if (USE_TORRENT)
    qt6_add_resources(eiskaltdcpp-qt-tests torrent_icons
        PREFIX "/torrent"
        BASE "${CMAKE_SOURCE_DIR}/eiskaltdcpp-qt/icons/torrent"
        FILES "${CMAKE_SOURCE_DIR}/eiskaltdcpp-qt/icons/torrent/torrent.png")
    target_sources(eiskaltdcpp-qt-tests PRIVATE
        test_torrenttoolbar.cpp
        test_torrentwindow.cpp
        test_torrentsharing.cpp
        test_torrentcreatedialog.cpp
        test_unifiedtransfers.cpp
        test_unifiedtransfers_swarm.cpp
        test_torrentproxyadapter.cpp
        test_gostinterop.cpp
        test_torrentsharepublisher.cpp
        test_torrentruntime.cpp
        test_settingstorrent.cpp
        ${QT_SRC_DIR}/TorrentProxyAdapter.cpp
        ${QT_SRC_DIR}/TorrentProxyAdapter.h
        ${QT_SRC_DIR}/GostUdpRelay.cpp
        ${QT_SRC_DIR}/GostUdpRelay.h
        ${QT_SRC_DIR}/TorrentSharePublisher.cpp
        ${QT_SRC_DIR}/TorrentSharePublisher.h
        ${QT_SRC_DIR}/TorrentRuntime.cpp
        ${QT_SRC_DIR}/TorrentRuntime.h
        ${QT_SRC_DIR}/SettingsTorrent.cpp
        ${QT_SRC_DIR}/SettingsTorrent.h
        ${QT_SRC_DIR}/TorrentWindow.cpp
        ${QT_SRC_DIR}/TorrentWindow.h
        ${QT_SRC_DIR}/TorrentCreateDialog.cpp
        ${QT_SRC_DIR}/TorrentCreateDialog.h
        ${QT_SRC_DIR}/TransferViewModel.cpp
        ${QT_SRC_DIR}/TransferViewModel.h)
    target_link_libraries(eiskaltdcpp-qt-tests PRIVATE Qt6::Network eiskalt-torrent)
    target_compile_definitions(eiskaltdcpp-qt-tests PRIVATE
        GOST_FIXTURE_SCRIPT="${CMAKE_SOURCE_DIR}/tests/proxy/gost-fixture.sh")
    # Exercise the same status-bar painter enabled in the application build.
    set_source_files_properties(${QT_SRC_DIR}/TransferViewModel.cpp PROPERTIES
        COMPILE_DEFINITIONS USE_PROGRESS_BARS)
    add_executable(eiskalt-torrent-ui-preview EXCLUDE_FROM_ALL
        torrent_ui_preview.cpp
        ${QT_SRC_DIR}/ProxyTestRunner.cpp ${QT_SRC_DIR}/ProxyTestRunner.h
        ${QT_SRC_DIR}/AppIconTheme.cpp
        ${QT_SRC_DIR}/ArenaWidget.cpp ${QT_SRC_DIR}/ArenaWidget.h
        ${QT_SRC_DIR}/SettingsTorrent.cpp ${QT_SRC_DIR}/SettingsTorrent.h
        ${QT_SRC_DIR}/TorrentWindow.cpp ${QT_SRC_DIR}/TorrentWindow.h
        ${QT_SRC_DIR}/TorrentCreateDialog.cpp ${QT_SRC_DIR}/TorrentCreateDialog.h)
    target_include_directories(eiskalt-torrent-ui-preview PRIVATE ${QT_SRC_DIR} ${CMAKE_SOURCE_DIR}
        ${OPENSSL_INCLUDE_DIR} ${GETTEXT_INCLUDE_DIR})
    if(LUA_SCRIPT)
        target_include_directories(eiskalt-torrent-ui-preview PRIVATE ${LUA_INCLUDE_DIR})
    endif()
    target_compile_definitions(eiskalt-torrent-ui-preview PRIVATE QT_CONTEXT_MINIMAL)
    target_link_libraries(eiskalt-torrent-ui-preview PRIVATE Qt6::Widgets Qt6::Svg Qt6::Network eiskalt-torrent dcpp)
    eiskalt_add_icon_themes(eiskalt-torrent-ui-preview)
endif()
if (LUA_SCRIPT)
    target_include_directories(eiskaltdcpp-qt-tests PRIVATE ${LUA_INCLUDE_DIR})
endif()

target_link_libraries(eiskaltdcpp-qt-tests PRIVATE dcpp Catch2::Catch2 Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Test Qt6::Network )

# Auto-discover test cases.
# - PRE_TEST defers discovery to ctest time (avoids build-time env issues)
# - ENVIRONMENT sets offscreen rendering for headless CI runners
catch_discover_tests(eiskaltdcpp-qt-tests
    DISCOVERY_MODE PRE_TEST
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    # Catch's all-skipped exit code can equal a real failure count. Match only
    # these explicit opt-in/child notices, never a numeric exit code.
    PROPERTIES SKIP_REGULAR_EXPRESSION
        "Opt-in: set verified GOST_BIN|Internal fixture child; use the public"
)
