# Fork Delta Changelog

Comparison against original upstream EiskaltDC++.

- Base: origin/master @ 697db4b0 (697db4b03e3d9ffa48b3d4c74fd043dee7663266)
- Head: codex-2.5.2-release @ 0bbc5c13 (0bbc5c13c20fc00a23e852cc83e70eee5ec9acd6)
- Generated on: 2026-04-28
- Commits ahead of upstream: 252
- Files changed vs upstream: 568

## 2.5.4-v1 Release Notes

- Refactored for modern macOS on Apple Silicon ARM64, including macOS 14+ and macOS 26-era theme behavior.
- Removed the broken user-facing simple-share toggle and kept the simple sharing view enabled by default.
- Added GUI/Basic controls for close-button minimize behavior, status icon visibility, and colored versus monochrome status icon mode.
- Added runtime-generated monochrome status icon support and refresh handling during light/dark mode changes.
- Cleaned Preferences layout problems: History inner border, User Commands button spacing, and DHT alignment.
- Improved macOS combo boxes and popup menus with clearer padding, rounded popup styling, readable dark/light colors, and explicit arrow indicators.
- Redesigned Preferences sub-tabs to match the rounded macOS fork styling instead of the old flat grey segmented tabs.
- Added the hub user-list "Filter" placeholder and refreshed Qt translation catalogs for the new UI strings.
- Reworked Download Queue context menus so generic actions remain available when source data is missing, source-specific submenus disable cleanly, and the menu has consistent spacing/icons.
- Fixed Transfer View right-click "Cancel download" so it removes queued source entries and closes the active connection instead of becoming a no-op for source-only rows.
- Hardened the hub user-list model against stale indexes during live updates and added a macOS startup guard for the Qt 6.11 Cocoa accessibility crash path.
- Updated About/Homepage/project links to https://github.com/Delitants/eiskaltdcpp.

## Prior Fork Changes Included

- Native macOS ARM64 release packaging with bundled Qt 6 frameworks and plugins.
- Dark/light theme handling across hub chat, private messages, transfers, preferences, menus, and live system appearance switching.
- Chat input, BBCode panel, emoji picker positioning/sizing, spoiler image display, pasted-image handling, and local image cache behavior improvements.
- Hub tab close-button alignment and toolbar/icon fixes, including stable Dock icon behavior.
- IPv6 support exposure in connection/user-list UI.
- Sharing settings Add/Remove buttons and simplified sharing layout.
- Legacy GUI config detection with a safe import/discard warning path.
- Crash fixes for DHT status updates, ChatEdit palette recursion, startup platform plugin loading, and macOS close/reopen behavior.
- Windows 11/Visual Studio oriented build helper documentation and project setup support.

## Contributors In This Delta
 -  211 Joe Rivera <j@jriv.us>
 -   41 Neolo <admin@nlight.org.ua>

## Top-Level Areas Changed (file counts)
-  164 eiskaltdcpp-qt
-  145 dcpp
-   98 eiskaltdcpp-gtk
-   82 tests
-   20 dht
-   10 eiskaltdcpp-daemon
-    8 extra
-    7 cmake
-    6 windows
-    6 macos
-    3 json
-    2 haiku
-    1 update-translations.sh
-    1 screenshot.png
-    1 linux
-    1 data
-    1 build-local.ps1
-    1 Version.h.in
-    1 SAFE_PORT_STATUS.txt
-    1 README.md
-    1 README.html
-    1 INSTALL
-    1 ChangeLog.txt
-    1 CMakePresets.json
-    1 CMakeLists.txt
-    1 CHANGELOG_FORK_DELTA.md
-    1 AUTHORS
-    1 .gitignore
-    1 .github

## Diff Stat vs Upstream

