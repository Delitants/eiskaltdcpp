# Torrent Shutdown Refusal Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task after approval of the focused diagnostic design. Do not rerun earlier successful large-scope probes as acceptance.

**Goal:** Preserve precise failure-stage observations after worker exit without changing shutdown policy or claiming the original Intel cause.

**Architecture:** Worker-owned fixed-size observations record only existing refusal branches. The owner collects them after the existing completion receipt and physical join, caches the authoritative outcome, then emits one best-effort sanitized diagnostic. Ordinary reconfiguration is excluded.

**Tech Stack:** C++20, Qt, Catch2, existing retained per-CPU consumers.

**Spec:** `docs/superpowers/specs/2026-10-08-torrent-shutdown-refusal-diagnostics.md`.

## Global Constraints

- This is proposed diagnostic work, not an approved causal runtime repair.
- Preserve the dirty seven-file repair and untracked allocation-failure fixture.
- Preserve both three-second budgets, branch order, original test bodies/helpers,
  exact assertions and timing limits; no polling retries until a pass.
- `QuitRetirementResult` and boolean shutdown contracts remain unchanged.
- Only worker writes; owner reads only after ready receipt and physical join.
- No profile/app/service/VM/network changes, installation, signing or publication.
- ARM-host x86_64 execution is Rosetta QA, not native Intel/macOS-14 acceptance.
- Production signed feeds remain last.

## Review Focus

- A configuration close followed by quit must not report a stale failure.
- Multiple failed stages must remain distinct and never turn Refused into Ready.
- A queued or direct diagnostic subscriber may throw or reenter collection;
  cached result and exactly-once emission must already be committed. Direct
  emission catches do not contain later queued receiver exceptions; owned queued
  callbacks need receiver-side containment, external callbacks own their contract.
- An exceptional worker exit before final shutdown is entered needs a distinct
  classification, never a fabricated selected-check/checkpoint drain failure.
- Pending calls must never race worker-owned counters or block awaiting them.
- Diagnostic text must exclude sensitive input even on unexpected exceptions.

### Task 1: Worker-Owned Observation And Post-Exit Delivery

**Files:**
- Modify: `torrent/TorrentEngine.cpp` for fixed-size internal observations and
  post-join delivery; do not change `torrent/QuitRetirement.h`.
- Modify: `tests/torrent/test_torrent.cpp` by appending regressions only.
- Inspect: `eiskaltdcpp-qt/src/TorrentRuntime.cpp` and
  `eiskaltdcpp-qt/src/MainWindow.cpp` diagnostic connections. Modify only the
  minimal owned queued callback if needed to contain its own exceptions; preserve
  inherited runtime changes and do not claim protection for external subscribers.
- Create: private frozen source/build/input/output evidence under a new owned QA
  directory; never overwrite earlier failed binaries or receipts.

**Interfaces:**
- Consumes existing `Impl::run()`, `drainFileChecks()`, `closeSession()`,
  `collectRetirement()`, `workerCompletion` and the engine `diagnostic` signal.
- Produces an internal worker-owned `ShutdownObservation`; public retirement
  status/component and `shutdownChecked()` return type remain identical.

- [ ] Step 1: Freeze exact baseline, inherited patch and original test bodies/
  helper limits; verify retained build inputs before replacing a private TU.
- [ ] Step 2: Append tests for state-commit failure detail, resume checkpoint
  rejection, held Pending/no diagnostic, cached repeated refusal, stale ordinary
  close followed by quit, and a direct throwing/reentrant diagnostic receiver.
  Add a queued receiver-side containment regression that throws inside an owned
  callback and verifies it catches before returning to Qt, plus a pre-final
  worker-exit regression where operation-error delivery escapes `run()` before
  final shutdown is entered. Assert `PreFinalWorkerExit`, Refused, physical cleanup
  and no false drain-stage attribution; retain the existing outer catch policy.
- [ ] Step 3: Run baseline regressions, retaining exact missing-observation RED
  assertions with no setup errors, skips, unrelated failures or altered limits.
- [ ] Step 4: Implement fixed-size flags/counts/times only at existing branches.
  Keep a diagnostic-attempt guard around final worker shutdown, record the last
  attempted stage before throws, and have the outer worker wrapper finalize
  `PreFinalWorkerExit` on a bypassed final boundary before the existing receipt.
- [ ] Step 5: Collect after physical join, cache terminal status and mark emission
  attempted before formatting/delivery. Catch telemetry exceptions; reentrant or
  repeated collection returns the cached result without a second emission.
  Treat queue submission and later queued callback delivery as separate exception
  boundaries. Contain exceptions in owned callbacks, not solely around `emit`.
- [ ] Step 6: Keep the isolated at/after-deadline and journal-residue branch tests
  as controls; add exact observation expectations in a private scripted fixture
  without product fault injection. They are not real disk or causal acceptance.
- [ ] Step 7: Run both-CPU new tests and unchanged engine/checkpoint/retirement
  controls serially. Record original refusal as unresolved regardless of green.
- [ ] Step 8: Independent focused review and one exact RED/GREEN pass for important
  findings; recheck privacy, exception safety, pending reads and stale attempts.
- [ ] Step 9: Preserve source/app/profile/evidence before/after, commit only reviewed
  owned changes after required checks and record remaining integration gates.

### Task 2: Causal Capture Gate, Not An Automatic Retry

**Files:** Append observed evidence to Development Status and a separate private
diagnostic receipt; do not rewrite original XML or earlier passing traces.

**Interfaces:** Consumes post-exit observations; produces a failure-stage report,
not a fix, deadline extension or whole-release acceptance.

- [ ] Step 1: Agree a bounded, previously untested failure capture scenario with
  the maintainer. Do not repeat buffered/post-exit positive setups or add load.
- [ ] Step 2: Preserve any actual failed receipt and identify only flags observed
  for that attempt. A pass leaves the original Intel gate unresolved.
- [ ] Step 3: If a causal failure is captured, propose its focused repair design
  and exact regression before product behavior changes. Otherwise report the
  diagnostic limit and continue independent release work, without retry loops.

## Current Planning Checkpoint

Private branch analysis passes ten cases per CPU; deleting the deadline condition
fails exactly two controls per CPU. No full engine/library/disk run or production
implementation occurred. This plan and spec require review/approval before the
runtime ownership/diagnostic path changes. Original Intel cause, wider QA,
provenance/reproducibility, final review/build/signing/update/install/publication
gates remain open. Signed feeds last.
