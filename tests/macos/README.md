# Native macOS Open Smoke

For a separate build/signing regression (no launch or registration), run
`bash tests/macos/metadata-rebuild.sh /absolute/new-qa-directory Xcode`.
It builds a tiny bundle through the production metadata modules, changes only
the plist template, then switches torrent support off. Both changes must relink
and re-sign with a valid resulting signature. The optional generator also accepts
`Ninja` or `Unix Makefiles`. Logs and generated metadata are retained in QA.

Run from a logged-in macOS GUI session with Xcode command-line tools, CMake,
and Qt6 Widgets installed. This standalone project never uses the shared build.
The first argument must be a **built/generated app Info.plist**, not the source template.
The second must be an absolute, nonexistent QA directory. Prefer the dedicated
`Work/qa/open-handlers-20260908/fixture` location below: the observed `/tmp` run
delivered native events but was not exposed in LaunchServices candidate lists.

```sh
REPO="$(git rev-parse --show-toplevel)"
export REPO
bash "$REPO/tests/macos/smoke.sh" \
  "$REPO/build-torrent/eiskaltdcpp-qt/EiskaltDC++.app/Contents/Info.plist" \
  "$REPO/Work/qa/open-handlers-20260908/fixture/native-green-2"
```

Override `CMAKE_PREFIX_PATH` if Qt6 is not at `/opt/homebrew/opt/qt`.
The shared built plist is only read and copied. Everything built is in QA.
The standalone CMake entry point also accepts `-DREPO=/absolute/source/checkout`.
Append `--no-torrent` to test torrent-disabled metadata: the receiver is built
without `USE_TORRENT`, torrent candidacy must be absent, and only the four
scheme cold/warm pairs run. No torrent URL is delivered in this mode.

## Assertions

- A temporary UUID-identified `.app` copies all supplied metadata, changing only
  bundle identifier, executable, name, and display name (if originally present).
  It does not invent missing URL/document declarations.
- LaunchServices must list that exact fixture for `dchub`, `adc`, `nmdcs`,
  `adcs`, `.torrent`, plus other schemes/extensions/content types declared in
  the input metadata. Absent declarations in an old bundle produce a failing
  run even when explicit-app delivery works. Wildcard extensions are not probed.
- Five cold/warm pairs each start an independent native Cocoa QApplication.
  Both opens in a pair must reach the same PID. `QFileOpenEvent` feeds the actual
  production `PendingOpenEvents::nativeSource` and `enqueue` implementation,
  compiled with `USE_TORRENT`. No copied normalization implementation is used.
- The first received event starts an 800 ms unready interval. The driver waits
  for its queued delivery before opening the same source again while running.
  Both sources must be exact, with one delivery per open; cold receipt must be
  unready, warm receipt ready, and all deliveries ready.
- The local filename includes spaces, literal `#`, and literal `%`. The minimal
  bencoded fixture is never parsed or downloaded. Hub URLs use `.invalid`.
- The receiver exits after two deliveries plus a 700 ms duplicate-observation
  interval, or after its 15 s watchdog. The driver never kills any process.
- Cleanup unregisters only the fixture, removes its `.app`, checks that its
  path and bundle ID are absent from candidates, and compares default handlers
  before/after. There are no default-setting APIs or commands in the harness.

## Evidence And Limits

`summary.json` is the verdict, `driver.log` contains checks, and one JSONL report
per source contains event/source/native file/native URL/PID/readiness/acceptance.
`source.Info.plist`, `fixture.Info.plist`, `inputs.sha256`, and compiler logs
preserve provenance. A nonzero exit means an assertion or build failed. Failed
registration checks do not skip native delivery diagnostics. Native failures
still proceed through cleanup. A force-killed driver or machine shutdown cannot
guarantee cleanup; `summary.json` records the fixture path when it completes.
For an interrupted run, unregister only its `NativeOpen-<UUID>.app` using
`/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -u APP_PATH`
after the receiver watchdog has expired, then remove that disposable bundle.

This covers OS delivery into Qt and the production pending queue, not the full
Eiskalt application, its single-instance wrapper, profile migration, windows,
connection logic, torrent parsing, or actual installed-default dispatch. Qt is
dynamically linked from the local installation, not deployment-tested. The
fixture passes an isolated HOME/config environment and instantiates no Eiskalt
profile/settings code. NSWorkspace always receives an explicit fixture app and
does not add recent items. Existing unrelated defaults are accepted unchanged;
the parent task owns installed-app registration/default readback.

## Verified Run (2026-09-08)

The command above was run with output suffix `native-green-1` against the newly
generated torrent-enabled built plist. It exited 0: all five cold/warm pairs,
candidate checks, self-exit, unregister/removal, and unchanged defaults passed.
The QA directory retains the complete summary, ten receipts and ten deliveries
across five JSONL files, metadata snapshots, and build logs. Swift emits
a deprecation warning for the read-only legacy scheme-default query.

The parent separately reported its red check against the actual installed old
app: all four scheme candidates and `.torrent` absent. Its preserved
`Work/qa/open-handlers-20260908/associations-before.json` records four null scheme
defaults and qBittorrent for torrent. The sandboxed harness attempt in
`/tmp/eiskalt-native-open-red-20260908-2` failed to launch and is **not** native
coverage or a valid metadata-only red check. The first unsandboxed `/tmp` run
preserves candidate failures and the path-alias assertion subsequently fixed
by canonicalizing the QA path. No production code was changed for those fixes.

Run with native GUI/LaunchServices access outside a restrictive execution
sandbox. The harness does not weaken sandbox settings itself. The parent owns
generation of ON/OFF plists; this harness reads their resulting metadata rather
than reimplementing that generation.

Final variant invocations (both exited 0; use new output suffixes when repeating):

```sh
REPO="$(git rev-parse --show-toplevel)"
bash "$REPO/tests/macos/smoke.sh" \
  "$REPO/Work/qa/open-handlers-20260908/ON.plist" \
  "$REPO/Work/qa/open-handlers-20260908/fixture/native-on-1"
bash "$REPO/tests/macos/smoke.sh" \
  "$REPO/Work/qa/open-handlers-20260908/OFF.plist" \
  "$REPO/Work/qa/open-handlers-20260908/fixture/native-off-1" --no-torrent
```

ON passed 56 assertions and ten native opens. OFF passed 49 assertions and
eight native opens, with no torrent candidate or torrent event report. Both
verified self-exit, unregister, candidate removal, and unchanged defaults.