```
 .github/workflows/build.yml                        | 1254 +++++++++++
 .gitignore                                         |   10 +
 AUTHORS                                            |    7 +-
 CHANGELOG_FORK_DELTA.md                            |  297 +++
 CMakeLists.txt                                     |  143 +-
 CMakePresets.json                                  |  106 +
 ChangeLog.txt                                      |  118 +
 INSTALL                                            |    6 +-
 README.html                                        |   10 +-
 README.md                                          |   87 +-
 SAFE_PORT_STATUS.txt                               |   24 +
 Version.h.in                                       |    2 +-
 build-local.ps1                                    | 1155 ++++++++++
 cmake/CMakeLists.txt                               |  508 ++---
 cmake/FindASPELL.cmake                             |   14 +-
 cmake/FindGTK3.cmake                               |    3 +-
 cmake/FindGettext.cmake                            |   45 +-
 cmake/FindLua.cmake                                |   13 +-
 cmake/FindMiniupnpc.cmake                          |   11 +-
 cmake/mingw_gcc15_semaphore_fix.h                  |   18 +
 data/CMakeLists.txt                                |    4 +-
 dcpp/ADLSearch.cpp                                 |   17 +-
 dcpp/ADLSearch.h                                   |    7 +-
 dcpp/AdcCommand.h                                  |    6 +-
 dcpp/AdcHub.cpp                                    |  254 ++-
 dcpp/AdcHub.h                                      |   54 +-
 dcpp/BZUtils.cpp                                   |  106 +-
 dcpp/BZUtils.h                                     |   57 +-
 dcpp/BloomFilter.h                                 |  113 +-
 dcpp/BufferedSocket.cpp                            |   63 +-
 dcpp/BufferedSocket.h                              |   15 +-
 dcpp/BufferedSocketListener.h                      |   18 +-
 dcpp/CID.cpp                                       |   14 +-
 dcpp/CID.h                                         |   51 +-
 dcpp/CMakeLists.txt                                |   75 +-
 dcpp/Client.cpp                                    |   57 +-
 dcpp/Client.h                                      |   22 +-
 dcpp/ClientListener.h                              |   34 +-
 dcpp/ClientManager.cpp                             |  119 +-
 dcpp/ClientManager.h                               |   67 +-
 dcpp/ClientManagerListener.h                       |   14 +-
 dcpp/ConnectionManager.cpp                         |  191 +-
 dcpp/ConnectionManager.h                           |   52 +-
 dcpp/ConnectionManagerListener.h                   |   38 +-
 dcpp/ConnectivityManager.cpp                       |   79 +-
 dcpp/ConnectivityManager.h                         |   19 +-
 dcpp/CriticalSection.h                             |   12 +-
 dcpp/CryptoManager.cpp                             |  274 ++-
 dcpp/CryptoManager.h                               |   24 +-
 dcpp/DCContext.cpp                                 |  280 +++
 dcpp/DCContext.h                                   |  190 ++
 dcpp/DCPlusPlus.cpp                                |  172 +-
 dcpp/DCPlusPlus.h                                  |   20 +-
 dcpp/DebugManager.h                                |   26 +-
 dcpp/DirectoryListing.cpp                          |   19 +-
 dcpp/DirectoryListing.h                            |   11 +-
 dcpp/Download.cpp                                  |    8 +-
 dcpp/Download.h                                    |    2 +-
 dcpp/DownloadManager.cpp                           |   84 +-
 dcpp/DownloadManager.h                             |   30 +-
 dcpp/DownloadManagerListener.h                     |   75 +-
 dcpp/Encoder.cpp                                   |  176 +-
 dcpp/Encoder.h                                     |   31 +-
 dcpp/Exception.cpp                                 |   10 +-
 dcpp/Exception.h                                   |   50 +-
 dcpp/FastAlloc.h                                   |  109 +-
 dcpp/FavoriteManager.cpp                           |   69 +-
 dcpp/FavoriteManager.h                             |   40 +-
 dcpp/FavoriteManagerListener.h                     |   20 +-
 dcpp/File.cpp                                      |   50 +-
 dcpp/File.h                                        |   26 +-
 dcpp/FinishedItem.cpp                              |  133 +-
 dcpp/FinishedItem.h                                |  130 +-
 dcpp/FinishedManager.cpp                           |   28 +-
 dcpp/FinishedManager.h                             |   16 +-
 dcpp/FinishedManagerListener.h                     |   45 +-
 dcpp/Flags.h                                       |   53 +-
 dcpp/HashBloom.cpp                                 |  125 +-
 dcpp/HashBloom.h                                   |   65 +-
 dcpp/HashManager.cpp                               |   86 +-
 dcpp/HashManager.h                                 |   47 +-
 dcpp/HashManagerListener.h                         |   42 +-
 dcpp/HashValue.h                                   |   46 +-
 dcpp/HttpConnection.cpp                            |   29 +-
 dcpp/HttpConnection.h                              |   17 +-
 dcpp/HttpConnectionListener.h                      |   14 +-
 dcpp/HubEntry.cpp                                  |   16 +
 dcpp/HubEntry.h                                    |   19 +-
 dcpp/LogManager.cpp                                |   15 +-
 dcpp/LogManager.h                                  |   22 +-
 dcpp/LogManagerListener.h                          |   17 +-
 dcpp/MappingManager.cpp                            |   29 +-
 dcpp/MappingManager.h                              |   14 +-
 dcpp/MerkleTree.h                                  |  372 ++--
 dcpp/NmdcHub.cpp                                   |  302 ++-
 dcpp/NmdcHub.h                                     |   16 +-
 dcpp/OnlineUser.h                                  |   34 +-
 dcpp/PerFolderLimit.cpp                            |   14 +-
 dcpp/PerFolderLimit.h                              |    3 +-
 dcpp/Pointer.h                                     |   44 +-
 dcpp/QueueItem.cpp                                 |   18 +-
 dcpp/QueueItem.h                                   |   11 +-
 dcpp/QueueManager.cpp                              |  258 ++-
 dcpp/QueueManager.h                                |   75 +-
 dcpp/QueueManagerListener.h                        |   99 +-
 dcpp/ResourceManager.cpp                           |    5 +
 dcpp/ResourceManager.h                             |   15 +-
 dcpp/SFVReader.cpp                                 |   68 +-
 dcpp/SFVReader.h                                   |   49 +-
 dcpp/SSL.h                                         |   16 +
 dcpp/SSLSocket.cpp                                 |   83 +-
 dcpp/SSLSocket.h                                   |   25 +-
 dcpp/ScopedFunctor.h                               |   18 +-
 dcpp/ScriptManager.cpp                             |   76 +-
 dcpp/ScriptManager.h                               |   26 +-
 dcpp/SearchManager.cpp                             |  132 +-
 dcpp/SearchManager.h                               |   23 +-
 dcpp/SearchManagerListener.h                       |   19 +-
 dcpp/SearchResult.cpp                              |    8 +-
 dcpp/SearchResult.h                                |    3 +-
 dcpp/Semaphore.h                                   |   24 +-
 dcpp/SettingsManager.cpp                           |   53 +-
 dcpp/SettingsManager.h                             |   36 +-
 dcpp/ShareManager.cpp                              |  182 +-
 dcpp/ShareManager.h                                |   67 +-
 dcpp/SimpleXML.cpp                                 |    2 +-
 dcpp/SimpleXML.h                                   |    6 +-
 dcpp/Singleton.h                                   |   37 +-
 dcpp/Socket.cpp                                    |  397 +++-
 dcpp/Socket.h                                      |   50 +-
 dcpp/Speaker.h                                     |   55 +-
 dcpp/Streams.h                                     |  337 +--
 dcpp/StringSearch.h                                |  161 +-
 dcpp/StringTokenizer.cpp                           |   10 +-
 dcpp/StringTokenizer.h                             |   59 +-
 dcpp/Text.cpp                                      |   83 +-
 dcpp/Text.h                                        |   68 +-
 dcpp/Thread.cpp                                    |   65 +-
 dcpp/Thread.h                                      |  137 +-
 dcpp/ThrottleManager.cpp                           |   67 +-
 dcpp/ThrottleManager.h                             |   35 +-
 dcpp/TimerManager.cpp                              |    8 +-
 dcpp/TimerManager.h                                |   17 +-
 dcpp/Transfer.cpp                                  |    8 +-
 dcpp/Upload.cpp                                    |   15 +-
 dcpp/Upload.h                                      |   53 +-
 dcpp/UploadManager.cpp                             |  130 +-
 dcpp/UploadManager.h                               |   39 +-
 dcpp/UploadManagerListener.h                       |   69 +-
 dcpp/User.cpp                                      |   54 +-
 dcpp/UserCommand.h                                 |    2 +-
 dcpp/UserConnection.cpp                            |   38 +-
 dcpp/UserConnection.h                              |   28 +-
 dcpp/UserConnectionListener.h                      |   50 +-
 dcpp/Util.cpp                                      |  259 ++-
 dcpp/Util.h                                        |   18 +-
 dcpp/format.h                                      |    2 +-
 dcpp/intrusive_ptr.h                               |   34 +-
 dcpp/po/da.po                                      |    2 +-
 dcpp/po/en.po                                      |    2 +-
 dcpp/po/ie.po                                      |    2 +-
 dcpp/po/pl.po                                      |    2 +-
 dcpp/po/sk.po                                      |    2 +-
 dcpp/po/vi.po                                      |    2 +-
 dcpp/po/zh_CN.po                                   |    2 +-
 dcpp/stdinc.cpp                                    |    5 +
 dht/BootstrapManager.cpp                           |   50 +-
 dht/BootstrapManager.h                             |   10 +-
 dht/CMakeLists.txt                                 |   10 +-
 dht/ConnectionManager.cpp                          |   30 +-
 dht/ConnectionManager.h                            |   12 +-
 dht/DHT.cpp                                        |  108 +-
 dht/DHT.h                                          |   41 +-
 dht/IndexManager.cpp                               |   12 +-
 dht/IndexManager.h                                 |   22 +-
 dht/KBucket.cpp                                    |   30 +-
 dht/KBucket.h                                      |    8 +-
 dht/SearchManager.cpp                              |   62 +-
 dht/SearchManager.h                                |   15 +-
 dht/TaskManager.cpp                                |   32 +-
 dht/TaskManager.h                                  |   10 +-
 dht/UDPSocket.cpp                                  |   72 +-
 dht/UDPSocket.h                                    |    6 +
 dht/Utils.cpp                                      |    5 +-
 dht/Utils.h                                        |    4 +-
 dht/stdafx.cpp                                     |    5 +
 eiskaltdcpp-daemon/CMakeLists.txt                  |    5 +-
 eiskaltdcpp-daemon/ServerManager.cpp               |   88 +-
 eiskaltdcpp-daemon/ServerManager.h                 |   72 +-
 eiskaltdcpp-daemon/ServerThread.cpp                |  369 ++--
 eiskaltdcpp-daemon/ServerThread.h                  |   28 +-
 eiskaltdcpp-daemon/jsonrpcmethods.cpp              |  731 +++----
 eiskaltdcpp-daemon/jsonrpcmethods.h                |    6 +
 eiskaltdcpp-daemon/nasdc.cpp                       |  105 +-
 eiskaltdcpp-daemon/stdafx.h                        |    4 +
 eiskaltdcpp-daemon/xmlrpcserver.h                  |  727 -------
 eiskaltdcpp-gtk/CMakeLists.txt                     |   38 +-
 eiskaltdcpp-gtk/src/GtkContextAware.hh             |   31 +
 eiskaltdcpp-gtk/src/UserCommandMenu.cc             |   19 +-
 eiskaltdcpp-gtk/src/UserCommandMenu.hh             |    5 +-
 eiskaltdcpp-gtk/src/WulforUtil.cc                  |   51 +-
 eiskaltdcpp-gtk/src/WulforUtil.hh                  |   24 +-
 eiskaltdcpp-gtk/src/adlsearch.cc                   |   23 +-
 eiskaltdcpp-gtk/src/adlsearch.hh                   |    5 +-
 eiskaltdcpp-gtk/src/bacon-message-connection.cc    |   45 +-
 eiskaltdcpp-gtk/src/bookentry.cc                   |    6 +-
 eiskaltdcpp-gtk/src/cmddebug.cc                    |   11 +-
 eiskaltdcpp-gtk/src/cmddebug.hh                    |    7 +-
 eiskaltdcpp-gtk/src/dialogentry.cc                 |    6 +-
 eiskaltdcpp-gtk/src/downloadqueue.cc               |  117 +-
 eiskaltdcpp-gtk/src/downloadqueue.hh               |    5 +-
 eiskaltdcpp-gtk/src/entry.cc                       |    8 +-
 eiskaltdcpp-gtk/src/favoritehubs.cc                |   73 +-
 eiskaltdcpp-gtk/src/favoritehubs.hh                |    5 +-
 eiskaltdcpp-gtk/src/favoriteusers.cc               |   75 +-
 eiskaltdcpp-gtk/src/favoriteusers.hh               |    5 +-
 eiskaltdcpp-gtk/src/finishedtransfers.cc           |   71 +-
 eiskaltdcpp-gtk/src/finishedtransfers.hh           |    9 +-
 eiskaltdcpp-gtk/src/hashdialog.cc                  |   36 +-
 eiskaltdcpp-gtk/src/hashdialog.hh                  |    5 +-
 eiskaltdcpp-gtk/src/hub.cc                         |  337 +--
 eiskaltdcpp-gtk/src/hub.hh                         |    3 +-
 eiskaltdcpp-gtk/src/mainwindow.cc                  |  192 +-
 eiskaltdcpp-gtk/src/mainwindow.hh                  |    4 +-
 eiskaltdcpp-gtk/src/models/AdlSearchDisplay.cpp    |   90 +
 eiskaltdcpp-gtk/src/models/AdlSearchDisplay.h      |   97 +
 eiskaltdcpp-gtk/src/models/CMakeLists.txt          |   77 +
 eiskaltdcpp-gtk/src/models/ChatFormatter.cpp       |  311 +++
 eiskaltdcpp-gtk/src/models/ChatFormatter.h         |  134 ++
 eiskaltdcpp-gtk/src/models/DownloadQueueModel.cpp  |  262 +++
 eiskaltdcpp-gtk/src/models/DownloadQueueModel.h    |  137 ++
 eiskaltdcpp-gtk/src/models/DownloadQueueParams.cpp |   84 +
 eiskaltdcpp-gtk/src/models/DownloadQueueParams.h   |   62 +
 eiskaltdcpp-gtk/src/models/EmoticonLoader.cpp      |  160 ++
 eiskaltdcpp-gtk/src/models/EmoticonLoader.h        |   74 +
 .../src/models/FavoriteHubListModel.cpp            |  107 +
 eiskaltdcpp-gtk/src/models/FavoriteHubListModel.h  |   74 +
 eiskaltdcpp-gtk/src/models/FavoriteHubParams.cpp   |   72 +
 eiskaltdcpp-gtk/src/models/FavoriteHubParams.h     |   47 +
 .../src/models/FinishedTransferParams.cpp          |   96 +
 .../src/models/FinishedTransferParams.h            |   75 +
 eiskaltdcpp-gtk/src/models/GtkFormatters.cpp       |   69 +
 eiskaltdcpp-gtk/src/models/GtkFormatters.h         |   53 +
 eiskaltdcpp-gtk/src/models/GtkMessageTypes.h       |   74 +
 eiskaltdcpp-gtk/src/models/GtkSettingsDefaults.cpp |  340 +++
 eiskaltdcpp-gtk/src/models/GtkSettingsDefaults.h   |   55 +
 eiskaltdcpp-gtk/src/models/GtkSettingsModel.cpp    |  323 +++
 eiskaltdcpp-gtk/src/models/GtkSettingsModel.h      |  153 ++
 eiskaltdcpp-gtk/src/models/GtkWulforUtil.cpp       |  210 ++
 eiskaltdcpp-gtk/src/models/GtkWulforUtil.h         |   89 +
 eiskaltdcpp-gtk/src/models/HubUserListModel.cpp    |  234 ++
 eiskaltdcpp-gtk/src/models/HubUserListModel.h      |  125 ++
 eiskaltdcpp-gtk/src/models/HubUserParams.cpp       |   77 +
 eiskaltdcpp-gtk/src/models/HubUserParams.h         |   63 +
 eiskaltdcpp-gtk/src/models/NotifyDispatch.h        |  118 +
 eiskaltdcpp-gtk/src/models/PublicHubFilter.cpp     |   98 +
 eiskaltdcpp-gtk/src/models/PublicHubFilter.h       |   95 +
 eiskaltdcpp-gtk/src/models/SearchResultParams.cpp  |  128 ++
 eiskaltdcpp-gtk/src/models/SearchResultParams.h    |   81 +
 eiskaltdcpp-gtk/src/models/SearchResultsModel.cpp  |  227 ++
 eiskaltdcpp-gtk/src/models/SearchResultsModel.h    |  118 +
 eiskaltdcpp-gtk/src/models/SettingsValidation.cpp  |  124 ++
 eiskaltdcpp-gtk/src/models/SettingsValidation.h    |   92 +
 eiskaltdcpp-gtk/src/models/ShareBrowserModel.cpp   |   98 +
 eiskaltdcpp-gtk/src/models/ShareBrowserModel.h     |   93 +
 eiskaltdcpp-gtk/src/models/SoundDispatch.h         |   88 +
 eiskaltdcpp-gtk/src/models/TransferListModel.cpp   |  219 ++
 eiskaltdcpp-gtk/src/models/TransferListModel.h     |  128 ++
 eiskaltdcpp-gtk/src/models/TransferParams.cpp      |  121 ++
 eiskaltdcpp-gtk/src/models/TransferParams.h        |  100 +
 eiskaltdcpp-gtk/src/notify.cc                      |    6 +-
 eiskaltdcpp-gtk/src/previewmenu.cc                 |    6 +-
 eiskaltdcpp-gtk/src/privatemessage.cc              |  124 +-
 eiskaltdcpp-gtk/src/privatemessage.hh              |    5 +-
 eiskaltdcpp-gtk/src/publichubs.cc                  |   63 +-
 eiskaltdcpp-gtk/src/publichubs.hh                  |    5 +-
 eiskaltdcpp-gtk/src/search.cc                      |  135 +-
 eiskaltdcpp-gtk/src/search.hh                      |    5 +-
 eiskaltdcpp-gtk/src/searchspy.cc                   |   25 +-
 eiskaltdcpp-gtk/src/searchspy.hh                   |    5 +-
 eiskaltdcpp-gtk/src/settingsdialog.cc              |  353 +--
 eiskaltdcpp-gtk/src/settingsdialog.hh              |    5 +-
 eiskaltdcpp-gtk/src/settingsmanager.cc             |  482 +---
 eiskaltdcpp-gtk/src/settingsmanager.hh             |   77 +-
 eiskaltdcpp-gtk/src/sharebrowser.cc                |   69 +-
 eiskaltdcpp-gtk/src/sharebrowser.hh                |    5 +-
 eiskaltdcpp-gtk/src/sound.cc                       |    6 +-
 eiskaltdcpp-gtk/src/transfers.cc                   |  125 +-
 eiskaltdcpp-gtk/src/transfers.hh                   |    5 +-
 eiskaltdcpp-gtk/src/uploadqueue.cc                 |  813 +++----
 eiskaltdcpp-gtk/src/uploadqueue.hh                 |    5 +-
 eiskaltdcpp-gtk/src/wulfor.cc                      |  248 ++-
 eiskaltdcpp-gtk/src/wulformanager.cc               |  189 +-
 eiskaltdcpp-gtk/src/wulformanager.hh               |   26 +-
 eiskaltdcpp-qt/CMakeLists.txt                      |  469 ++--
 eiskaltdcpp-qt/Info.plist.in                       |    2 +
 eiskaltdcpp-qt/cmake/copy_aspell.sh                |   13 +
 eiskaltdcpp-qt/cmake/write_qt_conf.sh              |    6 +
 eiskaltdcpp-qt/eiskaltdcpp-qt.appdata.xml          |    4 +-
 .../scriptengine/ClientManagerScript.cpp           |   25 +-
 eiskaltdcpp-qt/scriptengine/ClientManagerScript.h  |   19 +-
 eiskaltdcpp-qt/scriptengine/ConsolePrinter.h       |   33 +
 eiskaltdcpp-qt/scriptengine/HashManagerScript.cpp  |   23 +-
 eiskaltdcpp-qt/scriptengine/HashManagerScript.h    |   19 +-
 eiskaltdcpp-qt/scriptengine/LogManagerScript.cpp   |   12 +-
 eiskaltdcpp-qt/scriptengine/LogManagerScript.h     |   19 +-
 eiskaltdcpp-qt/scriptengine/MainWindowScript.cpp   |   51 +-
 eiskaltdcpp-qt/scriptengine/MainWindowScript.h     |    9 +-
 eiskaltdcpp-qt/scriptengine/ScriptBridge.h         |   52 +
 eiskaltdcpp-qt/scriptengine/ScriptConsole.cpp      |  105 +-
 eiskaltdcpp-qt/scriptengine/ScriptConsole.h        |   11 +-
 eiskaltdcpp-qt/scriptengine/ScriptEngine.cpp       |  664 +++---
 eiskaltdcpp-qt/scriptengine/ScriptEngine.h         |   37 +-
 eiskaltdcpp-qt/src/ADLS.cpp                        |   62 +-
 eiskaltdcpp-qt/src/ADLS.h                          |   30 +-
 eiskaltdcpp-qt/src/ADLSModel.cpp                   |    5 +-
 eiskaltdcpp-qt/src/ActionCustomizer.cpp            |   13 +-
 eiskaltdcpp-qt/src/AntiSpamFrame.cpp               |  153 +-
 eiskaltdcpp-qt/src/Antispam.cpp                    |   40 +-
 eiskaltdcpp-qt/src/Antispam.h                      |   14 +-
 eiskaltdcpp-qt/src/ArenaWidget.cpp                 |   11 +-
 eiskaltdcpp-qt/src/ArenaWidget.h                   |    7 +-
 eiskaltdcpp-qt/src/ArenaWidgetFactory.h            |   15 +-
 eiskaltdcpp-qt/src/ArenaWidgetManager.cpp          |   18 +-
 eiskaltdcpp-qt/src/ArenaWidgetManager.h            |   24 +-
 eiskaltdcpp-qt/src/AutoToolTip.cpp                 |    7 +-
 eiskaltdcpp-qt/src/ChatEdit.cpp                    |  528 ++++-
 eiskaltdcpp-qt/src/ChatEdit.h                      |   28 +
 eiskaltdcpp-qt/src/CmdDebug.cpp                    |   53 +-
 eiskaltdcpp-qt/src/CmdDebug.h                      |   44 +-
 eiskaltdcpp-qt/src/CustomFontModel.cpp             |   17 +-
 eiskaltdcpp-qt/src/DHTBootstrapList.cpp            |  115 +
 eiskaltdcpp-qt/src/DHTBootstrapList.h              |   31 +
 eiskaltdcpp-qt/src/DownloadQueue.cpp               |   81 +-
 eiskaltdcpp-qt/src/DownloadQueue.h                 |   39 +-
 eiskaltdcpp-qt/src/DownloadQueueModel.cpp          |   59 +-
 eiskaltdcpp-qt/src/DownloadQueueModel.h            |    1 +
 eiskaltdcpp-qt/src/DownloadToHistory.h             |   13 +-
 eiskaltdcpp-qt/src/EiskaltApp.h                    |   21 +-
 eiskaltdcpp-qt/src/EiskaltApp_haiku.h              |   21 +-
 eiskaltdcpp-qt/src/EiskaltApp_mac.h                |  170 +-
 eiskaltdcpp-qt/src/EmoticonDialog.cpp              |  144 +-
 eiskaltdcpp-qt/src/EmoticonDialog.h                |   10 +-
 eiskaltdcpp-qt/src/EmoticonFactory.cpp             |   91 +-
 eiskaltdcpp-qt/src/EmoticonFactory.h               |   14 +-
 eiskaltdcpp-qt/src/FavoriteHubModel.cpp            |    5 +-
 eiskaltdcpp-qt/src/FavoriteHubs.cpp                |  113 +-
 eiskaltdcpp-qt/src/FavoriteHubs.h                  |   33 +-
 eiskaltdcpp-qt/src/FavoriteUsers.cpp               |   95 +-
 eiskaltdcpp-qt/src/FavoriteUsers.h                 |   41 +-
 eiskaltdcpp-qt/src/FavoriteUsersModel.cpp          |   12 +-
 eiskaltdcpp-qt/src/FileBrowserModel.cpp            |   29 +-
 eiskaltdcpp-qt/src/FileHasher.cpp                  |   42 +-
 eiskaltdcpp-qt/src/FinishedTransfers.h             |   85 +-
 eiskaltdcpp-qt/src/FinishedTransfersModel.cpp      |   15 +-
 eiskaltdcpp-qt/src/FlowLayout.cpp                  |  112 +-
 eiskaltdcpp-qt/src/FlowLayout.h                    |    8 +-
 eiskaltdcpp-qt/src/GlobalTimer.cpp                 |   49 +-
 eiskaltdcpp-qt/src/GlobalTimer.h                   |   32 +-
 eiskaltdcpp-qt/src/HashProgress.cpp                |   59 +-
 eiskaltdcpp-qt/src/HubFrame.cpp                    | 1733 ++++++++++-----
 eiskaltdcpp-qt/src/HubFrame.h                      |   24 +-
 eiskaltdcpp-qt/src/HubManager.cpp                  |    4 +-
 eiskaltdcpp-qt/src/HubManager.h                    |   12 +-
 eiskaltdcpp-qt/src/IPFilterFrame.cpp               |   72 +-
 eiskaltdcpp-qt/src/IPFilterModel.cpp               |    5 +-
 eiskaltdcpp-qt/src/LineEdit.cpp                    |   20 +-
 eiskaltdcpp-qt/src/Magnet.cpp                      |   86 +-
 eiskaltdcpp-qt/src/MainWindow.cpp                  |  850 +++++---
 eiskaltdcpp-qt/src/MainWindow.h                    |   34 +-
 eiskaltdcpp-qt/src/MultiLineToolBar.cpp            |   33 +-
 eiskaltdcpp-qt/src/Notification.cpp                |  159 +-
 eiskaltdcpp-qt/src/Notification.h                  |   16 +-
 eiskaltdcpp-qt/src/PMWindow.cpp                    |  661 ++++--
 eiskaltdcpp-qt/src/PMWindow.h                      |   14 +-
 eiskaltdcpp-qt/src/PublicHubModel.cpp              |   12 +-
 eiskaltdcpp-qt/src/PublicHubs.cpp                  |   96 +-
 eiskaltdcpp-qt/src/PublicHubs.h                    |   20 +-
 eiskaltdcpp-qt/src/PublicHubsList.cpp              |   28 +-
 eiskaltdcpp-qt/src/QtContext.cpp                   |  150 ++
 eiskaltdcpp-qt/src/QtContext.h                     |  222 ++
 eiskaltdcpp-qt/src/QtContextAware.h                |   59 +
 eiskaltdcpp-qt/src/QueuedUsers.cpp                 |   32 +-
 eiskaltdcpp-qt/src/QueuedUsers.h                   |   35 +-
 eiskaltdcpp-qt/src/QuickConnect.cpp                |   22 +-
 eiskaltdcpp-qt/src/ScriptManagerDialog.cpp         |   17 +-
 eiskaltdcpp-qt/src/SearchBlacklist.cpp             |   12 +-
 eiskaltdcpp-qt/src/SearchBlacklist.h               |   21 +-
 eiskaltdcpp-qt/src/SearchBlacklistDialog.cpp       |   19 +-
 eiskaltdcpp-qt/src/SearchFrame.cpp                 |  256 +--
 eiskaltdcpp-qt/src/SearchFrame.h                   |   15 +-
 eiskaltdcpp-qt/src/SearchModel.cpp                 |   26 +-
 eiskaltdcpp-qt/src/SearchModel.h                   |    4 +-
 eiskaltdcpp-qt/src/Secretary.cpp                   |   76 +-
 eiskaltdcpp-qt/src/Secretary.h                     |   44 +-
 eiskaltdcpp-qt/src/Settings.cpp                    |  577 ++++-
 eiskaltdcpp-qt/src/Settings.h                      |    5 +
 eiskaltdcpp-qt/src/SettingsAdvanced.cpp            |   14 +-
 eiskaltdcpp-qt/src/SettingsConnection.cpp          |  363 +++-
 eiskaltdcpp-qt/src/SettingsConnection.h            |   14 +-
 eiskaltdcpp-qt/src/SettingsDownloads.cpp           |   97 +-
 eiskaltdcpp-qt/src/SettingsGUI.cpp                 |  518 +++--
 eiskaltdcpp-qt/src/SettingsGUI.h                   |   17 +-
 eiskaltdcpp-qt/src/SettingsHistory.cpp             |   63 +-
 eiskaltdcpp-qt/src/SettingsLog.cpp                 |   64 +-
 eiskaltdcpp-qt/src/SettingsNotification.cpp        |  107 +-
 eiskaltdcpp-qt/src/SettingsPersonal.cpp            |   36 +-
 eiskaltdcpp-qt/src/SettingsSharing.cpp             |  337 +--
 eiskaltdcpp-qt/src/SettingsSharing.h               |   13 +-
 eiskaltdcpp-qt/src/SettingsShortcuts.cpp           |   25 +-
 eiskaltdcpp-qt/src/SettingsUC.cpp                  |   41 +-
 eiskaltdcpp-qt/src/ShareBrowser.cpp                |  217 +-
 eiskaltdcpp-qt/src/ShareBrowser.h                  |   21 +-
 eiskaltdcpp-qt/src/ShareBrowserSearch.cpp          |   35 +-
 eiskaltdcpp-qt/src/ShareBrowserSearch.h            |    7 +-
 eiskaltdcpp-qt/src/ShellCommandRunner.cpp          |    5 +-
 eiskaltdcpp-qt/src/ShortcutGetter.cpp              |   20 +-
 eiskaltdcpp-qt/src/ShortcutManager.cpp             |    3 +-
 eiskaltdcpp-qt/src/ShortcutManager.h               |   16 +-
 eiskaltdcpp-qt/src/SideBar.cpp                     |   74 +-
 eiskaltdcpp-qt/src/SpellCheck.cpp                  |   11 +-
 eiskaltdcpp-qt/src/SpellCheck.h                    |   14 +-
 eiskaltdcpp-qt/src/SpyFrame.cpp                    |   29 +-
 eiskaltdcpp-qt/src/SpyFrame.h                      |   19 +-
 eiskaltdcpp-qt/src/SpyModel.cpp                    |   13 +-
 eiskaltdcpp-qt/src/TabButton.cpp                   |  258 ++-
 eiskaltdcpp-qt/src/TabButton.h                     |    6 +-
 eiskaltdcpp-qt/src/TabFrame.cpp                    |  123 +-
 eiskaltdcpp-qt/src/TabFrame.h                      |    2 +-
 eiskaltdcpp-qt/src/ToolBar.cpp                     |  164 +-
 eiskaltdcpp-qt/src/ToolBar.h                       |    2 +
 eiskaltdcpp-qt/src/TransferView.cpp                |  210 +-
 eiskaltdcpp-qt/src/TransferView.h                  |   61 +-
 eiskaltdcpp-qt/src/TransferViewModel.cpp           |   86 +-
 eiskaltdcpp-qt/src/TransferViewModel.h             |    1 +
 eiskaltdcpp-qt/src/UCModel.cpp                     |   48 +-
 eiskaltdcpp-qt/src/UserListModel.cpp               |   49 +-
 eiskaltdcpp-qt/src/UserListModel.h                 |   11 +-
 eiskaltdcpp-qt/src/WulforSettings.cpp              |   58 +-
 eiskaltdcpp-qt/src/WulforSettings.h                |   35 +-
 eiskaltdcpp-qt/src/WulforUtil.cpp                  |  413 +++-
 eiskaltdcpp-qt/src/WulforUtil.h                    |   24 +-
 eiskaltdcpp-qt/src/codeeditor/codeeditor.cpp       |    3 +-
 eiskaltdcpp-qt/src/main.cpp                        |  733 ++++---
 .../src/qtsingleapp/qtsinglecoreapplication.cpp    |   33 +-
 .../src/qtsingleapp/qtsinglecoreapplication.h      |    3 +
 eiskaltdcpp-qt/translations/en.ts                  | 2295 +++++++++++---------
 eiskaltdcpp-qt/ui/HubFrame.ui                      |  268 ++-
 eiskaltdcpp-qt/ui/PrivateMessage.ui                |  235 +-
 eiskaltdcpp-qt/ui/UIAbout.ui                       |    8 +-
 eiskaltdcpp-qt/ui/UIPublicHubs.ui                  |    7 +
 eiskaltdcpp-qt/ui/UISettingsAdvanced.ui            |   18 +
 eiskaltdcpp-qt/ui/UISettingsConnection.ui          |   16 +-
 eiskaltdcpp-qt/ui/UISettingsGUI.ui                 |  129 +-
 eiskaltdcpp-qt/ui/UISettingsHistory.ui             |   34 +-
 eiskaltdcpp-qt/ui/UISettingsLog.ui                 |    2 +-
 eiskaltdcpp-qt/ui/UISettingsPersonal.ui            |   23 +-
 eiskaltdcpp-qt/ui/UISettingsSharing.ui             |   27 +
 extra/CMakeLists.txt                               |    8 +-
 extra/dyndns.cpp                                   |   22 +-
 extra/dyndns.h                                     |    9 +-
 extra/ipfilter.cpp                                 |   15 +-
 extra/ipfilter.h                                   |   17 +-
 extra/lunar.h                                      |    2 +-
 extra/upnpc.cpp                                    |   17 +-
 extra/upnpc.h                                      |    5 +-
 haiku/CMakeLists.txt                               |    3 +-
 haiku/haiku.rdef                                   |    4 +-
 json/CMakeLists.txt                                |    3 +-
 json/jsonrpc-cpp/jsonrpc_httpserver.cpp            |    2 +-
 json/jsonrpc-cpp/netstring.cpp                     |    2 +-
 linux/build-in-ubuntu.sh                           |   22 +-
 macos/build-using-homebrew.sh                      |   60 +-
 macos/check-bundle-external-links.sh               |   87 +
 macos/check-macos-min-version.sh                   |   60 +
 macos/fix-bundle-self-containment.sh               |   68 +
 macos/homebrew-toolchain.cmake                     |   44 +-
 macos/outdated/build-for-personal-use.sh           |    2 +-
 screenshot.png                                     |  Bin 0 -> 523696 bytes
 tests/CMakeLists.txt                               |   99 +
 tests/TestContext.h                                |  104 +
 tests/gtk/CMakeLists.txt                           |   67 +
 tests/gtk/test_adlsearchdisplay.cpp                |  108 +
 tests/gtk/test_chatformatter.cpp                   |  261 +++
 tests/gtk/test_downloadqueuemodel.cpp              |  196 ++
 tests/gtk/test_downloadqueueparams.cpp             |  116 +
 tests/gtk/test_emoticonloader.cpp                  |  199 ++
 tests/gtk/test_favoritehublistmodel.cpp            |  140 ++
 tests/gtk/test_favoritehubparams.cpp               |  117 +
 tests/gtk/test_finishedtransferparams.cpp          |  151 ++
 tests/gtk/test_gtk_main.cpp                        |   11 +
 tests/gtk/test_gtkformatters.cpp                   |  144 ++
 tests/gtk/test_gtksettingsmanager.cpp              |  404 ++++
 tests/gtk/test_gtksettingsmodel.cpp                |  186 ++
 tests/gtk/test_gtkwulforutil.cpp                   |  252 +++
 tests/gtk/test_hubuserlistmodel.cpp                |  214 ++
 tests/gtk/test_hubuserparams.cpp                   |  160 ++
 tests/gtk/test_messagetypes.cpp                    |   66 +
 tests/gtk/test_publichubfilter.cpp                 |  171 ++
 tests/gtk/test_searchresultparams.cpp              |  188 ++
 tests/gtk/test_searchresultsmodel.cpp              |  235 ++
 tests/gtk/test_settingsvalidation.cpp              |  218 ++
 tests/gtk/test_sharebrowsermodel.cpp               |  157 ++
 tests/gtk/test_soundnotify.cpp                     |  166 ++
 tests/gtk/test_transferlistmodel.cpp               |  196 ++
 tests/gtk/test_transferparams.cpp                  |  169 ++
 tests/qt/CMakeLists.txt                            |   70 +
 tests/qt/stubs_wulforutil.cpp                      |   21 +
 tests/qt/test_adlsmodel.cpp                        |  188 ++
 tests/qt/test_antispam.cpp                         |  245 +++
 tests/qt/test_favoritehubmodel.cpp                 |  227 ++
 tests/qt/test_flowlayout.cpp                       |  201 ++
 tests/qt/test_ipfiltermodel.cpp                    |  132 ++
 tests/qt/test_qt_main.cpp                          |   23 +
 tests/qt/test_searchblacklist.cpp                  |  154 ++
 tests/qt/test_spymodel.cpp                         |  168 ++
 tests/qt/test_wulforsettings.cpp                   |  198 ++
 tests/test_adccommand.cpp                          |  341 +++
 tests/test_adlsearch.cpp                           |   94 +
 tests/test_bloomfilter.cpp                         |  153 ++
 tests/test_cid.cpp                                 |   79 +
 tests/test_compression.cpp                         |  235 ++
 tests/test_dccontext.cpp                           |   95 +
 tests/test_debugmanager.cpp                        |  164 ++
 tests/test_dht_context.cpp                         |  115 +
 tests/test_encoder.cpp                             |  120 +
 tests/test_exception.cpp                           |  137 ++
 tests/test_favoritemanager.cpp                     |  285 +++
 tests/test_file_utils.cpp                          |  271 +++
 tests/test_filteredfile.cpp                        |  200 ++
 tests/test_finisheditem.cpp                        |  184 ++
 tests/test_flags.cpp                               |  123 ++
 tests/test_format.cpp                              |  119 +
 tests/test_hashbloom.cpp                           |  146 ++
 tests/test_hubentry.cpp                            |  190 ++
 tests/test_intrusive_ptr.cpp                       |  259 +++
 tests/test_logmanager.cpp                          |  125 ++
 tests/test_main.cpp                                |   12 +
 tests/test_merkletree.cpp                          |  205 ++
 tests/test_nmdc_escape.cpp                         |  118 +
 tests/test_queueitem.cpp                           |  382 ++++
 tests/test_scopedfunctor.cpp                       |   70 +
 tests/test_searchqueue.cpp                         |  180 ++
 tests/test_searchresult.cpp                        |  170 ++
 tests/test_settingsmanager.cpp                     |  121 ++
 tests/test_sfvreader.cpp                           |  165 ++
 tests/test_simplexml.cpp                           |  178 ++
 tests/test_simplexmlreader.cpp                     |  234 ++
 tests/test_streams.cpp                             |  274 +++
 tests/test_string_tokenizer.cpp                    |  124 ++
 tests/test_stringsearch.cpp                        |  143 ++
 tests/test_text.cpp                                |  211 ++
 tests/test_tigerhash.cpp                           |  218 ++
 tests/test_user.cpp                                |   96 +
 tests/test_usercommand.cpp                         |  182 ++
 tests/test_util.cpp                                |   67 +
 tests/test_util_extended.cpp                       |  190 ++
 tests/test_util_extra.cpp                          |  288 +++
 tests/test_util_formatting.cpp                     |  138 ++
 tests/test_version.cpp                             |   63 +
 tests/test_wildcards.cpp                           |  163 ++
 update-translations.sh                             |    2 +-
 windows/EiskaltDC++.nsi                            |  102 +-
 windows/README.txt                                 |   84 +-
 windows/build-using-mxe.sh                         |    3 +-
 windows/eiskaltdcpp.manifest                       |   39 +
 windows/qt.conf                                    |    3 +-
 windows/setup-win11-buildenv.ps1                   |  196 ++
 568 files changed, 43399 insertions(+), 13475 deletions(-)
```

