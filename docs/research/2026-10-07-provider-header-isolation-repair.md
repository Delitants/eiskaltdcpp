# Selected Provider Header Isolation Repair

Status: current-source test-consumer build-input gate **PASS**.
This does not close a crash/shutdown-refusal cause or any release gate.

## Scope

The [retained input audit](2026-10-07-current-static-header-attribution.md) found
shared c-ares headers identifying 1.34.4 in consumers linked to controlled 1.34.8
archives, plus an uncontrolled Apple Silicon MaxMind generated header. The failed
historical audit and sealed consumers are retained unchanged; none was rebuilt
in place. Current installed apps, profiles, providers, server, credentials,
signing keys, release assets, feeds and remote repositories remain unchanged.

The repair applies only to the dcpp consumer's compile headers. It follows the
selected c-ares target's concrete INTERFACE wrapper dependencies, accepts ordinary
and explicit SYSTEM include metadata plus legacy pkg-config -isystem paths, and
copies the selected public header families to a distinct target-owned per-config
build directory. CMake evaluates configuration expressions. Origin/hash receipts
are retained and missing main/companion headers refuse ambient fallback. MaxMind
priority is enabled only when the corresponding library is found.

The stage runs before compilation, preserves unchanged bytes and removes stale
copies. Declared outputs prevent Ninja from reusing objects based on pre-stage
headers. Provider/link properties, imported-system classification, unrelated
headers and sibling targets are not modified. The supported public set is six
c-ares headers and two MaxMind headers. CMake 3.10 single-config paths remain
supported; Ninja Multi-Config requires CMake 3.20+. Generator-wrapped link target
names are an unsupported metadata boundary and fail closed when no selected
main headers can be resolved. Older CMake/Xcode execution remains unverified.

## Verification

Nineteen public behavioral fixtures pass. Retained RED runs reproduced SYSTEM
precedence, companion fallback, configuration metadata, unavailable MaxMind,
upstream wrapper and Ninja stale-object cases. Separate provider directories and
independent poison controls exercise c-ares and MaxMind; siblings and unrelated
headers retain their original order. An immediate-edit Make control exposed
whole-second dependency timestamp granularity; its failure/reproduction remain
retained and the fixture separates edits by a clock tick.
The registered regression fixture requires CMake, a C++ compiler and Ninja.

Three additional Ninja/Ninja Multi-Config checks pass for Release and Debug,
including unchanged object timestamp/hash and per-config provider-copy bytes.
Independent bounded read-only review found no further material findings within
the supported eight-header scope. The reviewer did not run acceptance checks and
did not review the original seven unrelated runtime/test repairs or whole branch.

The final candidate freezes 3,506 source files, including the existing untracked
allocation fixture, plus 28 selected-provider archive/header/exported-CMake
inputs. Five fresh test executables per CPU (core, Qt, Torrent, window and settings)
build successfully. Their Mach-O CPU/floor records match arm64/macOS 26 and
x86_64/macOS 14. On the macOS 27 host, Intel execution is via Rosetta, not native
minimum-OS acceptance. Six headers named by the actual core compiler records
match the controlled providers through eight copied-header origin/byte checks
per CPU. Four consumers per CPU link exactly the selected c-ares/MaxMind archive
pair; the standalone Torrent consumer does not consume those core archives.

Both pure probes report c-ares headers/library 1.34.8 and MaxMind library 1.13.3.
Both CPUs also open the pinned upstream country fixture and return its expected
GB value. Disposable-home, external-network-denied runtime selections pass 97
core cases/1,012 assertions and one explicit missing-country-database case per
CPU. This last Qt control is not a populated-database/flag UI acceptance test.
Both CPUs pass the two registered CTest provider-header/release-profile checks
under the same isolation policy.

The initial Apple Silicon CTest launcher loaded unpatched Homebrew libtorrent
during test discovery and refused a missing selected-recheck symbol. That failed
receipt remains unchanged; the corrected launcher explicitly supplies the
controlled provider. No product library selection or installed provider was
modified to pass it. The final current-source input/readback gate passes; it
does not waive the original Intel shutdown-refusal fixture.

The initial five-consumer Apple Silicon candidate
confirmed c-ares header/library 1.34.8 and MaxMind 1.13.3 but predates review repairs;
it is not final acceptance evidence. Other failed configurations and two manually
interrupted own QA builds are retained and never counted as success.

## Remaining

Whole current-source dependency/resource/notice/material refresh and whole-build
reproducibility are not proven by selected-provider matching. The original Intel
shutdown refusal remains unresolved and unwaived; overall Torrent5, GUI-settings3
and updater7/8 remain open. Historical lifecycle/bind/UDP ownership/freeze/recovery
causes, broader active-download/live/GOST/selected-check/crash/bootstrap/soak and
benchmark acceptance, native Intel macOS 14/Apple Silicon macOS 26, unfinished
translations and native-language quality remain pending.

Independent whole-branch/final prerelease review, final app/material builds and
production numeric allocation, Developer ID signing/notarization, signed-v3
acceptance, guarded closed-app installation/readback, authenticated GitHub/wiki/
screenshots/prerelease publication/readback remain pending. Production signed
feeds are last. Nothing is installed, pushed, signed, activated or published here.
