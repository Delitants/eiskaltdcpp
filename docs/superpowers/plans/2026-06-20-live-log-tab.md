# Live Log Tab Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:test-driven-development` for each task and `superpowers:verification-before-completion` before claiming completion.

**Goal:** Add a main-window Live Log action that opens a singleton tab with category filtering, pause/resume, clear, and auto-scroll controls.

**Architecture:** Extend `LogManager` with a bounded, thread-safe structured live-entry stream while preserving existing file logging and `Message` listeners. A Qt table model receives structured entries, applies a persisted category mask, and backs a singleton `ArenaWidget` created lazily by `QtContext` and toggled from the Tools menu/toolbar.

**Tech Stack:** C++17, Qt 6 Widgets/model-view, EiskaltDC++ Speaker listeners, Catch2/Qt Test.

---

## Task 1: Add structured live entries to LogManager

**Files:**
- Modify: `dcpp/LogManagerListener.h`
- Modify: `dcpp/LogManager.h`
- Modify: `dcpp/LogManager.cpp`
- Modify: `dcpp/DownloadManager.cpp`
- Modify: `dcpp/UploadManager.cpp`
- Modify: `dcpp/QueueManager.cpp`
- Modify: `dcpp/DebugManager.h`
- Modify: `eiskaltdcpp-qt/src/HubFrame.cpp`
- Modify: `eiskaltdcpp-qt/src/SpyModel.cpp`
- Create: `tests/qt/test_logmanager_live.cpp`
- Modify: `tests/qt/CMakeLists.txt`

- [ ] Add an immutable entry type:

```cpp
struct LogEntry {
    uint64_t sequence;
    time_t timestamp;
    LogManager::Area area;
    string message;
};
```

- [ ] Add `LogManagerListener::EntryAdded` carrying one `LogEntry`.
- [ ] Keep `LogManagerListener::Message` unchanged so the main status bar and existing consumers do not regress.
- [ ] Add `getLiveEntries() const`, `clearLiveEntries()`, and a bounded deque with a 5,000-entry maximum. Assign monotonically increasing sequence numbers under the existing critical section.
- [ ] Replace `log(Area, ParamMap&)` with `log(Area, ParamMap&, bool writeToFile)`. It formats once, always records/fires the structured entry, and writes to disk only when `writeToFile` is true.
- [ ] Refactor every current producer so it always constructs/publishes the entry and passes its existing file-log condition as `writeToFile`:
  - `DownloadManager`: preserve `LOG_DOWNLOADS` and file-list filtering for disk only;
  - `UploadManager`: preserve `LOG_UPLOADS` and file-list filtering for disk only;
  - `QueueManager`: preserve `LOG_FINISHED_DOWNLOADS` and file-type filtering for disk only;
  - `DebugManager`: preserve `LOG_CMD_DEBUG` for disk only;
  - `HubFrame`: preserve `LOG_PRIVATE_CHAT`, `LOG_MAIN_CHAT`, and `LOG_STATUS_MESSAGES` for disk only;
  - `SpyModel`: preserve `LOG_SPY` for disk only.
- [ ] Add a compile-time migration guard by removing the old two-argument overload, so any missed gated producer fails to compile instead of silently disappearing from Live Log.
- [ ] Ensure `message()` records exactly one `SYSTEM` entry and still emits exactly one `Message` event. It must not duplicate a system message through two internal paths.
- [ ] Write tests for area retention when `writeToFile=false`, no file creation in that case, monotonic sequence, 5,000-entry eviction, clear behavior, and one-event compatibility for `message()`.
- [ ] Run:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "[livelog][core]"
```

Expected: structured history and compatibility tests pass.

- [ ] Commit:

```bash
git add dcpp/LogManagerListener.h dcpp/LogManager.h dcpp/LogManager.cpp \
  dcpp/DownloadManager.cpp dcpp/UploadManager.cpp dcpp/QueueManager.cpp dcpp/DebugManager.h \
  eiskaltdcpp-qt/src/HubFrame.cpp eiskaltdcpp-qt/src/SpyModel.cpp \
  tests/qt/test_logmanager_live.cpp tests/qt/CMakeLists.txt
git commit -m "Add structured live log stream"
```

## Task 2: Build and test the Live Log model

**Files:**
- Create: `eiskaltdcpp-qt/src/LiveLogModel.h`
- Create: `eiskaltdcpp-qt/src/LiveLogModel.cpp`
- Create: `tests/qt/test_livelogmodel.cpp`
- Modify: `tests/qt/CMakeLists.txt`

- [ ] Implement a `QAbstractTableModel` with columns Time, Category, Message.
- [ ] Store `dcpp::LogEntry` values and expose:

```cpp
void appendEntry(const dcpp::LogEntry& entry);
void replaceEntries(const dcpp::LogManager::EntryList& entries);
void clear();
void setCategoryMask(quint32 mask);
quint64 lastSequence() const;
```

- [ ] Map all areas to stable bit positions and translated display names: Chat, Private messages, Downloads, Finished downloads, Uploads, System, Status, Search spy, Command debug.
- [ ] Filter without discarding core history: changing the mask must repopulate visible rows from entries still held by the model, and paused UI refresh will re-read core history by sequence.
- [ ] Write tests for column data, category masks, sequence ordering, replacement, and clear.
- [ ] Run the model tests and commit:

```bash
git add eiskaltdcpp-qt/src/LiveLogModel.h eiskaltdcpp-qt/src/LiveLogModel.cpp \
  tests/qt/test_livelogmodel.cpp tests/qt/CMakeLists.txt
