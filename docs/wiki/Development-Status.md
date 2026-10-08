# Development Status

Checkpoint: 2026-10-07. `v3.0.0-pre.1` remains this development checkpoint's release target.

## Implemented In Development

The maintainer-selected Modern Classic globe app icon (variant 1) is integrated
as source assets for macOS ICNS, Windows ICO, desktop PNG resolutions and all
five legacy Qt app-brand directories. Modern toolbar themes use the same brand
resources. Original artwork attribution and generated-reference provenance are
retained, with a checksum manifest and reproducible current-tool packaging
recipe. This is not an installed-app or Windows/Linux runtime acceptance claim.
The asset-update verification passes eleven packaging checks, 617 Qt cases per
architecture and all three GUI layouts (101/101/99 cases) on ARM and
Intel/Rosetta. Six recovery repetitions per architecture also pass; the earlier
natural intermittent recovery timeout remains unexplained and unwaived.

A separate icon-checkpoint review found and corrected incremental MinGW resource
dependencies, generated-brand copyright metadata and missing non-Mac notice
installation. Tests exercise the real CMake resource dependency graph with a
substitute compiler, exact provenance scopes and actual install rules with
Linux/Windows destination layouts. This is not native Windows/Linux acceptance.
Two additional full single-line GUI runs pass in randomized order. Independently,
held search-blacklist and anti-spam write/flush calls reproduce a GUI-thread
stall on both ARM and Intel/Rosetta. Auxiliary checkpoint writes still need an
owned asynchronous snapshot/receipt repair; the original recovery timeout is
not attributed to this diagnostic.

The integrated Torrent UI and proxy routing, managed DC publication, native UI
updates and selective upstream maintenance are present in the development tree.
The audited Torrent translation batches cover 25 catalogs; compiled-string and
format checks do not replace native-speaker review. The DHT advertisement option
is independently configurable, off by default and takes effect on reconnect.

The UDP search-result queue shutdown race is repaired with deterministic
pre-worker-entry and producer/shutdown regressions. Context-specific protocol
validation and resource limits, authoritative outgoing Date/BaseDate metadata,
and interruptible socket waits with proxy revocation are now integrated in the
development tree. The consolidated functional review found two older Shadowsocks
integration defects: pending final ciphertext could be stranded, and coalesced
reply frames could wait indefinitely for more network input. Both are fixed with
regressions for legacy/2022 routes, buffered commands, file sends, graceful
disconnect and forced shutdown. This review covered the prioritized maintenance
paths, not an exhaustive audit of every source file.

The earlier September 29 macOS checkpoint passed 1,427 cases and 61,767 assertions
across core, Qt, Torrent and window suites, with the real-GOST tests enabled.
This is development verification, not an installed or published release.

A previous versioned macOS ARM64 candidate passed bundle dependency and
ad-hoc signature checks. A packaging regression also covers extensionless,
non-executable framework binaries, which must not escape the link/OS audits.
Its isolated CLI startup passes with network, real-profile and Homebrew reads
denied. The candidate declares macOS 26 because of its bundled dependencies;
testing was on macOS 27, not macOS 26 or older. The approved release split is an
Intel-only macOS 14+ ZIP and an Apple-Silicon-only macOS 26+ ZIP, with explicit
architecture/minimum-OS filenames. Separate thin Intel and Apple Silicon
verification builds now pass. Intel verification runs through Rosetta, not native
Intel hardware; native minimum-OS acceptance remains pending. The earlier ARM64
candidate is not the final rebuilt release for either architecture.
Dependency notices and matching source materials have been staged
for the exact candidate; final distribution checks are still required. Ad-hoc
verification is not Developer ID signing or notarization, and CLI startup is
not live GUI QA.

The eight optional real-GOST interoperability cases have passed on macOS using
disposable loopback identities and servers. Coverage includes authenticated TLS,
TCP/uTP Torrent transfers on IPv4/IPv6, UDP tracker and DHT exchanges, remote DNS,
and completed-session resume after a fixture-server restart. Direct destination
access is denied in the application fixtures. These checks do not contact or
verify a deployed proxy server, and do not establish partial-piece crash recovery.

## Updater And Quit Checkpoint

Built-in update settings, verified staging and committed-normal-quit installation
are integrated in development. Download routing, signature/archive acceptance
and installer recovery are separate checks; a transport success alone is not an
accepted update or installed release. Production signed-feed activation remains
a release gate.

Core checkpoint writes now use owned background workers after lifecycle
retirement. Settings Save callbacks remain on the startup owner. Pending or failed
receipts retain the application; a timed-out quit requires explicit retry and
cannot install an update merely because its worker finishes later. This is not a
total hard deadline for lifecycle joins, serialization or every filesystem call.

Search-blacklist and anti-spam quit saves also use an owned background batch.
Immutable paths and rule bytes are captured on the GUI owner; worker completion
includes physical thread teardown. A pending-phase timeout retains the application
until explicit retry. Auxiliary recovery preserves successful owner receipts and
permanently cancels update-install consent. Anti-spam now retains checked
per-file commit receipts: a partial failure retries only the remaining files
from the same frozen snapshot. Invalid receipts or owner loss refuse recovery.
Bounded acceptance is described in the latest checkpoint below; broader
filesystem responsiveness and release acceptance remain separate work.

The next isolated diagnostic confirms that the primary GUI settings sync still
blocks the event loop. Actual settings writes and flushes held for 350ms make a
100ms quit request return after 381-406ms on ARM and Intel/Rosetta. The session
can subsequently emit exit readiness; with a Ready, consented update fixture it
also reaches fake installer handoff after the deadline. No real installation was
performed. Frozen values are saved correctly. This is a reproduced quit-path
gap, not the cause of the earlier natural recovery timeout or Torrent freezes.
The owned settings persistence repair and its deadline/consent checks are now
locally implemented with settings-specific checks passing; final acceptance
remains open. The historical diagnostic remains retained.
The original parentless writer transfers to an owned settings-only worker;
GUI getters use frozen values, and readiness follows checked sync, destruction
and thread exit. Cached retirement prevents replay of successful saves.

Prior persistence/quit checks and all three applicable window layouts passed on
ARM and Intel/Rosetta. The latest adjacent ARM run has an intermittent anti-spam
recovery failure: the existing recovery-button case finds a hidden prompt and
fails to complete its retry. The same selector passes on Intel and an unchanged
accepted ARM binary; this does not explain or waive the failure. A focused
settings-ownership repair design and its
[implementation plan](../superpowers/plans/2026-10-06-gui-settings-quit-persistence.md)
were approved for Native execution. Local source checkpoints `e68f50545`,
`09b83854b` and `af796efbf` implement the repair. One fresh review found two
Important issues: pending GUI settings postponed core admission closure, and
pre-transfer refusal left queued automatic writes. Exact RED-to-GREEN checks
cover both fixes, including held settings with an unprepared core and a real
private snapshot-allocation failure. Core admission now closes only after the
safe GUI producer milestone; terminal settings refusal quiesces deferred writes
and preserves original intent. No post-fix reviewer approval is claimed.

