# AntiSpam Per-File Quit Recovery

Status: Task1 is verified and committed as `09a16b8f`; Task2's bounded final
cold-build, matrix, preservation and evidence-seal acceptance is verified.
The documentation checkpoint records that scope only. Nothing is
installed, signed, pushed, published or activated by this repair.

## Scope And Behavior

The approved Native/inline repair retains one GUI-owned frozen snapshot for
`antispam`, `blacklist`, `graylist` and `whitelist`. Each attempt owns copied
paths, UTF-8 bytes, the prior four-file committed mask and snapshot/attempt UUIDs.
The worker skips committed files without opening them and records new success
only after a full write and checked `QSaveFile::commit()`. One file's failure
does not discard other files' receipts.

Both direct saves and asynchronous quit recovery use the same writer and
receipt validator. Pending admission refuses duplicate/direct writers. Ready
is cached without I/O. Paths and serialization are never recaptured for retry.
Incomplete capture, missing/foreign/regressing receipts and owner loss refuse
quit terminally; a valid partial result or proven unstarted launch permits only
explicit retry. Exceptional future readiness is not physical thread-exit proof:
the coordinator retains that owned job rather than joining it on a GUI poll.
Forced coordinator destruction can still join, as allowed by the approved spec.

Search-blacklist, GUI/core settings and primary Torrent/sharing/proxy receipts
are not replayed by auxiliary recovery. Recovery permanently disarms previously
consented update installation. Public session APIs, existing quit wording,
deadlines, polling and ordinary AntiSpam save/load behavior are unchanged.

## Behavioral RED And Focused GREEN

The retained unchanged-production direct and composed consumers fail exactly
12 marker-preservation assertions each, on both CPUs: every ordered blocked /
successful-file pair in the four-by-three set, zero errors/skips. Separate
frozen-path RED refuses the missing original destination. Initial malformed
assertion/harness receipts remain retained and are not accepted as clean RED.

Focused repaired consumers pass **62 case instances / 3,000 assertions**:
per CPU, direct 9/261, composed settings 9/367, held lifecycle 7/520 and six
single-case private controls totaling 352 assertions. Fresh Task1 confirmation
passes 36/1,256 before the commit and again after it. This includes exact frozen
serialization, all four file failures, successive partial masks, marker bytes
and inode/device preservation, syscall write counts, cached Ready, held write /
flush / thread exit, owner destruction/replacement, real commit refusal,
unstarted launch rollback, terminal receipt faults and capture/copy failure.

## Review And Test-Collector Disposition

One independent reviewer inspected the immutable ten-path candidate, not the
mutable checkout or unrelated Torrent repairs. Two P2 findings were accepted
as Important: auxiliary owner identity was bound too late, and normal CTest
selection omitted four composed recovery cases. Held-primary owner replacement
RED has exactly nine failures, including unintended fake installer calls and
replacement writes; CTest discovery proves all four omissions. Binding at the
first freeze and expanding the normal selector fix both. Matching-CPU held
regressions and actual CTest discovery pass. Hidden controls remain opt-in.

Two new test collectors initially blocked their own GUI with `watchdog.get()`.
The original ARM early-exception failure and Intel two-failure owner hold remain
retained. A private Intel trace reproduces a 348-350ms collector block with
unchanged heartbeat ticks. Event-pumped waits for probe release precede those
two collectors; the 350ms hold, 100ms request, 5000ms helper and every original
assertion remain. Only the shared test object was relinked for focused testfix2
acceptance; product objects are unchanged. Final cold consumers include both
fixture corrections.

The executing-plans workflow uses one independent review and one RED/GREEN fix
pass, not a second review. The review did not independently audit runtime,
private relink/interposer tooling, full provenance, unrelated repairs or release
acceptance. Those limits are not waived by the source verdict.

## Evidence Identity

Private evidence root:
`Work/qa/antispam-per-file-recovery-20261007/`.

- Preflight HEAD: `4da986721f3fac3a8ff6fb23d7abf29a80098871`.
- Original seven-file dirty patch SHA-256:
  `6dfa0ef2233ef73f36877907090a229d3a39a5c1d3c9c3847e3460676bfda6c2`.
- Final source capture: 3,496 files at Task1 checkpoint, plus the preserved local
  repair hunks; manifest SHA-256
  `8c70947ce70ee0f25f30328a76b8ba698d8e83b62efec8667fdd7e01b4515661`.
- Focused result SHA-256:
  `dd74d71f76d238701d500b620e2f76f622e1e7f73ed19680832b36cfaba353f9`.
- Exact RED inventory SHA-256:
  `bc145d2f697593708606cd19bf7f3da95747535381ed5df782459ccf057e48e6`.
- Each newer run retains binary SHA-256, exact selector, seed `594128903`,
  layout, matching interposer/wrapper identities and source manifest in its
  `*-inputs.json`; XML, log and exit receipts are separate and never overwritten.
- Tests use disposable HOME/XDG/TMP, offscreen Qt, accessibility disabled and
  loopback-only routing with direct external-connection denial checked first.
  No timed test overlaps compilation or heavy hashing.
- Host is ARM/macOS27; Intel runs through Rosetta. Mach-O targets are ARM26.0 and
  Intel14.0. QA internal build number is 1, not a production allocation.

Eight final consumers were cold-built. Paths below are under the corresponding
`<cpu>-final-build/` directory in the private evidence root; no-Torrent is ARM.

