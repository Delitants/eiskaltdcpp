# macOS Cocoa stability patches

Qt 6.11.1's Cocoa tray delegate reads `NSApp.currentEvent.clickCount` without
checking the event type. On macOS 27 a status-menu gesture can arrive while
the current event is not a mouse event, causing an Objective-C exception.

`macos27-tray-event.patch` backports the guard from Qt's development branch.
It uses the existing `cocoaEvent2QtMouseEvent(event) == QEvent::None` directly;
the newer upstream `qt_mac_isMouseEvent` is its inverse. No Qt framework or
system-wide Homebrew files are modified.

Upstream references (retrieved 2026-09-26):

- https://github.com/qt/qtbase/blob/dev/src/plugins/platforms/cocoa/qcocoasystemtrayicon.mm
- https://github.com/qt/qtbase/blob/dev/src/plugins/platforms/cocoa/qcocoahelpers.mm

## Rebuild

Use a **Qt 6.11.1** installation with private headers and a matching source
archive. The standalone target keeps the upstream Cocoa sources and cursor
resources, with only the supplied event-guard patch. Qt's source retains its
original license headers; retain the source archive and patch with QA artifacts.
This is an arm64 local QA build, not verification of macOS 14 compatibility.

`accessibility-table-lifetime.patch` is a local Qt 6.11.1 correction, not an
upstream backport. Synthesized accessibility rows, columns and placeholder
cells borrow their table's ID. Their cleanup must not unregister the live
table or surviving cells: Qt's model/cache, not native row representations,
owns the interfaces. Materialized cell coordinates follow persistent model
indexes after row/column changes. Retired synthetic descendants are invalidated
before their arrays are released, including when macOS retains an old row.
The patch also resolves selected interfaces by cached ID between native
element conversions rather than retaining raw pointers across those calls.
Accessibility remains enabled. The existing tray-event patch is still required.

```sh
ROOT="$(git rev-parse --show-toplevel)"
QA="$ROOT/Work/qa/cocoa-crash-20260926"
# Download only if not already present.
curl -fL https://github.com/qt/qtbase/archive/refs/tags/v6.11.1.tar.gz \
  -o "$QA/qtbase-v6.11.1.tar.gz"
echo "e20852bd45cdef5da5175f3634e285e03e2be7ca437f3d2b7e1a2af7321bca7a  $QA/qtbase-v6.11.1.tar.gz" \
  | shasum -a 256 -c -
# Extract and patch once into a fresh source directory.
tar -xzf "$QA/qtbase-v6.11.1.tar.gz" -C "$QA"
patch -d "$QA/qtbase-6.11.1" -p1 < "$ROOT/macos/qt-cocoa-backport/macos27-tray-event.patch"
patch -d "$QA/qtbase-6.11.1" -p1 < "$ROOT/macos/qt-cocoa-backport/accessibility-table-lifetime.patch"
cmake -S "$ROOT/macos/qt-cocoa-backport" -B "$QA/plugin-build" \
  -DQTBASE_SOURCE="$QA/qtbase-6.11.1" \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build "$QA/plugin-build" -j2
```

## Native Regression

Run in a logged-in GUI session. This fixture uses no Eiskalt profile or network.
It posts the real Cocoa tray-menu notification with controlled current events,
and verifies both the non-mouse guard and normal click activation signals.
Only the synchronous test temporarily substitutes `NSApplication.currentEvent`.

```sh
cmake -S "$ROOT/tests/macos/cocoa-tray" -B "$QA/test-build" \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
cmake --build "$QA/test-build" -j2
QT_ACCESSIBILITY=0 QT_QPA_PLATFORM=cocoa QT_DEBUG_PLUGINS=1 \
  "$QA/test-build/cocoa_tray_regression" "$QA/plugin-build/plugins"
```

The original installed plugin must fail on the application-defined event;
the patched plugin must pass. The fixture's plugin-root argument prevents
accidentally testing another platform plugin from Homebrew.

## Packaging

Copy `plugin-build/plugins/platforms/libqcocoa.dylib` into the app's
`Contents/PlugIns/platforms` **after** macdeployqt and before signing. Keep
`@loader_path/../../Frameworks` as its only Qt framework runpath.
Verify the bundle's signature and plugin hash, and rerun the native fixture
against the staged bundle's `Contents/PlugIns` before replacing the installed app.
Back up the old bundle and require Eiskalt to be closed before replacement.

For subsequent application rebuilds set `EISKALT_QCOCOA_PLUGIN_OVERRIDE` to
the absolute patched plugin path and `EISKALT_QCOCOA_PLUGIN_SHA256` to the
verified plugin hash. Configuration checks its hash, architecture and Qt
framework dependencies; changes also invalidate the app's packaging step.
Clear that override when moving to a Qt
version containing the upstream fix; this backport must not load against a
different Qt version.

## Accessibility Regression

`tests/macos/cocoa-accessibility` uses generated table/tree data, without an
Eiskalt profile or network. It checks borrowed identity ownership before using
the native table bridge, and exits on failure rather than continuing with an
invalid interface. It also checks row selection, model updates after explicit
autorelease-pool drainage, empty selection, tree selection, retained retired
rows, native parent coordinates, placeholder promotion, row/column shifts,
model reset and view teardown with accessibility enabled. Run this in addition to the tray
fixture above; a passing tray fixture alone does not cover accessibility.

```sh
cmake -S "$ROOT/tests/macos/cocoa-accessibility" -B "$QA/accessibility-test-build" \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
cmake --build "$QA/accessibility-test-build" -j2
"$QA/accessibility-test-build/cocoa_accessibility_regression" "$QA/plugin-build/plugins"
```

For installed-bundle tests, put the fixture in a separate app-style harness
`Contents/MacOS` directory and use the bundle's frameworks so that its
`@executable_path` dependencies resolve correctly. The fixture verifies the
actual loaded Cocoa plugin path. Never add test executables to the signed
production application. Retest both patches when updating Qt; do not carry
these changes onto a different Qt version without review.