The first complete post-fix matrix passed 30 nonempty runs / 4,281 case instances /
443,362 assertions, with zero failures/errors/skips. Both main CPUs pass core
1,035, Qt 618, Torrent 152 and ordinary settings 5 cases, plus GUI layouts
106/106/104 and explicit held retirement checks. The separate no-Torrent build
executes six selections / 13 case instances. Private broken controls detect
exactly seven/four defects. Actual Qt is 6.11.1; host/Rosetta execution and
minimum-OS metadata are not native macOS-14/macOS-26 acceptance. Windows and
other Qt versions remain unverified. Opt-in real-GOST cases are not part of this
matrix. Protected profiles, historical bundles/binaries, providers and prior
failure evidence pass preservation checks. Installed state and release gates
remain unchanged; forced/crash teardown responsiveness is not covered.

A fresh post-commit check then exposed a fixture reading settings bytes before
physical completion. A private delayed-fsync diagnostic proves the value is
absent while Pending and present after Ready. Both byte checks now await the
checked receipt (`612ae2e3c`); original assertions, holds and deadlines are
unchanged. The control passes three cases / 301 assertions. The latest complete
matrix executes 4,281 case instances / 443,781 passing assertions but has four
failures in the retained recovery-button case: `prompt->isVisible()` and
`recovered`, on ARM multiline and Intel sidebar. All settings-specific checks
pass. Overall QA and Task 3 remain open; the earlier green result does not waive
this recurrence. Failed evidence and protected-state readbacks are preserved.

The subsequent fresh post-commit Ready-update/thread-exit check fails two
responsiveness assertions: a 100ms-budget request takes 354ms with no GUI
heartbeat progress. The writer is off the GUI; saved bytes are correct and exit
and installation remain refused. A private same-seed join-tracing comparison
passes without explaining the failed run. This failure is retained and unwaived;
settings responsiveness and plan Task 3 are not accepted as complete.

A bounded unchanged-seed diagnostic then executes 14 runs / 28 case instances:
13 runs pass, while one unchanged-binary run fails initial retry-request latency
(180ms against 100ms) and held-write heartbeat progress. Private stage probes
do not reproduce a slow phase. A passing read-only process sample observes a
synchronous Torrent-engine join on the GUI, but does not identify the natural
failure's cause. Original timings/assertions, source/object/binary pins, live
state and failed evidence remain preserved. No production fix is claimed;
both responsiveness and recovery acceptance remain open.

A wider private wait trace then runs 12 original/passively observed selections /
24 case instances, with 2,279 passing assertions and one heartbeat failure.
That failed flush section returns in 68ms and captures a 63ms synchronous
Torrent-engine join before the settings hold, not a proven cause of the later
heartbeat failure. A separate late-watchdog control proves the fixture can
report no heartbeat despite observed GUI progress when blocking result
collection stops the event loop. RED fails exactly three heartbeat assertions;
private event-pumped collection passes 117 assertions without changing the
request budget or original checks. This is control evidence, not a waiver of
natural stalls or a merged fix. Fixture-only collection correction is proposed;
production ownership and Task 3 acceptance remain unchanged.

The subsequent approved fixture-only correction adds event-pumped watchdog
completion at four refusal-path collectors, leaving all original assertions,
deadlines and storage/destructor collectors intact. Fresh late-snapshot RED
fails exactly three heartbeat checks; current-fixture GREEN passes all 117
assertions. Five consumers are rebuilt into separate ARM, Intel/Rosetta and
no-Torrent test outputs while original targets/objects and failed evidence are
preserved. The complete 32-selection matrix matches exact inventories: 4,287
case instances, 443,977 passing assertions and six failures across five runs,
including one isolated teardown abort, with no skips/expected failures.
Failures cover ARM initial DC publication, sidebar recovery, a 102ms
Ready-update request against 100ms, checked-context teardown, and Intel stopped
selected-file recheck completion at the original 1500ms limit. Intel held
settings and no-Torrent checks pass; none waives a failed run. This does not fix
natural quit-latency/recovery failures; production ownership, Task 3 acceptance
and release gates remain unchanged.

The next checked-context diagnostic runs the retained abort seed in eleven
natural admission selections, all passing 704 assertions. Private receipt-delay
controls reproduce the same teardown abort four times, after 128 ordinary
assertions pass. Both snapshot and settings phases capture Pending on the
correct owner, with a physically ready receipt that cleanup has not collected.
This demonstrates a test fixture cleanup contract gap after quit refusal, not
an established exact-phase cause for the historical natural abort or a
production ownership failure. A test-only receipt drain before disposal is
recommended but not implemented. The guard, deadlines, production sources and
installed application remain unchanged; natural latency/recovery/Torrent
failures and all release gates stay open.

After approval, the isolated admission test's cleanup now collects retained
core receipts on the owner and requires Ready before disposal, while proving
the quit session stays Refused with no exit or update installation. Only eight
test lines change; production ownership/guards, deadlines and the shared fixture
destructor remain intact. Four controlled RED runs reproduce the guard abort
after 128 original assertions; GREEN against the same delayed core objects
passes 148 assertions. Five consumers are rebuilt privately for ARM,
Intel/Rosetta and no-Torrent, preserving original binaries, objects and failure
evidence. This corrects the demonstrated test cleanup gap, not the outstanding
natural stalls, recovery/Torrent causes or release gates.

The subsequent full serial readback matches 32 exact inventories / 4,287 case
instances and 444,190 passing assertions, with two failures in one ARM sidebar
checkpoint-recovery case. No fatal errors, expected failures or skips occur.
Corrected admission passes in all three configurations; every other selection
passes. Neither the current passes nor controlled proof waives the recovery
failure or retained natural latency/Torrent failures. Protected state and old
evidence remain intact; Task 3, installation and release gates stay open.

Focused ARM sidebar diagnosis now reproduces that recovery failure both with
the unmodified test and in a private natural trace. The test clicks at 36ms
while the previous blocked save is still Preparing; its failed receipt is
collected at 48ms and reopens the prompt. The test's 30ms wait is not a terminal
save receipt. Private delayed-receipt controls reproduce the same sequence for
Search blacklist and Anti-spam. Across 26 focused runs, 2,210 assertions pass
and twelve narrowly validated failures remain retained; no fatal errors,
expected failures or skips occur. A test-only wait for renewed refusal before
repairing the path is recommended but not implemented. Production code,
deadlines, assertions, installed app and live profile remain unchanged. This
does not close other quit stalls, the historical exact timings, broader
regression/native acceptance or release gates.

