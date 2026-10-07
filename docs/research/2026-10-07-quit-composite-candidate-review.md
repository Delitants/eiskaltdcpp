# Composite Torrent Quit Candidate Review

## Scope And Verdict

One fresh-context, read-only independent review covers committed range
`dd088f2b1..db94c22e3`, with the seven existing local tracked retirement-repair
files and test-only `PublisherAllocationFailure.cpp` overlaid. Snapshot:
`Work/qa/quit-candidate-review-20261007/candidate`; evidence and original
reviewer output are retained beside it. This is the current composite Torrent
retirement/integration candidate, not the entire updater or prerelease diff.

The reviewer found no Critical or Important source issue and judged the
reviewed source mergeable conditional on parent identity/provenance checks.
Those checks pass. The source review gate is complete, not integrated Task5,
GUI-settings Task3, updater Tasks7/8, native/live or release acceptance.

The single Minor recommendation is deferred: add established TCP/UDP payload
exchange across Fence before physical Proxy retirement. The queued-DNS case
and physically held setup/reaper cases do not directly protect the
`setupComplete` exemption with an already-established upstream relay.
Source inspection confirms the exemption is retained; no actual relay
interruption was demonstrated. This is nonblocking coverage debt, not a
production defect or authorization for an unplanned change.

## Coverage

The reviewer inspected changed source/headers/tests, the approved nonblocking
quit, settings-freeze and GUI compatibility specifications, parent Review Focus,
repair plan and relevant snapshot dependencies. Checks included:

- Publisher allocation-free token ownership, cancellation and checked retraction.
- Engine main/local-verification session destruction on exceptional worker exit.
- Proxy pre-launch ownership, physical exit receipts, setup fencing and relay lifetime.
- Runtime sticky failure, dependency phases and no premature core/route retirement.
- MainWindow cached layouts/settings persistence and physical producer completion.
- Session/coordinator deadline, retry and installer-consent boundaries.
- Diagnostic privacy/formatting, paired-port ownership and migrated GUI observers.

The reviewer performed no edits, execution, builds, networking, live operations
or further delegation. A second review was not dispatched. This is source
review, not independent execution of the supplied test counts.

## Fresh Parent Verification

The snapshot has 3,486 regular files, all hash-verified before and after review.
The exact local seven-file patch and empty index are checked; checkout status
is identical before and after review. Existing repairs remain local/separate.

All 101 frozen GUI matrix pins verify. Of its 34 repository source pins, 31
still match the candidate; three explicitly differ by committed native
diagnostics: `TorrentEngine.cpp`, `test_torrent.cpp` and
`test_torrentruntime.cpp`. These were separately tested in the diagnostic
checkpoint; the old full GUI matrix is not silently relabeled as a full
current-source matrix. The initial checker omitted the third expected delta,
refused, and was corrected only after exact git/frozen-source readback.
The failed QA expectation is retained in `provenance-preflight.txt`.

All 47 GUI matrix receipts reparse as 6,729 instances / 621,195 assertions,
zero failures/errors/skips, with all 37 inventory comparisons retained.
The sealed paired-port receipts remain 1,520 ordinary Qt instances / 68,566
assertions, zero failures, with exact RED/broken-control contracts. No test
suite was re-executed during this read-only review task.

All 275 incremental regular-file link inputs and current GREEN source copies
verify. The GUI 427-file and swarm 143-file seals verify, as do earlier
84/194/73-file seals and the original failed 519-file checkpoint.
These remain ARM/macOS 27 and Intel/Rosetta evidence; not native minimum-OS,
current consistent whole-app rebuild or installed acceptance.

## Review Dispositions

- Identity/provenance assigned to the parent was verified as above. Binary
  coverage limits and source deltas remain explicit; misapplying the verdict
  to different bytes would invalidate it. A consistent final build is required.
- Historical ARM bind cause, Intel UDP `61112` conflicting owner and
  `102/180/354ms` timing causes remain unavailable/unwaived. Later GREEN does
  not resolve them; intermittent failures may still recur.
- Native minimum-OS/native Intel/live GUI/real-peer/benchmark/soak are outside
  this static pass and remain gates; supported-host behavior may differ.
- Repository-wide security, native-language quality, reproducibility and
  release artifacts were not discharged. Final updater/prerelease whole-diff
  review remains separate; unrelated release risks must not be inferred closed.

The deferred Minor above and all four set-aside categories are explicit in
the QA disposition ledger; none is silently dropped.

## Remaining Work

Next: fresh consistent current-source consumers and the complete serial matrix,
without mixing later source changes into old acceptance claims. Task5 still
requires its historical-cause/lifecycle gates; the earlier 94 failures/errors
are preserved, not waived. GUI-settings Task3 and updater Tasks7/8 stay open.

Broader established-relay/peer/bootstrap/selected-check coverage, historical
latency/recovery causes, localization/native quality, provenance/reproducibility,
native minimum-OS/live/benchmark/soak, final reviewed app/material builds,
production build allocation, signing/notarization, signed-v3 acceptance, guarded
closed-app installation/readback and authenticated GitHub/wiki/screenshots/
prerelease publication/readback remain gated. Signed feeds are last.

No application/profile/transfer/key/server/VM/network/feed mutation, installation,
signing, push or publication occurred.
