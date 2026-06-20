# User-List Context Menu Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:test-driven-development` for each task and `superpowers:verification-before-completion` before claiming completion.

**Goal:** Show meaningful icons in the hub user-list context menu while retaining the macOS safeguard that prevents the previous Cocoa menu-image crash.

**Architecture:** Keep the application-wide `Qt::AA_DontShowIconsInMenus` setting on macOS. Explicitly opt in only stable, application-owned actions using `QAction::setIconVisibleInMenu(true)`. Centralize this in a small helper so submenu actions and dynamic user commands are treated consistently and testably.

**Tech Stack:** Qt 6 Widgets, macOS Cocoa platform plugin, existing Wulfor icon theme.

---

## Task 1: Add a safe per-action menu icon helper

**Files:**
- Create: `eiskaltdcpp-qt/src/MenuIconHelper.h`
- Create: `eiskaltdcpp-qt/src/MenuIconHelper.cpp`
- Create: `tests/qt/test_menuiconhelper.cpp`
- Modify: `tests/qt/CMakeLists.txt`

- [ ] Implement:

```cpp
namespace MenuIconHelper {
void enableFor(QAction* action);
void enableFor(QMenu* menu);
}
```

`enableFor(QAction*)` must no-op for null actions and null icons; otherwise call `setIconVisibleInMenu(true)`. `enableFor(QMenu*)` applies the same rule to `menu->menuAction()`.

- [ ] Write tests that set `Qt::AA_DontShowIconsInMenus`, then prove an action with an icon reports `isIconVisibleInMenu() == true`, a null-icon action remains unchanged, and submenu parent actions can be opted in.
- [ ] Run:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "[menuicons]"
```

- [ ] Commit:

```bash
git add eiskaltdcpp-qt/src/MenuIconHelper.h eiskaltdcpp-qt/src/MenuIconHelper.cpp \
  tests/qt/test_menuiconhelper.cpp tests/qt/CMakeLists.txt
git commit -m "Add safe context menu icon helper"
```

## Task 2: Apply icons to every user-list action

**Files:**
- Modify: `eiskaltdcpp-qt/src/HubFrame.cpp:680-820`
- Modify: `eiskaltdcpp-qt/src/WulforUtil.cpp:1719-1805`

- [ ] Remove ineffective `menu->setProperty("iconVisibleInMenu", true)` calls.
- [ ] After constructing `ul_actions`, call `MenuIconHelper::enableFor()` on every action: Browse files, Private Message, Add/Remove Favorite, Grant slot, Copy data, Match Queue, Remove from Queue, and any available operator actions.
- [ ] Apply the helper to submenu parent actions for Copy data, User commands, and Anti-spam.
- [ ] In `WulforUtil::buildUserCmdMenu`, assign the existing generic command/console icon to custom user-command leaf actions, then opt those actions in. This is the confirmed semantic fallback for commands without their own icon.
- [ ] Keep both macOS `AA_DontShowIconsInMenus` sites intact unless a separate verified cleanup proves one is redundant. This change must not globally re-enable native menu icons.
- [ ] Build and run the helper tests.
- [ ] Commit:

```bash
git add eiskaltdcpp-qt/src/HubFrame.cpp eiskaltdcpp-qt/src/WulforUtil.cpp
git commit -m "Show icons in user-list context menus"
```

## Task 3: macOS regression verification

- [ ] Build and install the release application.
- [ ] Open/close the user-list context menu at least 100 times across users with different action availability, including nested Copy data and User commands menus.
- [ ] Switch application focus repeatedly while a context menu is open.
- [ ] Confirm no crash appears in `QCocoaMenuItem::sync()` / `QImage::toCGImage()`, and every visible icon follows the currently selected icon theme.
- [ ] Confirm normal application menus remain iconless under the macOS global safeguard.