After approval, that test alone now waits for the renewed recovery prompt before
repairing the blocked path. Five inserted lines preserve all original assertions
and deadlines, without changing production retry or update consent. Fresh
controlled RED retains eight exact failures after 124 passing assertions; GREEN
against the same delayed-receipt objects passes 180 assertions. All six ARM and
Intel/Rosetta GUI layouts pass in the complete serial matrix. Its 32 exact
inventories cover 4,287 case instances and 444,046 passing assertions, but QA
remains FAIL on one Intel/Rosetta core assertion: final Shadowsocks disconnect
send completion (`tests/test_bufferedsocket.cpp:129`, seed 130004532). There are
no fatal errors, exceptions, expected failures or skips. The new failure is
retained with cause unestablished. This verifies the bounded test scheduling
correction, not other retained causes, native acceptance or release readiness.
No installed app, profile, production routing or signed feed changes.

A focused socket diagnostic subsequently observes a peer-EOF/listener-receipt
gap. Nine unchanged-binary reruns pass, but one of 24 private no-forced-delay
trace runs checks completion false after EOF/all expected ciphertext bytes;
the callback publishes about 0.35ms later within the original deadline. The
socket closes before Failed listeners run. Trace instrumentation can affect
scheduling, so the original historical 2022 failure's precise timing is not
claimed. Four private delayed-callback controls reproduce the gap for both
ciphers on ARM and Intel/Rosetta. Six inventories / 37 cases / 296 sections
validate 1674 passing assertions and nine exact failures, without exceptions,
fatal errors, expected failures or skips. This is diagnostic evidence, not
green-suite acceptance or a proxy fix. A test-only wait for completion using the
same three-second deadline is recommended pending approval; no public code,
installed state, live routing, credential/key or release-feed changes occur.
Earlier causes and all final acceptance/publication gates remain open.

The approved test-only repair now awaits the disconnect callback after the
read loop within the same three-second deadline. Three inserted lines preserve
every prior assertion, byte/drain check and cancellation bound; production
callback order and transport behavior are unchanged. Fresh RED verifies eight
completion failures after 173 passing assertions; GREEN with the same delayed
callback behavior passes 183 assertions. Twelve uninstrumented focused runs
pass 555 assertions. The complete serial offline matrix passes all 32 exact
selections / 4,287 cases / 444,190 assertions, with zero failures, test errors,
expected failures or skips. Six GUI layouts and no-Torrent checks are included;
optional real-GOST interop is not. Input and protected-state readbacks pass.
Intel runs through Rosetta, not native minimum-OS acceptance. Prior failures
remain retained and earlier causes are not waived. This verifies the bounded
test repair, not overall Task 3, independent candidate review, live/native/soak
acceptance or release readiness. Installed/profile/server/key/feed state stays
unchanged; signed feeds remain last.

Earlier regular core and Qt suites also passed on both; the Intel
Torrent suite passes. An ARM Torrent peer-policy rejection-counter failure is
retained for investigation despite a passing focused rerun. Earlier intermittent
bootstrap, publication and bind causes are not waived by green reruns.

A subsequent isolated quit diagnostic confirms that GUI retirement synchronously
joins the Torrent worker while its final checkpoint is written. Original
350ms write/flush holds on the checkpoint cause 28 requests to take 365-401ms
against a 100ms budget, with no GUI heartbeat; 14 matched unheld controls take
6-25ms. Timeout still refuses exit, update consent and installation, and saved
bytes finish correctly. Four observed stacks identify the same join boundary.
ARM and Intel/Rosetta execute 13 diagnostic runs / 19 cases / 2774 passing
assertions and 56 exact controlled duration/heartbeat failures. Six unchanged
current-selector runs pass but do not waive historical 102/180/354ms stalls or
establish their original timing. No production code or existing test changes,
complete-suite rerun, deployment or release acceptance are claimed here.
Original consumers, production objects and protected state remain unchanged.
A separately reviewed owned nonblocking Torrent-retirement design is the next
repair step, including sharing and proxy worker boundaries. Broader coverage,
retained causes, localization/provenance, independent review/native/live/soak
and final build/sign/install/publication remain pending. Signed feeds last.

The focused nonblocking Torrent-retirement written design has been approved.
Its implementation plan is now approved; Native execution is preserved. The
first source checkpoint, `1ccb0131`, adds a terminal engine admission fence and
an owned thread-exit/checkpoint receipt while preserving synchronous legacy
shutdown. Eight baseline case instances produce 24 exact failures; final
focused tests pass 90 assertions on each CPU. Complete ordinary Torrent suites
pass 241 instances / 18,520 assertions on both ARM and Intel/Rosetta, with no
failures/errors/skips and matching inventories. Protected state and prior
evidence seals pass readback. Sharing retraction, proxy physical reaping and
runtime/GUI composition are still pending: the application quit path remains
synchronous. Integrated matrix, broken-path controls and independent review
remain required. No install/release/activation occurred; earlier failures and
remaining gates stand.

Peer-policy test failures now include the policy variant, listener ports, proxy
forwarding and available leech peer/alert state. A deliberately invalid loopback
endpoint reproduces the counter failure without payload transfer and verifies
the diagnostic output. It does not establish the cause of the retained natural
failure or justify changing the security policy.
The diagnostic observers were reviewed and corrected to freeze the original
assertion values before collecting extra details. A controlled late-counter
change verifies that boundary. Corrected full Torrent suites pass on ARM and
Intel/Rosetta (152 cases each); the original intermittent failure remains open.

The 32 previously missing proxy-test, Torrent-menu, magnet-sharing and transfer
runtime keys now have entries in all 25 catalogs. The unchanged production
extraction contains 2,065 keys, with no missing active keys in those catalogs.
Catalog format and compiled-string checks do not establish complete localization:
inherited unfinished translations and native-speaker review remain pending.
Complete resource/dependency provenance and reproducibility remain separate
gates. Independent static review of the complete updater change range found two
receipt defects: late backend offer revocation and incomplete GUI layout saving.
The repairs also cover uninitialized file-browser snapshots, hidden history
loaders and shared/inactive history layout keys. Final narrow static review
finds no remaining actionable issue in that correction. Latest ARM layout and
recovery checks pass, including complete single-line, multiline and applicable
sidebar suites. Fresh Intel/Rosetta core, Qt, Torrent and all three applicable
GUI-layout suites also pass; focused controller checks pass on both architectures.
An earlier complete single-line checkpoint-recovery timeout remains unexplained
despite passing reruns. A sorting-test teardown abort was traced to that fixture
destroying a live database loader; the corrected fixture joins it and retires
GUI workers before destruction. Failed evidence is retained. Static review and
isolated tests are not native release acceptance or security certification.
These receipt repairs are implemented and reviewed; the retained intermittent
failures and the final release gates below remain open.
The current catalog audit also passes all 23 tests without skips; this checks
extraction, formats and compiled strings, not native-language quality.