git commit -m "Add category-filtered Live Log model"
```

## Task 3: Create the singleton Live Log ArenaWidget

**Files:**
- Create: `eiskaltdcpp-qt/ui/UILiveLog.ui`
- Create: `eiskaltdcpp-qt/src/LiveLog.h`
- Create: `eiskaltdcpp-qt/src/LiveLog.cpp`
- Modify: `eiskaltdcpp-qt/src/ArenaWidget.h`
- Modify: `eiskaltdcpp-qt/src/WulforSettings.h`
- Modify: `eiskaltdcpp-qt/src/QtContext.h`
- Modify: `eiskaltdcpp-qt/src/QtContext.cpp`

- [ ] Add `ArenaWidget::LiveLog` before `NoRole`.
- [ ] Add `WS_LIVE_LOG_CATEGORIES = "live-log/categories"` with a default mask containing every category.
- [ ] Design `UILiveLog.ui` with a compact top row:
  - Categories `QToolButton` with a menu of checkable categories and All/None actions;
  - checkable Pause button;
  - Clear button;
  - checked Auto-scroll checkbox;
  - `QTableView` filling the remainder.
- [ ] Implement `LiveLog` as `QWidget`, `ArenaWidget`, `QtContextAware`, and `LogManagerListener` with `Singleton | Hidden` state.
- [ ] Marshal `EntryAdded` callbacks to the GUI thread with a queued signal. Never mutate the Qt model from a core worker thread.
- [ ] Pause behavior: stop appending to the model only. On resume, call `getLiveEntries()`, append entries whose sequence exceeds `model.lastSequence()`, then apply the selected category mask.
- [ ] Clear behavior: call both `LogManager::clearLiveEntries()` and `model.clear()`; do not delete log files.
- [ ] Auto-scroll only when the user was already at the bottom before appending.
- [ ] Add lazy ownership to `QtContext`: forward declaration, `createLiveLog()`, accessor, and `unique_ptr<LiveLog>`.
- [ ] Build the application to validate `moc`/`uic` discovery. The existing CMake globs should include the new `.cpp`, `.h`, and `.ui` automatically after reconfiguration:

```bash
cmake -S . -B builddir-release
cmake --build builddir-release --target eiskaltdcpp-qt -j4
```

- [ ] Commit:

```bash
git add eiskaltdcpp-qt/ui/UILiveLog.ui eiskaltdcpp-qt/src/LiveLog.h \
  eiskaltdcpp-qt/src/LiveLog.cpp eiskaltdcpp-qt/src/ArenaWidget.h \
  eiskaltdcpp-qt/src/WulforSettings.h eiskaltdcpp-qt/src/QtContext.h \
  eiskaltdcpp-qt/src/QtContext.cpp
git commit -m "Add Live Log singleton tab"
```

## Task 4: Add the main control action and persistence

**Files:**
- Modify: `eiskaltdcpp-qt/src/MainWindow.h`
- Modify: `eiskaltdcpp-qt/src/MainWindow.cpp`
- Modify: `eiskaltdcpp-qt/translations/*.ts`

- [ ] Add `toolsLiveLog` to `MainWindowPrivate` and create it with `WulforUtil::eiOPEN_LOG_FILE`.
- [ ] Add translated text `Live Log`, status tip `Show live application log`, and a `slotToolsLiveLog()` that calls `toggleSingletonWidget(widgetForRole(ArenaWidget::LiveLog))`.
- [ ] Add it to the Tools menu and the configurable main toolbar action list.
- [ ] Add a `widgetForRole(ArenaWidget::LiveLog)` case that creates/registers the singleton via `QtContext`, sets the action as its tool button, and returns it.
- [ ] Ensure restoring a saved tab role recreates Live Log correctly after restart.
- [ ] Run `./update-translations.sh tr_up`, then review only source-location/new-string changes before staging translation files.
- [ ] Run complete deterministic tests and build release.
- [ ] Commit:

```bash
git add eiskaltdcpp-qt/src/MainWindow.h eiskaltdcpp-qt/src/MainWindow.cpp \
  eiskaltdcpp-qt/translations/*.ts
git commit -m "Expose Live Log from the main window"
```

## Task 5: Install and exercise the live UI

- [ ] Install:

```bash
rsync -aE --delete builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/ /Applications/EiskaltDC++.app/
```

- [ ] Verify Tools > Live Log opens one tab, repeated activation raises the same tab, each category can be independently toggled, Pause catches up on resume, Clear leaves files untouched, auto-scroll does not yank a user who scrolled upward, and selections persist after restart.
