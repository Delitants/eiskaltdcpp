# Download-To Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:test-driven-development` for each task and `superpowers:verification-before-completion` before claiming completion.

**Goal:** Add visible, standard Add and Remove controls to the Downloads > Download to preferences tab without changing its persisted data format.

**Architecture:** Extract Download-to entry serialization into a small testable helper. The buttons and existing context menu call the same add/remove slots, and every mutation rewrites both persisted base64 lists atomically from the current table contents.

**Tech Stack:** Qt 6 Widgets, C++17, Catch2, existing WulforSettings keys and icon theme.

---

## Task 1: Extract and test Download-to persistence

**Files:**
- Create: `eiskaltdcpp-qt/src/DownloadToSettings.h`
- Create: `eiskaltdcpp-qt/src/DownloadToSettings.cpp`
- Create: `tests/qt/test_downloadtosettings.cpp`
- Modify: `tests/qt/CMakeLists.txt`

- [ ] Define:

```cpp
struct DownloadToEntry {
    QString path;
    QString alias;
    bool operator==(const DownloadToEntry& other) const {
        return path == other.path && alias == other.alias;
    }
};

QList<DownloadToEntry> decodeDownloadTo(const QString& encodedPaths,
                                        const QString& encodedAliases);
QPair<QString, QString> encodeDownloadTo(const QList<DownloadToEntry>& entries);
```

- [ ] Write tests for empty settings, one entry, Unicode alias/path round trip, mismatched path/alias counts, and deletion followed by re-encoding.
- [ ] Preserve the existing `WS_DOWNLOADTO_PATHS` / `WS_DOWNLOADTO_ALIASES` base64 representation and separator semantics exactly.
- [ ] Run:

```bash
cmake --build build-test --target eiskaltdcpp-qt-tests -j4
QT_PLUGIN_PATH="$PWD/builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/Contents/PlugIns" \
QT_QPA_PLATFORM=cocoa \
build-test/tests/qt/eiskaltdcpp-qt-tests "[downloadto]"
```

Expected: helper tests pass.

- [ ] Commit:

```bash
git add eiskaltdcpp-qt/src/DownloadToSettings.h eiskaltdcpp-qt/src/DownloadToSettings.cpp \
  tests/qt/test_downloadtosettings.cpp tests/qt/CMakeLists.txt
git commit -m "Extract Download-to settings serialization"
```

## Task 2: Add visible Add and Remove buttons

**Files:**
- Modify: `eiskaltdcpp-qt/ui/UISettingsDownloads.ui:240-280`
- Modify: `eiskaltdcpp-qt/src/SettingsDownloads.h`
- Modify: `eiskaltdcpp-qt/src/SettingsDownloads.cpp:105-240`

- [ ] Add a bottom `QHBoxLayout` below `treeWidget` with a stretch followed by:
  - `pushButton_DOWNLOADTO_ADD`, text `Add`;
  - `pushButton_DOWNLOADTO_REMOVE`, text `Remove`.
- [ ] Use `WulforUtil::eiEDITADD` and `WulforUtil::eiEDITDELETE` from the selected icon theme.
- [ ] Replace `slotDownloadTo()` with shared operations:
  - `slotAddDownloadTo()` asks for alias, then directory; cancellation changes nothing;
  - `slotRemoveDownloadTo()` removes selected rows after confirmation;
  - `slotDownloadToMenu(const QPoint&)` constructs Add/Remove actions and delegates to the same slots;
  - `slotDownloadToSelectionChanged()` enables Remove only when at least one row is selected;
  - `saveDownloadToEntries()` rewrites both setting keys using the helper.
- [ ] Ensure the buttons remain visible in light/dark themes and use normal disabled-state styling.
- [ ] Update the context menu to use the same translated text and icons as the buttons.
- [ ] Run helper tests and build the full application.

Expected: the Download-to tab shows Add/Remove, Remove is disabled with no selection, and both buttons and context menu persist identical results.

- [ ] Refresh source strings:

```bash
./update-translations.sh tr_up
```

Expected: `.ts` files contain `Add` and `Remove` contexts; no obsolete unrelated strings are deleted.

- [ ] Commit:

```bash
git add eiskaltdcpp-qt/ui/UISettingsDownloads.ui eiskaltdcpp-qt/src/SettingsDownloads.h \
  eiskaltdcpp-qt/src/SettingsDownloads.cpp eiskaltdcpp-qt/translations/*.ts
git commit -m "Add Download-to list controls"
```

## Task 3: Install and visually verify

- [ ] Build release:

```bash
cmake --build builddir-release --target eiskaltdcpp-qt -j4
```

- [ ] Install the generated application using the established bundle-preserving command:

```bash
rsync -aE --delete builddir-release/eiskaltdcpp-qt/EiskaltDC++.app/ /Applications/EiskaltDC++.app/
```

- [ ] Launch Preferences > Downloads > Download to and verify Add, Remove, selection state, context menu parity, persistence after restart, and light/dark appearance.