The retained historical QA bundles now have file-level framework mappings for
114 Apple Silicon and 96 Intel non-code files, including shipped headers and
helper metadata. All match the pinned Sparkle SDK or deployed Qt providers.
Across both bundles, 93 files match source bytes and 72 localization dictionaries
match source values; 12 generated resources have source associations, not
reconstructed-build proof. All 33 initially unresolved metadata/module-map files
now have byte-exact reconstruction evidence. Nineteen ARM Qt plists and four
Sparkle files matched current-generator fixtures. The ten Intel Qt plists match
an unchanged, independently retrieved official CMake 3.30.6 template; CMake added
the bundle-name field in 3.31. The original ten failures and the newer-template
rejection control remain retained. A matching template does not identify the
historical SDK producer, compiler or complete framework build recipe.

The 440 inherited icon/resource files in each historical bundle also have
byte-exact immutable public project repository origins: 283 in upstream Eiskalt
and 157 in this fork. Project edit history is not proof of the original artist
or derivative-work licensing chain. Inherited notices remain unchanged; legal
review and reproducibility are not certified. These bundles are not current-
source release assets. Retained bundles/providers and protected live files remain
unchanged.

## Before Prerelease Publication

1. Finish retained regression diagnoses, localization, resource/provenance checks
   and independent review of the final source candidate.
2. Complete native/minimum-OS and live GUI/lifecycle acceptance for the approved
   Intel-only macOS 14+ and Apple-Silicon-only macOS 26+ split.
3. Rebuild final reviewed-source assets; preserve matching dependency notices and
   source materials. Complete numeric build allocation, signing, updater acceptance
   and guarded closed-app installation/readback. Keep profiles, credentials,
   private infrastructure configuration and QA artifacts out of release packages.
4. Publish the maintained wiki, approved screenshots and prerelease assets, then
   confirm public readback. Activate signed feeds only after the final gates;
   signed feeds are last. Wiki sources remain available in `docs/wiki` meanwhile.

## Acceptance Still Separate

Native-speaker review, Windows/Linux Torrent packaging, older macOS compatibility,
live proxy reconnection and DHT behavior, sharing-cache restart acceptance and
extended crash/freeze soak must be reported as tested or untested individually.
A green macOS test suite is not proof for other platforms or a live deployment.

## Later Roadmap

Mixed DC/Torrent pieces, SQLite share storage, parallel TTH hashing, MCN1,
HBRI/SUDP, temporary sharing, richer chat/retry policies and compressed/indexed
lists are separate work, not promised features of the first prerelease.

The next Native component checkpoint, `ff835364`, adds checked managed-share
retraction and publisher retirement. GUI fencing cancels publication and freezes
owner IDs without retracting through the core on the GUI. The existing worker
attempts all checked removals while core admission remains open; Ready requires
successful retraction and physical thread exit. Failed, throwing or missing
checked callbacks refuse completion. Legacy stop/invalidate behavior remains,
without replay after checked retirement.

A fresh read-only component review identified a callback-copy exception that
could end the worker before later cleanup. A real already-published-share
regression reproduces exactly six failures; the repair retains the failed worker
until owner retirement and rejects further work. Deterministic queued
cancellation and separate legacy counters address the two Minor test gaps.
The focused independent followup has no remaining findings; it is not the final
integrated repair-range review.

Both architectures pass focused core40/504 and Qt12/166 cases/assertions.
Four complete ordinary core/Qt runs pass 3,718 case instances and 368,874
assertions, with identical case-name inventories and zero errors/failures/skips.
Completion reruns focused tests and verifies unchanged source/binary pins against
those full-suite receipts. Historical bundle/signature, material/provider,
protected live-file and prior failure-evidence readbacks pass. This is offline
component QA on host/Rosetta, not native minimum-OS, deployed or installed-app
acceptance. Proxy receipts, runtime/GUI integration and the final composed/no-
Torrent/broken-control/review matrix remain pending, as do all previously named
natural-failure, language-quality and release gates. Signed feeds remain last.

The next Native proxy component checkpoint, `1561ca2f9` (2026-10-07), fences
new admission for checked quit and retains workers until physical thread exit.
Ordinary reaping no longer joins based on the body-end flag. Worker/container
ownership precedes thread launch; a real constructor-failure control verifies
rollback and accepted-descriptor closure. Route/limits remain retained while
Pending, ordinary stop/start stays restartable, and closed listeners are not
advertised. No product fault-injection hook or deployment change is added.

An unchanged extraction run passes 21 cases / 562 assertions. New baseline
controls reproduce exactly 31 expected failures. Focus and completion reruns
pass six JUnit case nodes / 94 assertions on each architecture, including the
separate explicitly selected test-only launch interposer. Ordinary socket/proxy
tests pass 92 / 1,012 each. Full Qt suites pass 753 nodes each and 67,999 total
assertions, with zero failures/errors/skips and matching inventories; all prior
ordinary tests remain. Source/binary pins, protected-state readbacks and prior
evidence seals pass. This is host/Rosetta offscreen QA, not native minimum-OS,
live or installed-app acceptance.

Runtime/MainWindow phase composition and cached GUI quit milestones are now
implemented and verified in isolated tests. Next are Task5's fresh integrated
and no-Torrent builds, remaining broken-path controls and independent
repair-range review. The checked quit path polls retained physical receipts;
legacy synchronous shutdown remains available for existing callers.
Earlier timing/Torrent causes, broader coverage, localization/native quality,
provenance/reproducibility, independent whole-candidate/native/live/soak checks,
final rebuild/sign/notarization/update/install and authenticated publication
remain pending. Overall GUI-settings Task3 and parent Tasks7/8 stay open;
signed feeds remain last.

The local runtime/MainWindow composition (2026-10-07) now uses
checked publisher -> engine -> proxy retirement and caches GUI layouts apart
from physical producer completion. Task4's focused verification is complete;
final integrated repair acceptance is still pending.
Final blocking controls reproduce exactly 40 failures; focused runtime and
composed selectors pass 144 and 534 assertions on each CPU. The unchanged
no-Torrent original settings suite passes 12 nodes / 654 assertions, using
current GUI/test translation units and retained accepted dependency objects.

The initial Torrent-enabled settings suite reported 23 failures: settings were
not frozen while Torrent is Pending, permitting GUI auto-sync and late writes;
two immediate core-route observations conflict with physical producer gating.
The approved settings-freeze/compatibility revision now passes. It starts the
existing owned settings writer after
layouts, independently of Torrent physical retirement. Only the original
core-route observation is moved behind producer readiness; its assertion and
all original helpers, holds and budgets remain. All failed runs
remain retained. Both CPU focus runtime144/composed648/original settings688
pass, with complete Qt754 cases each / 68,301 total assertions and matching
inventories. The applicable current-GUI/test retained-dependency no-Torrent
consumer passes12/656. Deferred-freeze and early-core controls fail exactly6
intended assertions each. Thirty-six input pins and protected/prior-evidence
readbacks pass. Task5, overall GUI-settings Task3, parent Tasks7/8,
historical natural-failure, native/language/provenance/soak and release gates
remain open. No installation, publication, key, server, VM or feed change occurs.

