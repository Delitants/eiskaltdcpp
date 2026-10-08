# Selected-Check Shutdown Boundary Diagnostics

## Outcome

The original Intel/Rosetta failure in `Large selected scopes keep owner admission
bounded` remains unresolved. This bounded diagnostic continuation did **not**
reproduce it; successful traces do not waive the original failed receipt.
Acceptance stays **HELD**, not repaired or complete.

Candidate is `67898f32` plus the byte-preserved seven-file local repair. Only
private copies receive worker-side scalar telemetry. Original test bodies,
helpers, assertions, native flow and three-second drain deadlines are unchanged.
Evidence is retained in `Work/qa/selected-checkpoint-refusal-20261007/`.

| Diagnostic | Cases / Assertions | Selected-Check Drain | Result |
| --- | ---: | ---: | --- |
| Focused Intel boundary trace 1 | 1 / 7 | 2,031ms | Pass |
| Focused Intel boundary trace 2 | 1 / 7 | 2,733ms | Pass |
| Focused Intel boundary trace 3 | 1 / 7 | 1,934ms | Pass |
| Focused ARM boundary comparison | 1 / 7 | 1,521ms | Pass |
| Original Intel planner/journal/engine selection | 79 / 1,255 | Large case: 2,139ms | Pass |
| Separate Intel helper profile | 1 / 7 | 1,942ms | Pass |

Six receipts contain 84 case instances / 1,290 assertions, zero failed assertions,
errors or skips. Original Intel suite case/section inventory matches the retained
79-instance selection, including its deliberate refusal/recovery controls.
Those controls still emit failed shutdown receipts as expected; they are not
new test failures and must not be mistaken for the large-case reproduction.

## Measured Lead

Each focused large-case trace reaches native cancellation, requests its final
resume snapshot, commits the journal, then clears active/check-record state and
issues a successful final checkpoint. The slowest focused Intel drain leaves
only about 267ms of its three-second budget. Instrumentation changes timing;
these are diagnostic observations, not latency guarantees or retrospective
evidence of the original refusal's cause.

One separately labelled profile adds scoped timers to complete worker validation
and commit helpers, without changing checks or their order. In its two closing
polls, `safeMetadata()` takes 703ms and 714ms: about 73% of the 1,942ms drain.
Storage identity takes 20ms/21ms, payload identity 43ms/45ms, and result updates
31ms/32ms. The closing journal-commit poll also includes persistence and local
session teardown; those boundaries are not individually attributed here.

Source inspection shows `safeMetadata()` walks every file and uses
`noSymlinks()` to revalidate its path ancestors on every poll. This is a measured
repeat-validation cost for the 10,000-file fixture, **not** proof that validation
caused the earlier refusal. No checks are omitted or cached across polls, no
timeout is extended, and failed retirement is never converted to success.

## Evidence And Limits

Private consumers replace one complete test translation unit per CPU using 44
verified retained backend link/metadata inputs. The extra Intel profile uses the
same pinned inputs. Reversing only the private engine include substitution
recovers the exact original test source. Frozen backend source, candidate HEAD,
original dirty patch and both consumer input hashes verify.

An initial profiling generator emitted literal newline escapes into its timer
declaration and failed compilation before tests. Its script, source and compiler
log remain retained. A new, separately labelled generator corrects declaration
escaping only; no failed receipt, original test or production source is rewritten.
Existing compiler deprecation warnings are retained.

Runs are serial with seed `594128903`, fresh disposable HOME/XDG/TMP and verified
loopback-only Seatbelt external-network denial. No timed run overlaps compilation
or heavy hashing. All parent dyld receipts identify the matching maintained
libtorrent provider. Historical whole-app trees/strict signatures, five archive/
notice pins, 58 provider resources and 9/17/8/3 protected/audit/negative manifest
entries verify before and after. The previous 95-file evidence seal verifies
unchanged. This checkpoint has a verified exact 64-file seal and six disposable
homes, including the failed profiling preparation evidence.

Final evidence seal SHA-256:
`d0f83369e2289789e01402aa5e973be67ab1868a13d337d863d264a203c47800`.
Boundary results SHA-256:
`82116a67520395796cbd53116408c17dcfa1bb33a3b350d17041915f3de4ddb3`.
Profile results SHA-256:
`9dd69998de7172bc03201194d20f63a48adaec0e8fb3a3f3cd0d7ba34b0fd71f`.

## Remaining

The next diagnosis must capture the actual refused large-scope drain/commit
boundary, with validation attribution and original limits intact. Do not repeat
green runs to close the failure or optimize away path-safety checks from this
successful profile alone. Any runtime repair needs a causal regression and a
focused repair design; neither is established by this checkpoint.

Overall Torrent Task5, GUI-settings Task3 and updater Tasks7/8 stay open.
Historical bind/freeze/latency/recovery causes, active-download/composed-GOST/
partial-piece-crash/external/live acceptance, broader lifecycle/native minimum-OS/
benchmark/soak, localization/native quality, complete provenance/reproducibility,
final whole review/build/allocation/signing/notarization/signed-v3 acceptance,
guarded installation and authenticated GitHub/wiki/screenshots/prerelease
publication/readback remain pending. ARM and Rosetta on this host are not native
minimum-OS proof. No app/profile/server/VM/key mutation, installation or publication;
production signed feeds remain last.
