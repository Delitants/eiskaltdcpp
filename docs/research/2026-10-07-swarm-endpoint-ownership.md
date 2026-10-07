# Swarm Fixture Endpoint Ownership Investigation

## Scope And Finding

Approved investigative spike following the native listener diagnostic checkpoint
`97dfd7af`. No product or checked-in fixture behavior changes. All probe code and
consumers are throwaway artifacts under
`Work/qa/swarm-endpoint-ownership-20261007/`.

The existing swarm fixture's TCP-only port selection can choose an endpoint
already occupied for UDP. Controlled reservations reproduce the original
`observedTraffic` failure and identify the precise injected UDP descriptor on
ARM and Intel/Rosetta, for both v1 and creator-generated hybrid metadata.

The prior natural collision at `127.0.0.1:61112` did not recur in the single
traced full replay. Its conflicting owner remains unknown; the earlier
historical ARM bind error also remains unexplained. Neither failure is waived.
Torrent quit Task5, GUI-settings Task3 and updater Tasks7/8 remain open.

## Native Trace And Controls

The unmodified fixture obtains seed/download ports with temporary TCP listeners,
then releases them before metadata creation and session startup. Its settings
disable DHT and uTP, but the actual SDK still binds UDP on the listen endpoint.
With production retries and system-port fallback disabled, a UDP collision emits
the captured `sock_bind` / UDP / `system:48` error.

A private dyld observer calls the original bind once and preserves its return
value and errno. It records numeric PID/FD, socket type and endpoint. On
`EADDRINUSE`, a read-only same-process descriptor snapshot identifies matching
socket candidates. It does not inspect credentials, paths, other processes or
alter socket settings. Known-owner probes verify actual collision results and
the matching descriptor on both CPUs, with a fresh ARM confirmation.

Each private fixture variant inserts only a UDP reservation on the selected
seed port after the original TCP reservations close. The held variant retains
it for that branch; the released variant closes it immediately. All original
transfer assertions and the 30-second polling deadline are unchanged. Reservation
setup failures are rejected as setup errors, never counted as behavioral proof.

| Run | Case Instances | Assertions | Result |
| --- | ---: | ---: | --- |
| Original traced ARM swarm | 1 | 67 | Pass |
| Released UDP ARM control | 1 | 67 | Pass |
| Held UDP ARM control | 1 | 29 | Exactly 2 expected REQUIRE failures |
| Original traced Intel/Rosetta swarm | 1 | 67 | Pass |
| Released UDP Intel/Rosetta control | 1 | 67 | Pass |
| Held UDP Intel/Rosetta control | 1 | 29 | Exactly 2 expected REQUIRE failures |
| Single full ordinary Intel/Rosetta Qt replay | 756 | 34,218 | Pass; no natural collision recurrence |

Both held branches fail only `observedTraffic`, with zero downloaded payload,
the expected native UDP bind error and the exact injected PID/FD/port captured
as the matching owner. No errors or skips occur. The full replay retains the
previous complete case inventory. Its intentionally occupied-port diagnostic
tests also exercise the observer, but are not natural recurrence evidence.

All runs are serial with disposable HOME, explicit provider/dyld readback,
offscreen Qt and verified external-network denial under loopback-only Seatbelt.
The controls replace only the complete swarm test translation unit in pinned
incremental consumers. Observer/probe code is built for both architectures.
There are 269 unique verified control link inputs; the preceding 297 diagnostic
inputs, source snapshots and 194/73/519-file evidence seals still verify. The
pre-existing seven-file uncommitted repair patch remains byte-identical.

Two setup mistakes remain recorded: an observer compiler signedness warning,
and a duplicate probe label refused with exit 3. The original aggregate driver
retains exit 1 for that label refusal. The prior successful probe and fresh
confirmation are preserved; the separate checker validates every actual receipt
and the exact expected behavioral failures without relabeling the driver exit.

This is ARM/macOS 27 and Intel under Rosetta, not native minimum-OS acceptance,
independent candidate review, final app rebuild, installation or publication.
Observer overhead can affect scheduling; a passing observed replay cannot prove
the original unobserved socket ownership. No live app, profile, key, server, VM,
production network or signed feed change occurs.

## Recommended Next Bounded Design

Repair only the swarm fixture's endpoint ownership. Reserve both loopback TCP
and UDP for each candidate, reject an occupied UDP candidate, and retain both
reservations throughout metadata setup. Release each pair immediately before
its corresponding session starts. Verify actual session TCP/UDP listener
readiness while preserving the existing transfer assertions and deadlines.
Add deterministic reservation-rejection, lifetime and handoff checks.

This narrows the handoff window but cannot atomically give Qt descriptors to the
SDK. Any bind failure after release must remain explicit, not be masked by a
session restart, relaxed assertion, skip, production retry or direct fallback.
Other fixtures and production randomized/fixed-port policy are outside scope.
Implementation needs approval of this bounded design; the spike artifacts are
not a retained repair.

Broader independent review, owner/lifecycle and historical timing causes,
localization/native quality, provenance/reproducibility, real-peer/selected-check/
bootstrap, native minimum-OS/live/benchmark/soak gates remain pending. Final
reviewed rebuild, numeric production build, signing/notarization, signed-v3
acceptance, guarded installation/readback and authenticated prerelease/wiki/
screenshots publication/readback remain gated. Signed feeds stay last.