Task4 source checkpoint `ee6d707b` is locally committed. Fresh completion runs
repeat both CPU runtime/composed focus and validate the unchanged complete
suite receipts with all36 input hashes. Tested inputs are frozen for later
Task5 work. This is an isolated source checkpoint, not deployment or final
integrated acceptance. Next: Task5 complete core/Qt/Torrent/GUI/hidden/no-Torrent
matrix, remaining bounded broken controls and one independent immutable-range
review. Broader localization/native quality, provenance/reproducibility,
whole-candidate/native minimum-OS/live/lifecycle/benchmark/soak checks, final
reviewed app/material rebuild and numeric production build, signing/notarization,
signed-v3 acceptance, guarded closed-app install/readback, authenticated
GitHub/wiki/screenshots/prerelease publication/readback remain pending. Overall
GUI-settings Task3 and parent Tasks7/8 remain open. Signed feeds remain last.

### Integrated Torrent Quit Checkpoint (2026-10-07)

Task5 is **not accepted**. Fresh ARM/Intel and whole no-Torrent builds succeed;
the three independent-review repairs pass focused tests on both CPUs. Private
blocking/reap/retraction controls retain their exact expected failures. The
complete serial run records 6,392 case instances / 603,596 assertions, with
94 failures/errors. ARM GUI runs abort on legacy immediate-completion assertions
in a notification callback; Intel records Pending-versus-terminal expectations
and a caught callback exception. ARM Qt also reports one unwaived Torrent bind
failure. Partial ARM GUI inventories are explicitly recorded, not treated as
passes. No-Torrent selectors pass; historical/live protected-state readbacks pass.

The focused [GUI Pending compatibility design](../superpowers/specs/2026-10-07-gui-quit-pending-compatibility-design.md)
and [implementation plan](../superpowers/plans/2026-10-07-gui-quit-pending-compatibility.md)
were approved and executed in Native mode. Fixture-only checkpoint `be8f21da`
migrates the 19 authorized observations, preserves all cases/sections, original
terminal/cached/rejection/consent checks, shared helper and timing limits, captures
GUI callback results outside Qt delivery, and retains blockers until Refused.
Production shutdown behavior is unchanged.

Fresh ARM/Intel five-target/interposer and whole ARM no-Torrent builds pass.
Focused verification passes 312 instances / 9,270 assertions. The subsequent
complete 47-run matrix passes 6,729 instances / 621,195 assertions, zero failures/
errors/skips/fatal exits. All 37 inventory comparisons match; complete GUI
inventories are 126/126/124 per CPU. Four current-source broken controls retain
their exact failures. All 101 current pins, historical bundles/signatures,
archive/provider/protected-file and earlier seals verify, including the sealed
519-file failed checkpoint and design/plan readbacks. These are host ARM/macOS 27
and Intel/Rosetta checks, not native minimum-OS/live or final release acceptance.

The earlier 94-failure/error audit remains intact. Its bind failure did not recur
but remains unexplained and unwaived; Task5 is still not accepted. The three
reviewed runtime repairs remain local and separate from the fixture checkpoint.
Broader owner/lifecycle, historical 102/180/354ms causes, localization/native
quality, provenance/reproducibility, real peer/selected-check/bootstrap coverage,
minimum-OS/live/soak acceptance remain open. Final reviewed builds, production
build allocation, signing/notarization, guarded install/readback, authenticated
GitHub/wiki/screenshots/prerelease publication and signed-v3 acceptance remain
pending. GUI-settings Task3 and updater Tasks7/8 stay open. No deployment or
publication occurred; signed feeds remain last.

### Torrent Bind Diagnostic Follow-Up (2026-10-07)

The [bind investigation](../research/2026-10-07-torrent-bind-diagnostic.md)
replayed the pinned historical telemetry consumer's complete ordinary ARM Qt
suite: 755 case instances / 34,361 assertions, no failures/errors/skips and the
same historical case/section inventory. The original bind error did not recur.
Private TCP-only and UDP-only occupied-port controls each reproduce the exact
source-seed assertion with native `sock_bind` / `EADDRINUSE`; their released-port
counterparts pass. Runtime chooses a random number, not an OS-selected free
port, contrary to the fixture comment. Production source/assertions, listener
policy and live state are unchanged.

This confirms a possible collision mechanism, not the historical error's native
cause: the original receipt lacks its code. That failure remains unwaived and
Task5 remains not accepted. A focused native bind-diagnostic design is next;
broader review, lifecycle/timing, localization/provenance, native/live acceptance,
final build/signing/install/publication gates remain open. Signed feeds remain
last.

### Native Torrent Listener Diagnostics (2026-10-07)

The approved [native listener diagnostics](../research/2026-10-07-native-torrent-bind-diagnostics.md)
retain the original error prefix plus operation, transport, numeric local
endpoint and safe category/code. Credentials, paths, arbitrary category/message
text and raw alerts are excluded. Listener history, ports, retries, routing and
no-direct-fallback policy are unchanged.

Both CPU RED consumers reproduce exactly 72 missing-field CHECK failures.
Focused GREEN passes 14 instances / 1,178 assertions, including real TCP/UDP
collisions, formatting/privacy, recoverable accept and runtime propagation.
Complete serial backend/ordinary Qt runs record 2,000 instances / 105,654
assertions, one failure and no errors/skips/fatal exits. Inventories retain all
preceding cases; 297 actual link-input hashes, current-source snapshots and the
73/519-file prior seals verify. These are private incremental ARM/macOS 27 and
Intel/Rosetta consumers, not final app/native minimum-OS or installed acceptance.

The Intel Qt shared-transfers swarm fails its original traffic assertion with
native `sock_bind` / UDP / `127.0.0.1:61112` / `system:48` (`EADDRINUSE`). Its
unchanged fixture chooses ports with TCP-only reservations; UDP availability and
the conflicting owner were not captured. Both original full-suite and all-green
checker exits remain 1. No green rerun waives this failure. Next: focused fixture
endpoint-ownership and dual-transport regression design, with assertions,
deadlines, production policy and live state preserved.

The original historical ARM bind cause remains unknown/unwaived. Task5,
GUI-settings Task3 and updater Tasks7/8 remain open. Earlier reviewed retirement
repairs remain local and separate. Broader independent review, lifecycle/timing,
native/localization/provenance/reproducibility, real peer/selected-check/bootstrap,
minimum-OS/live/benchmark/soak gates remain pending. Final reviewed builds,
production build allocation, signing/notarization, signed-v3 acceptance, guarded
install/readback and authenticated publication remain gated. No deployment or
publication occurs; signed feeds remain last.

### Swarm Endpoint Ownership Spike (2026-10-07)

The [fixture ownership investigation](../research/2026-10-07-swarm-endpoint-ownership.md)
uses throwaway consumers only. On both CPUs, original/released-reservation swarm
runs pass 67 assertions each; held UDP reservations reproduce exactly the two
original traffic REQUIRE failures (v1/hybrid), with the matching injected
descriptor captured. This confirms the TCP-only fixture selection gap without
changing production port policy, assertions or deadlines.