## Complete Commit List (newest first)

- 2026-04-28 0bbc5c13 Release 2.5.4-v1
- 2026-04-28 2c35d329 docs: refresh fork delta after preferences fix
- 2026-04-28 593235c6 Fix macOS Preferences dark-mode focus styling
- 2026-04-28 5f7e03a6 docs: refresh fork delta after crash fix
- 2026-04-28 e796e8e5 Fix DHT status startup crash
- 2026-04-27 b8ea0275 Fix macOS dark-mode repaint and legacy GUI config
- 2026-04-27 8d68ce15 docs: refresh fork delta for 2.5.3
- 2026-04-27 bf20fca3 Release 2.5.3 for macOS ARM64
- 2026-04-22 880186ff Bundle macOS Qt platform plugin to prevent startup abort
- 2026-04-22 960422f2 Fix ChatEdit contrast stylesheet recursion crash on macOS
- 2026-04-22 07214285 Keep macOS dock icon stable and disable runtime icon swapping
- 2026-04-22 638aa935 Fix duplicate dock icons by restoring single-instance guard
- 2026-04-21 f82691c4 ui: refresh hub/pm theme immediately on live macOS mode switch
- 2026-04-21 e452539c docs: refresh full fork delta changelog after branch sync
- 2026-04-21 1bd11379 docs: add full fork delta changelog and fix changelog links
- 2026-04-21 90417f83 ui: fix PM input/bbcode layout and disable favorite hub CID override
- 2026-04-21 a3a7f547 Update README.md
- 2026-04-21 ad4cc289 Update README.md
- 2026-04-21 4bccac17 Add files via upload
- 2026-04-20 61e3d138 ui: force contrast-safe chat text colors in hub and PM
- 2026-04-20 efeeeb11 ui: refresh chat text style after background palette updates
- 2026-04-20 80534734 ui: derive chat text color strictly from chat base background
- 2026-04-20 afab5a0d Delete assets/screenshots/eiskaltdcpp-macos-2.5.2-release.png
- 2026-04-20 b4bc1e62 Update README.md
- 2026-04-20 b4c3351a ui: force chat text white in dark mode and black in light mode
- 2026-04-20 7b0a1e52 ui: improve dark-mode contrast across chat and settings
- 2026-04-20 fb02582b docs: replace README screenshot with current Eiskalt window capture
- 2026-04-20 2e80cb04 docs: add 2.5.2-release changelog and update authors
- 2026-04-20 1244b338 docs: replace README screenshot with current macOS release UI
- 2026-04-20 f8e28612 Release 2.5.2-release
- 2026-04-10 bdedf929 Add safe 0.883 port status checkpoint note
- 2026-04-10 823b7d05 Port DC++ 0.883 upload translation unit cleanup
- 2026-04-10 01b8908d Port DC++ 0.883 connection manager listener with compatibility shim
- 2026-04-10 f90d990b Port DC++ 0.883 listener headers and align local noexcept overrides
- 2026-04-10 3339fe69 Port DC++ 0.883 finished and log manager listener headers
- 2026-04-10 f9a45439 Port DC++ 0.883 finished item helpers with HintedUser include shim
- 2026-04-10 3b3a0ed7 Port DC++ 0.883 pointer helpers with local compatibility shims
- 2026-04-10 e2444524 Port DC++ 0.883 bloom and allocator helpers
- 2026-04-10 8cc65134 Ignore upstream import workspace
- 2026-04-10 90d0852c Fix macOS ARM Qt6 app bundling and daemon/json build issues
- 2026-04-10 1f2eaba2 Port stable DC++ 0.883 core helper batch
- 2026-04-07 c9940e1e fix: reduce thread stack to 1MiB on POSIX; catch bad_alloc in connect()
- 2026-04-07 36aec609 diag: granular bad_alloc tracing inside nmdcConnect and getConnection
- 2026-04-07 24779e25 diag: add mode value and RevConnectToMe traces to NmdcHub
- 2026-04-07 bb375f65 revert: remove all getUser() caps, restore clean nmdcConnect
- 2026-04-07 d48f2118 fix: cap ALL user creation paths with NMDC_GETINFO_LIMIT
- 2026-04-07 b3cdbd62 fix: cap / user creation with NMDC_GETINFO_LIMIT; granular exception tracing in nmdcConnect
- 2026-04-06 245b3035 diag: granular bad_alloc tracing inside nmdcConnect — wrap checkHubCCBlock, setter, and connect in individual try/catch
- 2026-04-06 72a39287 diag: trace file transfer flow — QM::addList, CM::onSecond, ClientManager::connect, NmdcHub::connectToMe
- 2026-04-06 1344f703 diag: add ENTER trace at top of ConnectionManager::nmdcConnect
- 2026-04-06 6b91e395 diag: add step-by-step tracing to $ConnectToMe handler
- 2026-04-06 5b7f817f diag: enhanced bad_alloc diagnostics in ConnectionManager and NmdcHub
- 2026-04-06 7c155b84 Add NMDC_GETINFO_LIMIT setting to cap $GetINFO requests
- 2026-04-06 22e35583 fix: add try/catch to NmdcHub::on(Line) for bad_alloc resilience
- 2026-04-06 f5ba9c07 diag: add listener type info to Speaker::fire exception logging
- 2026-04-06 82dcfc38 fix: remove all noexcept from dcpp library functions
- 2026-04-06 3d1e9334 fix: remove noexcept from all listener on() methods and related functions
- 2026-04-05 be7c32cd fix: add try/catch inside noexcept on() methods and prepareFile() ok: section
- 2026-04-05 33b4258c fix: catch exceptions in Speaker::fire() to prevent std::terminate
- 2026-04-05 7b114397 fix: catch std::exception in BufferedSocket::run() and Thread::start()
- 2026-04-03 040e72be fix(ci): version extraction regex captured trailing CMake comment
- 2026-04-02 00a7d73b fix: replace FILE*-based OpenSSL calls with BIO in CryptoManager
- 2026-04-02 f5a2caa1 ci: add retry logic for NSIS chocolatey install
- 2026-04-02 accef416 diag: finer-grained startup fprintf for DHT construction
- 2026-04-02 3c494392 fix(win32): only call WSACleanup when WSAStartup was called
- 2026-04-02 d6ca5500 diag: add fprintf diagnostics to DCContext::startup() for Windows CI crash
- 2026-03-30 669c02b7 fix: guard qtCtx() null dereference in SpyModel::addResult()
- 2026-03-27 ff9a1143 Remove global mutable state macros; refactor daemon ServerManager to class
- 2026-03-26 f88cdbef Phase 3-5,7: Eliminate getContext() from all frontends and daemon
- 2026-03-26 731be25b refactor: Phase 2 — DCContext& constructor injection, remove all global DCContext pointers
- 2026-03-25 16ab6fe5 refactor: Phase 1c — convert LOG/COMMAND_DEBUG to CTX_ variants in dcpp/
- 2026-03-25 8aa997d9 refactor: Phase 1b — convert SETTING/BOOLSETTING to CTX_ variants in dcpp/
- 2026-03-25 a150e127 refactor: Phase 1a — convert ContextAware dcpp/ classes from getContext() to ctx()
- 2026-03-25 3f622b51 refactor: Phase 0 — add CTX_ macros, dcCtx(), and GtkContextAware
- 2026-03-25 7d0cdf66 chore: remove completed plan documents from repo
- 2026-03-25 884658b9 fix(windows): use dcpp::getContext() in ScriptInstance::EvaluateFile
- 2026-03-25 be17e44e fix(gtk): update wulfor.cc for new dcpp::startup() ownership API
- 2026-03-25 d26ba742 chore: remove PR_DESCRIPTION.md from repo
- 2026-03-25 5ad6ff64 docs: update PR description with g_context removal details
- 2026-03-25 0b02b4f2 refactor: remove DCContext singleton (g_context)
- 2026-03-25 37fb077e Remove dead singleton code and fix outdated comments
- 2026-03-25 65ab1dba refactor: remove all singleton patterns from GTK, daemon, and DHT subsystems
- 2026-03-24 267b09d1 fix(msvc): add missing <clocale> include in Util.h; rename CI artifacts
- 2026-03-24 ae32f025 ci: remove redundant vcpkg cache step on Windows
- 2026-03-24 4bca0d9a fix(windows): resolve gettext tools not found on Windows CI
- 2026-03-24 dba73bc5 ci: add Debug/Release build matrix to Linux and macOS jobs
- 2026-03-24 cb216759 Remove macos-13 runners (sunset by GitHub Actions)
- 2026-03-24 b4e7b739 Fix macOS install path collision; restore Intel builds
- 2026-03-24 75f9557e Drop macos-13 runners (sunset by GitHub Actions)
- 2026-03-24 c96d0989 Replace qt_mac_set_dock_menu() with QMenu::setAsDockMenu() (Qt6)
- 2026-03-24 da6a930e Fix QVector::iterator used as bool in ShareBrowser (macOS)
- 2026-03-24 c9cdb24a fix(adc): add __aarch64__ to little-endian architecture check
- 2026-03-24 c11b1957 ci(macos): build and package DMGs for both Intel and Apple Silicon
- 2026-03-24 657692bc fix(gtk): add GTK3 library directories when using pkg-config
- 2026-03-24 e9e244f2 fix(gtk): add OPENSSL_INCLUDE_DIR to GTK target includes
- 2026-03-24 5f9b4998 fix(tests): add GETTEXT_INCLUDE_DIR to Qt test target
- 2026-03-24 82f84ef7 fix(macos): use pkg-config for GTK3 discovery on macOS
- 2026-03-24 a9422027 Fix macOS build: include HintedUser.h in FinishedItem.h, qualify std::move
- 2026-03-24 3f8df54e Fix macOS build: fall back to std::thread when std::jthread unavailable
- 2026-03-24 f4398010 CI: add macOS builds (Qt6 + GTK3) with DMG packaging
- 2026-03-20 30f408b4 Fix installer: add SetRegView 64 so uninstall entry appears in Add/Remove Programs
- 2026-03-20 eb645f35 Fix installer: move shortcuts into main section so they always get created
- 2026-03-19 60c15d1e Document all build dependencies and setup requirements in build-local.ps1
- 2026-03-19 b9f1b998 Fix NSIS installer: proper staging, desktop shortcut, Qt6 plugins
- 2026-03-19 97910f91 Fix GTK3 pixbuf loader crash: bundle transitive DLL deps
- 2026-03-19 9ff5bb48 Add Windows build script and ignore dist/build output dirs
- 2026-03-19 a989d02e Fix MSVC C2229: zero-sized float arrays in SettingsManager
- 2026-03-19 ff3eb89e Fix FloatSetting SENTRY assertion on startup (pre-existing bug)
- 2026-03-19 93e514bc Fix ArenaWidgetManager shutdown crash (use-after-free)
- 2026-03-19 fc269abe Fix QT_CONTEXT_MINIMAL test build after singleton removal
- 2026-03-19 33ed5089 Eliminate all singleton patterns from eiskaltdcpp-qt
- 2026-03-18 29c28be2 fix: ScriptEngine shutdown crash — owner destroys owned objects
- 2026-03-18 748995a6 fix: ScriptEngine shutdown crash — reorder QtContext member declarations
- 2026-03-07 b47af245 fix: HashProgress shutdown crash — destroy Qt widgets before dcpp managers
- 2026-03-07 fb1828c1 fix: migrate IPFilter from Singleton to DCContext ownership
- 2026-03-05 5e7a8b03 chore: remove PR_DESCRIPTION.md from source control
- 2026-03-05 dbb6b67c fix: ThrottleManager::shutdown() crash on Windows (UB mutex unlock)
- 2026-03-05 08f5d8e9 ci: include PDB debug symbols in Windows Qt6 Debug artifact
- 2026-03-05 5aa09f71 fix(gtk/win32): use forward slashes in loaders.cache to avoid escape mangling
- 2026-03-05 8b6cd135 Fix Qt6 exit crash and NMDC encoding conversion
- 2026-03-05 a00963c5 fix(ci): add fallback URL for Catch2 download
- 2026-03-05 0b7df164 fix: iconv charset conversion on Windows + strengthen GTK pixbuf loader fix
- 2026-03-05 bbeb1b4f fix: duplicate manifest link error on MSVC (LNK1123)
- 2026-03-05 d2b3db16 fix: Windows binary startup failures (Qt6 + GTK3)
- 2026-03-04 df244a4b Phase 5: WulforSettingsManager singleton removal + Windows test fixes
- 2026-03-03 589b543f GTK modernization: Phases 1-4 complete — 282 tests, 903 assertions
- 2026-03-03 876da10e fix: replace deprecated gdk_threads with g_idle_add for GUI dispatch
- 2026-03-03 56d045c5 fix: fetch Catch2 via URL tarball instead of git clone
- 2026-03-03 ce64a18e fix: GTK pixbuf loader crash on Windows + remove redundant VC redist
- 2026-03-03 9023eef9 fix(test): don't assert uninitialized HubEntry numeric fields
- 2026-03-03 5931e645 fix(gtk/win32): set GTK3 runtime env vars from exe path before gtk_init
- 2026-03-03 fbc384a5 fix: use std::nullptr_t in intrusive_ptr.h for GCC 15 modules-ts compatibility
- 2026-03-03 5d471cea tests: add 12 new test files covering Streams, StringSearch, ScopedFunctor, intrusive_ptr, Exception, version, HubEntry, DebugManager, BloomFilter, Flags, format, FilteredFile
- 2026-03-02 b9fdc2ae Migrate ScriptEngine + helpers from dcpp::Singleton to QtContext
- 2026-03-02 788992b5 Migrate all Qt-side dcpp::Singleton<> classes to QtContext
- 2026-03-02 ceacdb19 refactor(qt): introduce QtContext — own WulforSettings & SearchBlacklist
- 2026-03-02 eefe561d refactor(qt): remove Singleton<> from SearchBlacklist
- 2026-03-02 29f8fee6 refactor(qt): remove Singleton<> from WulforSettings
- 2026-03-02 b029b68d fix(tests): resolve MSVC static destruction order fiasco (#heap-corruption)
- 2026-03-02 56e84da0 fix(tests): Windows cross-platform compatibility for MSVC and MSYS2
- 2026-03-02 8fcf279b fix(build): proper workaround for GCC 15 <semaphore> on MSYS2/MinGW
- 2026-03-02 766b3552 fix(build): work around GCC 15 broken <semaphore> on MSYS2/MinGW
- 2026-03-02 7ed38947 fix(tests): use offscreen Qt platform for headless CI
- 2026-03-02 48e4fce5 test: Phase 4 — Qt UI Logic tests (111 cases, 212 assertions)
- 2026-03-02 10dc7231 docs: update TEST_COVERAGE_PLAN.md — Phase 3 complete (428 cases)
- 2026-03-02 af4fd399 test: Phase 3 — protocol, file I/O, hash tree tests (428 cases, 1890 assertions)
- 2026-03-02 14aac2ea docs: update TEST_COVERAGE_PLAN.md — Phase 1+2 complete (354 cases)
- 2026-03-02 358ed378 tests: Phase 2 round 3 — SearchResult, FavoriteManager
- 2026-03-02 4ae7109c tests: Phase 2 round 2 — FinishedItem, LogManager
- 2026-03-02 deaaac1f tests: Phase 2 round 1 — TestContext, SettingsManager, User, SearchQueue
- 2026-03-02 c5b6c27e tests: Phase 1 round 2 — ADLSearch, QueueItem/Segment, UserCommand, Util extra
- 2026-03-01 84864413 tests: Phase 1 coverage — TigerHash, HashBloom, Wildcards, compression, SimpleXMLReader, Util formatting
- 2026-03-01 2e00cdde refactor: migrate ScriptManager and DynDNS from Singleton to DCContext
- 2026-03-01 6a8794ab fix: Singleton test hangs on MSVC Debug (dcassert pops dialog on nullptr)
- 2026-03-01 77a62a3a CI: add CPACK_PACKAGE_DESCRIPTION for NSIS
- 2026-03-01 ec835423 CI: add CPACK_PACKAGE_FILE_NAME for NSIS
- 2026-03-01 58686e58 CI: install NSIS via choco (not pre-installed on windows-latest)
- 2026-03-01 b4540411 Fix install packaging: merge Windows packaging into build jobs
- 2026-03-01 a2ff5496 CI: rename install dir to dist (avoids INSTALL file conflict on Windows)
- 2026-03-01 885ca572 CI: add cmake/ninja/pkg-config to GTK3 package job MSYS2 setup
- 2026-03-01 1fd2342e CI: NSIS is pre-installed on windows-latest, remove continue-on-error
- 2026-03-01 537863ad CI: use cmake --install for Windows packaging + CPack NSIS installer
- 2026-03-01 2b71574a Fix buffer overrun in sanitizeUrl and trimCopy
- 2026-03-01 7f79839a CI: fix YAML syntax error from heredoc in GTK3 packaging
- 2026-03-01 cdfb26f8 CI: Debug+Release matrix for Windows builds + dumpbin DLL resolution
- 2026-03-01 4f8110ab fix: MSVC runtime bundling + GTK3 Windows launch failures
- 2026-03-01 26ed6bd7 fix: find VC runtime DLLs without msvc-dev-cmd environment
- 2026-03-01 b684a99d fix: bundle MSVC runtime DLLs in Windows Qt6 package
- 2026-03-01 d7fe22d5 fix: correct Qt6 exe name in Windows packaging step
- 2026-03-01 698e5b50 fix: daemon is a console app, remove WIN32 from add_executable
- 2026-03-01 e07b113f fix: only define HAVE_NL_MSG_CAT_CNTR when check actually passes
- 2026-02-28 512f67b5 fix: resolve unistring.lib linker error and fix vcpkg caching
- 2026-02-28 59567e5c fix: eliminate tstring/string type mismatches in Qt UI for MSVC UNICODE
- 2026-02-28 c866812c fix: remove redundant Text::fromT wrapping _tq() on MSVC/UNICODE builds
- 2026-02-28 b5365852 fix: move FakeMgr into dcpp namespace for MSVC template specialization
- 2026-02-28 8b8f6366 fix: MSVC cannot specialize template in anonymous namespace
- 2026-02-28 361925a9 fix: MSVC dllimport error in ScriptManager Lua helper functions
- 2026-02-27 f4a885be fix: MSVC min/max macro collision and add vcpkg binary caching
- 2026-02-27 58c627ed fix: copy all vcpkg DLLs for Windows Qt6 packaging
- 2026-02-27 7c49d832 fix: use ldd to bundle all GTK3 transitive DLL dependencies on Windows
- 2026-02-27 49ca34de release: bump version to 2.5.0 and add release job on version tags
- 2026-02-27 0486f8cb chore: remove AppImage builds, defer to self-hosted runners later
- 2026-02-27 99322e66 fix: add APPIMAGE_EXTRACT_AND_RUN=1 for linuxdeploy on CI
- 2026-02-27 21edf900 fix: AppImage OUTPUT to current dir, not dist/ subdirectory
- 2026-02-27 e5e12c3e fix: install gettext[tools] via vcpkg and add FindGettext diagnostics
- 2026-02-27 eb1eb726 fix: AppImage artifacts contained linuxdeploy tools instead of output
- 2026-02-27 e1b384de fix: update FindGettext.cmake for vcpkg compatibility
- 2026-02-27 ff5a2f6e fix: pass explicit GETTEXT_INCLUDE_DIR and GETTEXT_INTL_LIBRARY for vcpkg
- 2026-02-27 558dcdfa fix: add GETTEXT_SEARCH_PATH for vcpkg on Windows Qt6 build
- 2026-02-27 773c766f fix: use vcpkg gettext instead of choco for Windows Qt6 build
- 2026-02-27 34d49edc fix: progress bar text rendering in download queue and transfer view
- 2026-02-27 225c604d fix: pass explicit '/' separator in path tests for Windows compat
- 2026-02-27 b367731f fix: disable BUILD_TESTS in packaging jobs to avoid Catch2 git error
- 2026-02-27 61f77136 fix: remove invalid 'shell' property from download-artifact step
- 2026-02-27 0248afd2 ci: separate Windows packaging into dedicated jobs
- 2026-02-27 924eb33b ci: separate packaging jobs from build+test, fix AppImage LD_LIBRARY_PATH
- 2026-02-27 291f1d4a fix: replace non-standard 'uint' with 'unsigned int' in search.cc
- 2026-02-26 5ce45900 fix: coverage genhtml source error and Catch2 path leaking into report
- 2026-02-26 cb02fdf9 fix: add source to lcov --ignore-errors for coverage job
- 2026-02-26 81713f69 fix: Windows build - gettext discovery and POSIX guards for GTK sources
- 2026-02-26 9b0b8b7d fix: move arpa/inet.h include inside HAVE_IFADDRS_H guard (Windows)
- 2026-02-26 e80e89c1 ci: drop vcpkg gettext entirely, use choco for both tools and runtime
- 2026-02-26 dde3a25b ci: add --ignore-errors to lcov for gcov/mismatch warnings
- 2026-02-26 46235883 ci: add test coverage job with lcov HTML report artifact
- 2026-02-26 5e2030d0 ci: replace vcpkg gettext[tools] with choco prebuilt (fixes hang)
- 2026-02-26 2b707835 ci: fix MSYS2 GTK3, add AppImage builds, remove FreeBSD (temporary)
- 2026-02-26 775c35d7 cmake: fix FindGTK3 library names for MSYS2/MinGW
- 2026-02-26 d557ebc5 ci: fix FreeBSD Qt6 discovery and disable tests in VM
- 2026-02-26 5e5ed19a ci: fix version extraction for deb packaging (take first match only)
- 2026-02-26 173d8c42 ci: remove qtdeclarative from Qt modules (included in base)
- 2026-02-26 60769c3b ci: fix GTK3 canberra dep, MSYS2 step order, FreeBSD stability
- 2026-02-26 9c795e77 ci: fix deb packaging permissions and Windows Qt6 Python setup
- 2026-02-26 da66436a ci: add GTK3, FreeBSD jobs and .deb packaging
- 2026-02-26 bef51349 ci: add Windows CI job with MSVC + vcpkg + Qt6
- 2026-02-26 a1ad2d71 ci: switch to ubuntu-24.04 hosted runner with full deps
- 2026-02-26 013bbbb5 ci: install libgl-dev for OpenGL headers on self-hosted runner
- 2026-02-26 83645d9d fix: add missing break in DecorationRole switch cases
- 2026-02-26 3cced670 fix: emoticon dialog sizing, share browser title, progress bar rendering
- 2026-02-25 44441671 docs: update README for Qt6, add GitHub Actions CI workflow
- 2026-02-25 2bbf5094 refactor: drop Qt5 support, Qt6-only build
- 2026-02-25 c0a0e1c1 chore: remove plan file from version control
- 2026-02-25 141d422e Add Joe Rivera copyright to all modified source files
- 2026-02-25 c25ff542 Fix hang on exit: explicitly call qApp->quit() in closeEvent
- 2026-02-25 abe1c31e Symlink icons from source tree into build tree for dev builds
- 2026-02-25 41d116f9 Qt6 build fixes, runtime crash fixes, and icon loading improvements
- 2026-02-22 d9e393aa Phase 6: Modernize SIGNAL/SLOT to new-style connect (~560 connections)
- 2026-02-22 fc7f74b2 Phase 5: QDeclarativeView → QQuickWidget — 100/100 tests on both Qt5 and Qt6
- 2026-02-22 de9945a3 Phase 4: QScriptEngine → QJSEngine port — 100/100 tests on both Qt5 and Qt6
- 2026-02-22 5afba046 Phase 3: Qt6 compiles successfully — all 100 tests pass
- 2026-02-22 ffa9931e Phase 2f: Fix remaining Qt5 deprecation warnings
- 2026-02-21 2d79a114 Phase 2e: Remove Qt4 compat code and deprecated Qt5 patterns
- 2026-02-21 0d49e021 Phase 2d: QSound → QSoundEffect (Qt6-compatible)
- 2026-02-21 0bd53343 Phase 2c: Remove QTextCodec (all dead/unused code)
- 2026-02-21 f07fe3b7 Phase 2b: QRegExp → QRegularExpression (all 25 usages, 12 files)
- 2026-02-21 ed470857 Phase 2a: Simple Qt6 API renames (Qt5-compatible)
- 2026-02-21 ded71fa9 Phase 0.4+1: Add USE_QT6 option + dual Qt5/Qt6 CMake support
- 2026-02-21 55cf5637 Phase 0.3: Remove Qt4 and GTK2 dead code from CMake
- 2026-02-21 cd2596d9 B1: Add regression test suite (100 tests) for Qt6 migration
- 2026-02-21 876c0c7a fix(gtk): replace ConnectivityManager::getInstance() in mainwindow.cc
- 2026-02-21 dd9328ca A4: Remove Singleton<T> from DCContext-owned managers
- 2026-02-21 a1dcfe26 A2.5: Complete dcpp/ getInstance() mop-up
- 2026-02-21 91d12e6a A3.2+A3.4: Thread DCContext through daemon and GTK GUI
- 2026-02-21 9fd55e94 A3: Thread DCContext through Qt GUI layer
- 2026-02-21 e5cb6567 refactor(A2): replace 290 getInstance() calls with ctx()-> in dcpp core
- 2026-02-21 8b828bc2 refactor(A1): DCContext owns all 20 core managers via unique_ptr
- 2026-02-21 939598bf refactor: upgrade to C++20 and modernize core infrastructure
- 2026-02-21 c2fc7efe test(B0): add Catch2 v3 test infrastructure with initial tests
- 2026-02-21 70d08e81 refactor(A0.4): add ContextAware base class to all managers
- 2026-02-21 a974ad30 refactor(A0.3): add empty DCContext skeleton class
- 2026-02-21 1e486cbc refactor(A0.2): fix manager destructor issues
- 2026-02-20 04fef4d5 refactor(A0.1): make all manager constructors/destructors public
- 2026-02-20 fe75b29d fix: enable clean shutdown and re-initialization of dcpp singletons