| CPU / Binary | SHA-256 |
| --- | --- |
| arm64/tests/qt/eiskaltdcpp-qt-tests | `a3cc000b86f8b075a05f675415fb1d4edb1d01268bbeaf9c4b00907e5cac9cec` |
| arm64/eiskaltdcpp-qt/eiskalt-settings-quit-tests | `e27a6499f738c36559335c5e9ee0808457fc2cdbdd82785c36e151e283fda866` |
| arm64/eiskaltdcpp-qt/eiskalt-mainwindow-toggle-tests | `ad5999acf228daf0144c95cf11b7829df1f5fb5eafc2315010e5c683c6e66e61` |
| x86_64/tests/qt/eiskaltdcpp-qt-tests | `0aa444aa5c61b7621dbc4392ac827ab9f3bd4f04950f1372135273761c71c88c` |
| x86_64/eiskaltdcpp-qt/eiskalt-settings-quit-tests | `fa397e0e3d981874e35ca995dcdd91e4816d387cfab1f7a7523254ee9ecea71b` |
| x86_64/eiskaltdcpp-qt/eiskalt-mainwindow-toggle-tests | `f32d6cdf3f72ac128bc9dc045d1704eec1492e5bba50c9c633dcd6e03d081cd8` |
| no-torrent/tests/qt/eiskaltdcpp-qt-tests | `d216d053c2579f1ad5b5f52a4f0d651dea2deadaa31ccc5428c9e321811d17ee` |
| no-torrent/eiskaltdcpp-qt/eiskalt-settings-quit-tests | `bcc5e49506eba9cecc3141310a3dd2d1e5a37c0d264bba9e5f1ae6a63e1ae80d` |

## Final Acceptance And Remaining Gates

The complete serial matrix passes **44 runs / 2,975 case instances / 137,048
assertions**, zero failures/errors/skips/fatal exits. All 28 prior ordinary and
hidden inventory comparisons match, including four no-Torrent retirement
inventories and the established-relay case. Regular selectors are `~[interop]`
for Qt, `[settings-quit],[antispam-file-recovery]` for settings, and all three
GUI layouts (sidebar retains `~[tab-navigation-mainwindow]`). Hidden selectors
cover settings storage/write/ready/admission, composed Torrent/settings freeze,
existing auxiliary writes and new AntiSpam lifecycle checks. Six private control
cases run separately per CPU; no control binary selects unrelated expectations.
This is not a new full core/Torrent matrix or whole-application rebuild.

Fresh final confirmation adds 32 case instances / 1,774 assertions, comparing
both settings and GUI lifecycle inventories on both CPUs. All original seven
repair-file bytes remain intact. Protected-state readback exactly matches
preflight: both historical whole bundle trees/strict signatures, five archive /
notice pins, 58 provider resources and the 9/17/8/3 protected/negative manifests.
The retained current-matrix and established-relay evidence manifests reread all
5,875 entries successfully. These historical checks do not certify a new release.

The complete new evidence seal creates and rereads **37,114 file hashes**.
`final-matrix-summary.json` SHA-256:
`a05a84c349206fc23b27ddac000006ca813f05c30f6ee04bb05e27a670b3a37f`.
`final-seal.json` SHA-256:
`e6041e5628d1a8c71df17c044a26243c3640ae515500830cf0f925c55f0559de`.
Post-commit completion additionally reruns both composed settings consumers;
those receipts are separately retained/sealed in this plan's SDD workspace,
without modifying the sealed QA root. The ledger records their actual outcome.

## Execution Rulings

- Keep the Native checkout and original local repairs. Cost: shared-checkout
  risk, bounded by exact before/after file and index checks.
- Build only Qt/settings at focused RED/GREEN and defer full GUI to final.
  Cost: early GUI coverage is deferred, not omitted from final acceptance.
- Treat the active-recipe EOF as a harness failure; retain its failed log and
  verify unchanged captured targets separately. Cost: a separate recovery
  receipt is needed; never edit an active recipe again.
- Review immutable source concurrently with cold builds. Cost: source review
  is not runtime certification; final matching-CPU checks remain mandatory.
- Preserve original GREEN and create a fresh review-fix phase. Cost: additional
  disk evidence; accepted Important findings get exact RED/GREEN and final tests,
  not a second reviewer under the chosen executing-plans workflow.
- Commit the complete new composed test source in Task1, including opt-in
  Task2 cases. Cost: source presence alone must not be confused with acceptance.
- Privately relink the early-exception collector correction. Cost: extra test
  provenance; original budgets/assertions and production objects stay intact.
- Add event-pumped release collection to the pending-owner case after its
  traced heartbeat failure. Cost: one extra test check per section, not a relaxed
  deadline or production workaround. Both CPUs and final cold consumers pass.

No Minor finding is deferred by this scoped review. All unjudged runtime,
tooling/provenance and whole-release boundaries remain explicit, not waived.

## Still Pending

Historical bind failures and 102/180/354ms latency/natural recovery causes remain
unwaived. Broader lifecycle/real peer/bootstrap/selected-check, native Intel and
minimum-OS/live/benchmark/soak, localization/native quality, complete dependency
provenance/reproducibility and final whole updater/prerelease review remain open.
Reviewed whole-application/material builds, numeric production build allocation,
signing/notarization, signed-v3 acceptance, guarded closed-app installation and
authenticated GitHub/wiki/screenshots/prerelease publication/readback remain
separate gates. Established GOST relay-across-Fence coverage was already closed
at `9b88fab06`; this repair must retain it, not relabel it as deferred.
Production signed feeds remain last.