One traced full ordinary Intel/Rosetta Qt replay passes 756 instances / 34,218
assertions, with the same inventory. No natural collision recurs, so the owner
from the earlier `61112` UDP failure remains unknown and unwaived. Historical
ARM bind diagnosis also stays open. The 269 control inputs, preceding 297
diagnostic inputs, source snapshots and 194/73/519-file seals verify. Earlier
seven-file local repairs are byte-identical. Compiler/duplicate-label setup
attempts and the original driver exit 1 remain recorded separately from exact
behavioral control results.

Next proposed bounded repair: paired TCP/UDP fixture reservations held through
metadata preparation, released immediately before each session starts, with
native listener-readiness and deterministic rejection/lifetime/handoff tests.
The residual release-to-bind race must fail explicitly; no production retries,
fallback, restarts or assertion weakening. The probe does not authorize retaining
its throwaway code as a repair. Task5, GUI-settings Task3 and updater Tasks7/8,
independent/lifecycle/timing/native/localization/provenance/live/soak and final
build/signing/install/publication gates remain open. Nothing deployed or
published; signed feeds stay last.

### Swarm Paired Reservation Repair (2026-10-07)

The approved [fixture-only paired reservation repair](../research/2026-10-07-swarm-port-pair-repair.md)
now owns TCP and exclusive UDP together through metadata setup, releasing each
pair immediately before its session starts. Occupied UDP candidates release
tentative TCP ownership. Native probes verify retained reservations; fresh
sessions require both expected listener endpoints. Original transfer assertions,
30s polling limits, direct/fixed-port settings and production routing/retry/
fallback policy remain unchanged. The handoff race stays an explicit failure.

Both CPU RED consumers fail exactly four missing-reservation checks. Focused
GREEN passes 5 instances / 119 assertions per CPU, including original v1/hybrid
transfers. Missing UDP, early caller release and omitted UDP close controls fail
exactly 4/2/2 intended assertions per CPU. Complete serial ordinary Qt suites
pass 760 instances each / 68,566 total assertions, no failures/errors/skips/fatal
exits. Inventories retain all preceding cases plus four regressions. All 275
actual link inputs, current GREEN snapshots and prior 84/194/73/519-file seals
verify. Existing seven-file local retirement repairs remain unchanged/separate.

These are private incremental ARM/macOS 27 and Intel/Rosetta consumers, not
native minimum-OS, independent whole-candidate or final release acceptance.
Neither historical bind failure is waived or retrospectively explained. Task5,
GUI-settings Task3 and updater Tasks7/8 stay open. Next: independent immutable
candidate review and remaining lifecycle/timing, native/localization/provenance/
reproducibility, real peer/selected-check/bootstrap, minimum-OS/live/benchmark/soak
gates, then final build/signing/signed-v3/install/publication/readback. Nothing
installed or published; signed feeds remain last.

### Composite Torrent Quit Source Review (2026-10-07)

One [fresh independent composite source review](../research/2026-10-07-quit-composite-candidate-review.md)
of `dd088f2b1..db94c22e3` plus the existing local retirement repairs finds no
Critical/Important issue. Parent snapshot/pin/evidence checks satisfy its
conditional source verdict. One Minor is deferred: established TCP/UDP payload
exchange across Fence before physical Proxy retirement. No relay interruption
is demonstrated; this is explicit coverage debt.

All 3,486 regular snapshot files, 101 earlier pins, 275 incremental inputs and
historical evidence seals verify. Prior GUI/swarm receipts are revalidated,
not rerun. Three post-GUI-matrix diagnostics deltas remain explicit; a fresh
consistent current-source matrix is next. Source review closes only its scoped
gate, not historical causes, overall Task5, GUI-settings Task3, updater Tasks7/8
or final updater/prerelease whole-diff review. Native/live/language/provenance/
reproducibility/peer/bootstrap/selected-check/benchmark/soak and final build/
signing/install/publication/readback remain pending. No install or publication;
signed feeds last.

### Current Consistent Quit Matrix (2026-10-07)

The [fresh consistent current-source rebuild and full matrix](../research/2026-10-07-current-quit-matrix.md)
pass: 13 test consumers, both CPU configurations and whole ARM no-Torrent,
47 serial runs / 6,743 case instances / 621,713 assertions, no failures/errors/
skips/fatal exits, and all 37 retained-inventory comparisons. Four fresh broken
controls reproduce exact 14/9/14/6 failure multisets. All 97 frozen pins, 2,867
captured regular-file inputs, protected historical/live state and earlier seals
verify. This is ARM/macOS 27 and Intel/Rosetta test evidence, not native minimum-OS
or final release acceptance; no source repairs were changed in this checkpoint.

Current consistent build/matrix gates are verified. Overall Torrent Task5,
GUI-settings Task3 and updater Tasks7/8 stay open. Historical bind/timing/recovery
causes and deferred established-relay coverage remain unwaived. Broader lifecycle,
real peer/bootstrap/selected-check, native/minimum-OS/live/benchmark/soak,
localization/native quality, complete provenance/reproducibility, final whole
updater/prerelease review, application/material builds and numeric production
allocation, signing/notarization/signed-v3 acceptance, guarded closed-app install
and authenticated GitHub/wiki/screenshots/prerelease publication/readback remain
pending. Existing seven-file local source repairs stay separate. Nothing installed
or published; signed feeds remain last.

### Established GOST Relay Fence Coverage (2026-10-07)

The [approved test-only relay regression](../research/2026-10-07-established-relay-fence.md)
now covers established authenticated TLS GOST TCP/UDP payloads across repeated
quit fences, rejection of new connections and subsequent physical cleanup. Each
CPU private control produces exactly two intended post-Fence payload failures.
Focused checks pass 7 case nodes / 166 assertions per CPU. Complete ordinary Qt
passes 761 cases per CPU / 69,095 total assertions, retaining all 760 original
cases plus one generated case. Bounded independent source review finds no issues;
291 current and 2,866 retained input hashes, protected state and historical seals
verify. No production routing or deadline change.

The established-relay coverage deferral is addressed. Historical bind/freeze/
recovery causes remain unwaived; overall Torrent Task5, GUI-settings Task3 and
updater7/8 stay open. Broader lifecycle/real peer/bootstrap/selected-check,
native/minimum-OS/live/benchmark/soak, localization/native quality, complete
provenance/reproducibility and final whole review/build/allocation/signing/
notarization/signed-v3/install/publication/readback remain pending. This is
incremental Qt evidence, not a new 47-run matrix or release acceptance. Prior
repair hunks remain local/uncommitted. Nothing installed or published; signed
feeds last.

### AntiSpam Per-File Quit Recovery (2026-10-07)

