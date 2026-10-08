# Refusal-Only Shutdown Observation

## Outcome

The original Intel/Rosetta large-scope shutdown refusal remains unresolved and
unwaived. This single diagnostic continuation did not reproduce it. Acceptance
is **HELD**, not repaired or complete. No further positive repetitions of this
fixture are planned; continue independent release-gate work instead of retrying
it toward acceptance.

Candidate is `f142f63f8` plus the unchanged seven-file local repair. One private
Intel consumer ran the original planner/journal/engine selection with seed
`594128903`: **79 case instances / 1,255 assertions**, zero failures, errors or
skips, 102.152 seconds. The case/section inventory matches the original failed
receipt exactly. Original assertion expressions, helpers and three-second drain
deadlines remain unchanged.

Private evidence is retained in `Work/qa/selected-post-exit-20261007/`.

## Different Observation Method

Earlier passing probes timed validation and every drain poll. This probe adds
private worker markers only after an existing policy selects a refusal: drain
deadline, retained verification record, close/checkpoint/persistence refusal,
final persistence failure or shutdown exception. There are no per-poll timers,
logging, injected delays, load or validation changes in the normal path.

A test-only observer reads worker-owned state after the original
`CHECK(engine.shutdownChecked())`, guarding collection before inspection. A
pre-fixture case marker isolates this job from intentional negative controls.
The initial preparatory build lacked that delimiter; its unused binary, inputs
and compiler warnings remain retained, with the labelled consumer built in a
separate output. Neither preparation overwrites prior evidence.

Only the private engine/test copies receive observations. Reversing the exact
transformations reconstructs the current engine and the original failing
limited-peer test translation unit byte-for-byte. Code layout still changes,
so the diagnostic is not a latency guarantee or a shipped instrumentation change.

## Actual Large-Case State

The selected scope is 10,000 files / 500,000 pieces. No refusal marker is emitted
inside the large-case interval. Eighteen markers outside it belong to the
existing negative-control selection and are not new assertion failures.

After collection the observer reports:

- Successful worker receipt and cached Ready result.
- No retained main or local verification session.
- No active selected check, in-memory verification record or pending save.
- Terminal verification and a requested final save, without a check error.
- Matching durable/snapshot generations and no pending journal or journal error.

The owner call takes 4,923,605 microseconds. This is **not** the selected-check
drain duration: it includes waiting for accepted queued work, worker processing,
native teardown and final persistence. Comparing it directly with the drain's
three-second budget would misattribute the observation.

## Source Boundary Trace

The unchanged shutdown return passes through these separate gates:

| Boundary | Refusal Conditions |
| --- | --- |
| `shutdownChecked()` / `Impl::stop()` | Request stop, collect the worker, return the cached retirement receipt; owner wall time is not an individual drain deadline. |
| Worker command loop | Accepted commands are processed before the stopped/empty-queue exit; polling and ordinary persistence can precede closing. |
| `drainFileChecks()` | Cancel selected checks; refuse on the original three-second policy or any retained verification record. |
| Local-session close | Require selected-check drain and local persistence. |
| Main-session close | Require selected-check drain, final resume checkpoint and persistence. |
| Final worker persistence | Preserve earlier failure even if the later write succeeds; exceptions also refuse. |
| Thread-exit receipt | Session cleanup precedes receipt readiness; failed persistence is not converted to Ready. |

Importantly, `drainFileChecks()` checks its deadline **after** a complete poll.
An over-budget poll can clear the final active check and journal record yet
still return false. Thus clean post-exit state alone cannot exclude a deadline
refusal. This is a static possible path, not evidence that it caused the original
failed receipt. No guard is removed or widened, and no late update handoff is
authorized by this successful diagnostic.

## Preservation And Limits

The private consumer uses 22 verified Intel link/metadata inputs; all 44 retained
both-CPU inputs and frozen backend sources verify. The runner uses a fresh
disposable HOME/XDG/TMP, matching maintained Intel libtorrent/Qt and verified
loopback-only external-network denial. Compilation and heavy hashing do not
overlap the timed run. All deprecation warnings are retained.

Before/after readbacks preserve both historical app trees and strict signatures,
five archive/notice pins, 58 provider resources and all 9/17/8/3 protected/audit/
negative manifest entries. The original seven-file repairs and prior 95/64/54-file
diagnostic evidence sets remain intact. The catalog repair's 88-file evidence
set and all 26 catalog/test pins remain intact; the status document receives an
explicit additive update, not a claim that its historical candidate pin still
matches the evolving ledger.

The new exact seal covers 44 evidence files and one disposable home. Seal
SHA-256: `426cc9fa713edc9c59346071b5ddd99bb5bf1a7d9a2f94b51c1ff829611e25f1`.

No app/profile/service/VM/key mutation, installation, signing, publication or
feed activation occurs. Intel via Rosetta on this host is not native macOS 14
acceptance. This is a bounded diagnostic checkpoint, not a runtime repair,
independent whole-branch review or release verdict.

## Remaining

The actual original refused boundary still needs causal evidence before a focused
runtime repair. Do not repeat this successful fixture to waive it. Overall
Torrent task 5, GUI-settings task 3 and updater tasks 7/8 remain open.

Independent next work is dependency provenance/reproducibility. Historical
bind/freeze/recovery causes, broader lifecycle/selected-check/active-download/
GOST/partial-piece-crash/external/live/bootstrap/benchmark/soak, native minimum-OS
acceptance and inherited unfinished/native-language quality remain pending.
Final whole review, app/material builds, numeric production build allocation,
Developer ID signing/notarization, signed-v3 acceptance, guarded closed-app
installation/readback and authenticated GitHub/wiki/screenshots/prerelease
publication/readback remain pending. Production signed feeds are last.
