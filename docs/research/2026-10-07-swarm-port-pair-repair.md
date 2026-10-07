# Swarm Fixture Paired Port Reservation Repair

## Scope And Status

The approved bounded fixture repair passes focused, broken-control and complete
ordinary Qt verification on ARM and Intel/Rosetta. No product port, retry,
listener-history, routing or fallback behavior changes. Other fixtures remain
unchanged. Earlier seven-file local retirement repairs remain separate.

Evidence: `Work/qa/swarm-port-pair-repair-20261007/`.
Overall Torrent quit Task5, GUI-settings Task3 and updater Tasks7/8 remain open.
The earlier bind failures remain unwaived; this repair does not identify their
historical conflicting owners. No installation or publication occurs.

## Implementation

`tests/qt/LoopbackPortPair.h` owns IPv4 loopback TCP and exclusive UDP on the same
chosen port. An explicitly occupied UDP candidate is rejected and the tentative
TCP listener is released. Automatic fixture selection tries at most 32 candidates,
advancing only on UDP address-in-use; other setup errors fail immediately. These
are fixture allocation attempts, not production session retries.

Both reservations survive asynchronous metadata creation and model setup.
The seed pair closes immediately before add/seedCreated; the download pair stays
owned until its own add call. Explicit close and Qt member destruction release
both transports. Native competing-socket probes verify ownership after metadata
setup and before downloader handoff.

Each fresh session must confirm TCP and UDP on the expected endpoint. Readiness
waits terminate explicitly on native error; there is no session restart or silent
port movement. The rowsInserted observer is attached before downloader readiness
pumping, preserving observation of fast loopback completion.

The original transfer, byte/hash, hidden-window, persistent-selection, stop/resume
and row-count checks remain intact. Only the two old TCP-only allocation
preconditions become stricter paired-allocation checks. Original isolated route
settings, 30-second polling limits and 20ms pumping interval are unchanged.

## Verification

Tests were added before the helper. RED consumers execute the real original
TCP-only reservation behavior, compiled while the new header was absent.
Both CPUs produce exactly four declared CHECK/CHECK_FALSE failures: occupied
UDP rejection and tentative TCP cleanup, plus immediate and asynchronous UDP
retention. There are no unrelated failures, errors or skips.

GREEN includes the same four regression cases and original v1/hybrid swarm case.
A deliberate UDP claim after release must produce a native bind error, with no
confirmed replacement endpoint. This preserves the unavoidable handoff race as
an explicit failure instead of masking it. The native system-category check is
platform-neutral; Linux/Windows execution is not claimed here.

| Run, Per CPU Unless Noted | Instances | Assertions | Result |
| --- | ---: | ---: | --- |
| Original TCP-only RED | 4 | 28 | Exactly 4 expected failures |
| Focused GREEN, including v1/hybrid transfers | 5 | 119 | Pass |
| Private missing-UDP control | 4 | 28 | Exactly the same 4 RED failures |
| Private caller-release-before-metadata control | 1 | 21 | Exactly 2 ownership REQUIRE_FALSE failures |
| Private omitted-UDP-close control | 4 | 23 | Exactly 2 release/handoff failures |
| Complete ordinary ARM Qt | 760 | 34,289 | Pass |
| Complete ordinary Intel/Rosetta Qt | 760 | 34,277 | Pass |
| Complete ordinary total | 1,520 | 68,566 | Pass |

All expected failures are validated by exact type/expression counters, not count
alone. Complete inventories match between CPUs and retain all previous cases,
adding only the four new reservation cases. No errors, skips or fatal exits occur.

Runs are serial with disposable HOME, offscreen Qt, explicit provider/dyld
readback and verified external-network denial under loopback-only Seatbelt.
The whole changed test translation unit is freshly compiled per CPU and control;
unchanged objects are reused in private incremental consumers. All 275 actual
regular-file link inputs verify, and current source/header match frozen GREEN.
The earlier 84/194/73-file evidence and 519-file failed checkpoint seals verify.

Compile preflight artifacts retain the initial macOS-only numeric error assertion,
refined before behavioral RED to the portable system-category assertion. An
initial inline receipt reader incorrectly required CHECK rather than CHECK_FALSE;
the exact-expression reader corrected that tooling error without changing any
test or behavioral receipt. Both details remain recorded in QA artifacts.

Inline scoped review covers ownership, cleanup, readiness observation and
portability; it is not independent whole-candidate review. ARM minimum target is
26 and Intel minimum target 14, SDK 27. Execution is on ARM/macOS 27 and Intel
under Rosetta, not native minimum-OS proof or a final app rebuild. Hidden/real
interop and broader core/GUI acceptance matrices are not rerun for this fixture
change. No live app/profile/key/server/VM/feed change occurs.

## Remaining Gates

Qt descriptors cannot be atomically transferred to the SDK. The narrowed
release-to-bind window can still collide and must fail explicitly. Passing runs
do not retroactively identify the owner of the earlier `61112` UDP collision or
the original historical ARM bind error. Do not waive those receipts.

Independent immutable whole-candidate review, owner/lifecycle and historical
102/180/354ms causes, localization/native quality, provenance/reproducibility,
real-peer/selected-check/bootstrap, native minimum-OS/live/benchmark/soak acceptance
remain pending. Final reviewed app/material rebuild, numeric production build,
signing/notarization, signed-v3 acceptance, guarded closed-app install/readback
and authenticated GitHub/wiki/screenshots/prerelease publication/readback remain
gated. Signed feeds stay last.