The approved [bounded per-file repair](../research/2026-10-07-antispam-per-file-recovery.md)
is implemented at `09a16b8f`. One frozen snapshot and checked per-file masks
prevent replay of completed writes. Pending, malformed/foreign/regressing
receipts and lost/replaced owners cannot authorize exit or retry; valid partial
receipts and proven unstarted launch failures permit explicit selective retry.
Successful primary/search/settings receipts stay cached and recovery permanently
disarms update-install consent. Exceptional future readiness retains its owned
worker rather than joining on a GUI poll. Forced teardown may still join.

Both CPU behavioral RED consumers reproduce exactly twelve marker failures
apiece. Focused GREEN passes 62 instances/3,000 assertions. One independent source
review's two Important findings have exact RED/GREEN fixes: bind owner identity
at first freeze and include all four composed cases in normal CTest selection.
Two new watchdog collectors are corrected only after traced test-side GUI
blocking; original deadlines, checks and failing receipts remain preserved.

Eight final scoped consumers pass on ARM/macOS27, Intel/Rosetta and ARM
no-Torrent. All 44 serial selections pass 2,975 instances/137,048 assertions,
zero failures/errors/skips/fatal exits; all 28 retained inventory comparisons
match. Fresh final settings/GUI lifecycle confirmation adds 32/1,774. Protected
state, 58 provider resources, 5,875 prior evidence entries and 37,114 new sealed
file hashes verify. These are scoped consumers, not a new full core/Torrent
matrix, whole-application build or native minimum-OS/live acceptance. Original
seven-file local repairs remain separate; this bounded AntiSpam gate is closed.

Overall Torrent Task5, GUI-settings Task3 and updater7/8 remain open. Historical
bind/freeze/latency/recovery causes, broader lifecycle/real peer/bootstrap/
selected-check/native/minimum-OS/live/benchmark/soak, localization/native quality,
complete provenance/reproducibility and final whole review/build/allocation/
signing/notarization/signed-v3/install/publication/readback remain pending.
Nothing installed or published; production signed feeds remain last.

### Current-Candidate GOST Interoperability (2026-10-07)

The [latest-candidate real-GOST checkpoint](../research/2026-10-07-current-gost-interop.md)
passes all eight existing public cases / 448 assertions on each CPU consumer,
plus 13 child receipts each (ARM 1,824 and Intel/Rosetta 1,752 assertions).
Generated two-way TCP/uTP IPv4/IPv6 transfers, tracker/DHT/DNS traffic,
completed-job resume/restart, authenticated plain SOCKS5, global DC traffic and
bad-auth/trust/direct-denial checks pass. All 18 disposable fixtures shut down.
The current cold Qt binaries, 1,058 source/input pins, original local repair,
historical/protected state and exact 605-file new evidence set verify unchanged.
No production code/test-body change or new application build.

This closes latest-candidate loopback GOST interoperability only. External/live
server acceptance, partial-piece crash recovery, selected-check during live peer
traffic, historical bind/timing/recovery causes, broader lifecycle/native/
minimum-OS/benchmark/soak, localization, complete provenance/reproducibility and
final review/build/allocation/signing/notarization/signed-v3/install/publication/
readback remain pending. Overall Torrent Task5, GUI-settings Task3 and updater7/8
remain open. Nothing installed or published; production signed feeds stay last.

### Selected Recheck With An Active Peer (2026-10-07)

The [private active-upload checkpoint](../research/2026-10-07-selected-active-peer.md)
proves peer quiescence during a held selected-file check and continued transfer
by another job: 1 case / 24 assertions on each CPU. Original selected planner/
journal/engine inventories retain 79 instances / 1,255 assertions per CPU. ARM
passes; Intel/Rosetta fails only the 10,000-file / 500,000-piece case's shutdown
checkpoint, while its 35ms admission passes the unchanged 50ms limit. Both current
Qt UI/repair/sharing-restart selections pass 11 instances (ARM 464 / Intel 461).

The gate is NOT_ACCEPTED. One separately traced Intel pass (1/7, drain 2087ms,
commit/receipt true) does not explain or waive the original refusal. Two initial
private peer-prerequisite failures and preparation errors remain retained.
Corrected fixture-only peer-class throttling makes live upload observable;
original product/test bodies, deadlines and seven-file local repairs stay intact.
Forty-four retained backend inputs, protected state and 95 final sealed files verify.

Next is diagnosing the actual refused checkpoint boundary. Active-download/
combined-GOST/partial-piece-crash/external/live/native/minimum-OS acceptance,
historical causes, broader lifecycle/soak/benchmark, language/provenance/
reproducibility and final whole review/build/allocation/signing/notarization/
signed-v3/install/publication/readback remain pending. Overall Torrent5,
GUI-settings3 and updater7/8 remain open. Nothing installed or published; feeds last.

### Selected-Check Refusal Diagnostics (2026-10-07)

The [bounded boundary/profile diagnostics](../research/2026-10-07-selected-checkpoint-refusal.md)
do not reproduce or waive the original Intel large-scope shutdown refusal.
Six receipts pass 84 instances / 1,290 assertions, including the unchanged
79-instance Intel lifecycle inventory. Focused Intel drains take 1,934-2,733ms;
ARM takes 1,521ms. A separate successful Intel profile attributes 703+714ms
(about 73% of its 1,942ms drain) to repeated metadata/symlink validation. This
is a measured cost, not the earlier refusal's proven cause or a runtime repair.

Original tests/deadlines/local repairs, 44 retained backend inputs, historical/
protected state and prior evidence verify. The 64-file new seal includes six
disposable homes and a retained profiling-generator compilation error, corrected
only in a separately labelled private build. Acceptance remains HELD.

Next: capture the actual failing boundary with validation attribution; do not
extend deadlines or remove path-safety checks based on green traces. Overall
Torrent5/GUI-settings3/updater7/8, historical causes, wider lifecycle/native/
minimum-OS/live/soak/localization/provenance and final review/build/signing/
notarization/signed-v3/install/publication/readback remain pending. Nothing
installed or published; production signed feeds remain last.

### Buffered Drain And Catalog Audit (2026-10-07)

The [buffered diagnosis and current catalog audit](../research/2026-10-07-buffered-drain-and-catalog-audit.md)
retain the original Intel shutdown refusal as unresolved. Six buffered receipts
pass 84 instances / 1,290 assertions, with unchanged original limits and inventory.
Intel drains take 2.03-2.63 seconds; every large-scope summary ends with zero active
checks/journal records and committed receipts. Repeated metadata validation remains
the measured cost lead, not proof of the earlier failure's cause. Stop repeating
this successful setup; do not waive the failure. New buffered evidence has a
54-file exact seal, with original local repairs/protected state/prior seals intact.

The independent 23-test catalog audit finds exactly 75 missing-key failures:
three messages in every one of 25 catalogs. Two listener messages moved to the
`TorrentEngine` runtime context and can reuse 50 existing translations; the new
proxy-retirement caption needs 25 entries. A fresh 356-file extraction identifies
2,066 runtime keys and preserves all source/catalog/tool pins. The separate
inherited backlog is 9,327 unfinished runtime entries, including 9,159 empty ones.
The catalog repair awaits bounded-design approval; its 20-file evidence seal
retains the exact RED result. No catalog/runtime changes in this checkpoint.

Next: approved three-key catalog repair with compiled-QM tests. The unresolved
refusal, overall Torrent5/GUI-settings3/updater7/8, historical causes, broader
lifecycle/native/minimum-OS/live/soak/translation-quality/provenance and final
review/build/signing/notarization/signed-v3/install/publication/readback remain
pending. Nothing installed or published; production signed feeds remain last.

### Listener And Proxy-Retirement Catalog Repair (2026-10-07)

The approved [three-message repair](../research/2026-10-07-catalog-context-repair.md)
adds exactly 75 finished entries across 25 catalogs: 50 listener translations
reused verbatim in their current runtime context and 25 new proxy-retirement
captions. Existing catalog bytes/messages/TS attributes and the separate
seven-file runtime/test repair set are preserved. The new compiled-QM regression
first failed on exactly the 75 expected missing keys, then passed. The complete
24-test translation suite passes without errors or skips. Fresh 356-file /
2,066-key extraction has zero missing or ambiguous runtime entries in all locales.

This closes the current context-key gap only. The unchanged inherited inventory
is 9,327 unfinished runtime translations, including 9,159 empty ones; native
language review remains separate. The original Intel shutdown refusal, overall
Torrent5/GUI-settings3/updater7/8, historical causes, broader lifecycle/native/
minimum-OS/live/soak/provenance/reproducibility and final review/build/allocation/
signing/notarization/signed-v3/install/publication/readback remain pending.
Nothing installed or published; production signed feeds remain last.

### Refusal-Only Shutdown Observation (2026-10-07)

The [single refusal-only/post-exit diagnostic](../research/2026-10-07-refusal-only-shutdown-observation.md)
passes the exact original Intel 79-instance / 1,255-assertion selection without
errors or skips. It does not reproduce or waive the original large-scope refusal.
Markers only on already-selected failure branches avoid the previous per-poll
timers. The large case ends with a collected successful worker receipt, released
sessions, matching saved generations and no active check or pending journal.
Its 4.92-second owner-call duration includes work outside the selected-check
drain and must not be compared directly with that drain's three-second limit.
The source can refuse after a late poll clears its final record; that possible
path is not this historical failure's proven cause. No policy is weakened.

The original test/assertion/helper/deadline inventory, seven local repair files,
retained link inputs, protected state and prior evidence remain preserved.
Acceptance stays HELD. Stop positive repetitions of this fixture; next independent
work is dependency provenance/reproducibility. Overall Torrent5/GUI-settings3/
updater7/8, historical causes, broader lifecycle/native/minimum-OS/live/soak/
unfinished/native-language quality and final review/build/allocation/signing/
notarization/signed-v3/install/publication/readback remain pending. No installation
or publication; production signed feeds remain last.

### Retained Static Inputs And Header Isolation (2026-10-07)

The [current static-input audit](../research/2026-10-07-current-static-header-attribution.md)
traces four pinned c-ares/MaxMind archives through 186 matching object bytes and
eight retained both-CPU test consumers, including upstream source/license/notice
readback. However, both CPUs' resolver compiler records select shared headers
currently identifying c-ares 1.34.4 while linking 1.34.8 archives; Apple Silicon
also selected a differently generated uncontrolled MaxMind configuration header.
Fresh preprocessing confirms the retained selections. The strict header gate has
exactly seven byte mismatches and ten origin mismatches and remains HELD. Review
strengthened the audit's missing-evidence checks and retry preservation; four
new RED regressions precede the ten passing harness tests. They do not convert
this failed build-input gate into release acceptance.

Next: focused CMake/provider-header isolation repair and fresh both-CPU consumer
builds, preserving the sealed failed evidence. This is not a proven crash/refusal
cause. The original Intel shutdown refusal, overall Torrent5/GUI-settings3/
updater7/8, broader lifecycle/native/minimum-OS/live/soak/translation-quality/
provenance/reproducibility and final review/build/allocation/signing/notarization/
signed-v3/install/publication/readback remain pending. Nothing installed or
published; production signed feeds remain last.

### Current Provider Header Repair (2026-10-07)

The [bounded header-isolation repair](../research/2026-10-07-provider-header-isolation-repair.md)
passes its current-source test-consumer gate. The dcpp-only build overlay follows
selected provider wrappers, preserves SYSTEM/sibling/unrelated settings, refuses
missing public companion headers, gates MaxMind on library availability, and
declares staged outputs for Ninja incremental correctness. Nineteen fixtures
and three additional generator/config/no-change checks pass after retained RED
controls and bounded independent review.

Five fresh consumers build per CPU; six actual compiler headers, eight overlay
origins and four selected static archive pairs match per CPU. Version probes agree
on c-ares 1.34.8 and MaxMind 1.13.3. Both CPUs pass 97 affected core cases/1,012
assertions, the missing-country-db control, the pinned country fixture lookup,
and two isolated registered CTest checks. The first ARM CTest wrong-library
launcher refusal is retained; explicit controlled-provider routing passes.
This is host/Rosetta QA, not native macOS 14/26, final-app or crash-cause proof.

Next: complete current-source dependency/resource/material provenance and whole-
build reproducibility. Original Intel shutdown refusal, Torrent5/GUI-settings3/
updater7/8, historical causes, broader live/lifecycle/soak/native/translation
quality, final review/build/allocation/signing/notarization/signed-v3/install/
publication/readback remain pending. Nothing installed or published; signed
production feeds remain last.

### Current Libtorrent Source Materials (2026-10-07)

The [offline material refresh](../research/2026-10-07-current-libtorrent-materials.md)
finds the old staged libtorrent lock still selects ABI 1, while both fresh
providers require ABI 2. Exactly the ABI and patch checks fail before a new,
separate material set is prepared. All 1,653 reconstructed source files match
the retained source tree shared by both builds; each CPU's 295 installed
headers, 697 source dependency inputs, 285 link-accounted objects and 106
embedded archive members match. Four sealed fresh consumers per CPU select
the provider; the fifth core-only binary has no libtorrent import.

The new 1,699-file source set retains the complete modified/upstream sources,
current patch/lock, build records/instructions and original standalone notices.
Two complete tar readbacks match byte-for-byte for those frozen inputs. This is
repeatable material packing, not binary reproducibility, historical compiler
proof or complete release source/notice acceptance. Earlier bundles/materials,
dirty repairs, protected state and failed evidence remain unchanged.

Next: finish other current dependency/resource/material and reproducibility
gates. The original Intel shutdown refusal, overall Torrent5/GUI-settings3/
updater7/8, historical causes, broader lifecycle/native/minimum-OS/live/soak/
platform and translation quality, final whole review/build/allocation/signing/
notarization/signed-v3/install/GitHub/wiki/prerelease publication/readback remain
pending. Nothing installed or published; production signed feeds remain last.
