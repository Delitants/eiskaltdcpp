# Selected-File Recheck With An Active Peer

## Result

Verification-only continuation of the approved selected-recheck peer/quiescence
contract. Candidate is `0f3bd708f` plus the preserved seven-file local repair.
No production or checked-in test-body changes. Private evidence is retained under
`Work/qa/selected-active-peer-20261007/`.

The new private active-upload probe passes on ARM/macOS27 and Intel/Rosetta.
The wider checkpoint is **NOT_ACCEPTED**: one unchanged Intel large-scope case
refuses its shutdown checkpoint. A separately traced pass does not waive it.

| Selection | ARM Cases / Assertions | Intel/Rosetta Cases / Assertions | Outcome |
| --- | ---: | ---: | --- |
| Private active-upload peer probe | 1 / 24 | 1 / 24 | Both pass |
| Original selected planner/journal/engine cases | 79 / 1,255 | 79 / 1,255 | ARM passes; Intel has one failed assertion |
| Current Qt selected UI and repair/restart | 11 / 464 | 11 / 461 | Both pass |

All six selections execute 182 case instances / 3,483 assertions: 3,482 passing
and one failing assertion, zero errors/skips/fatal exits. All original selected
case/section inventories match the prior complete matrix. New probe instances
are separate from original inventories. This is not a complete application or
new full core/Qt/Torrent matrix.

## Active-Upload Evidence

A private complete test translation unit preserves every original test byte as
its prefix and appends a hidden augmentation of the existing other-job transfer
case. A real loopback downloader establishes a live peer and nonzero payload
upload on the chosen seed before the selected-file check. During the held disk
check, local and remote peers disconnect, the remote job remains incomplete and
its payload counter stays unchanged while a second job transfers and verifies
matching bytes. Existing exact selected-read, zero-write, progress and shutdown
assertions remain. No product routing, priority, selection or payload behavior
changes; this is private coverage, not a shipped regression test.

The initial ARM probe fails its active-peer prerequisite before any recheck.
A separate trace establishes a live peer but fails the incomplete-download
prerequisite after checking starts. Both failures remain retained. The SDK's
default loopback peer class is unlimited, outside the session's global download
limit. The corrected disposable downloader asserts that actual default mapping,
maps only its loopback peer to the limited global class and asserts readback
before connecting. Its 32 KiB/s limit makes an incomplete live transfer observable
without changing original helpers or production policy. These are fixture
corrections, not a product RED/GREEN repair.

## Unresolved Intel Checkpoint Refusal

`Large selected scopes keep owner admission bounded` uses 10,000 files and
500,000 pieces. Its owner admission takes 35ms, passing the unchanged 50ms bound;
only `engine.shutdownChecked()` fails. The complete Intel selection finishes
79 instances / 1,255 assertions with one failure, no errors/skips. No original
receipt is rewritten and no uninstrumented replay waives it.

One separately labelled Intel diagnostic retains the original case, control flow
and three-second drains. Worker-side telemetry records boolean results, aggregate
pending counts and elapsed time only. It passes 1 / 7 assertions: selected-check
drain takes 2,087ms, no active checks or retained check records remain, local
commit and final checkpoint receipt succeed. This shows the successful path,
not the cause of the earlier refusal. The next investigative gate is capturing
the actual failed drain/commit boundary when refusal occurs, without extending
deadlines or turning refusal into success. No production repair is made.

## Inputs And Preservation

Private consumers compile one complete test translation unit per CPU and reuse
44 verified regular backend link/metadata inputs from the consistent matrix.
Frozen backend source readback and unchanged original test prefixes verify.
The Qt consumers are the preceding final cold binaries with matching hashes.
All parent dyld logs identify the corresponding maintained libtorrent provider.
This is scoped relinking, not fresh whole-candidate or complete provenance proof.

Two preparation failures remain recorded: checking an unrelated old aggregate
GUI source pin, then losing implicit same-directory header lookup when moving
the copied source. Corrected scope and quoted-header paths use new output labels;
no failed log/source is overwritten. Existing compiler deprecation warnings are
retained. Original timing budgets and assertions are not relaxed.

Runs are serial, seed `594128903`, disposable HOME/XDG/TMP, offscreen Qt, no
interposer and verified direct external-network denial under loopback-only
Seatbelt. No timing run overlaps compilation or heavy hashing. Original dirty
patch and candidate HEAD verify before documentation edits. Historical whole
app trees/strict signatures, five archive/notice pins, 58 provider resources and
9/17/8/3 protected/audit/negative manifests verify before and after. New evidence
has an exact 95-file final verified seal, including nine disposable homes.
An own assertion-count correction to the ruling note raced the first seal.
Its exact original bytes and first seal are retained; all other 90 original pins
verify unchanged. The corrected final inventory includes the recovery/readback
record, without rewriting any test, failure, binary or first-seal receipt.

Results SHA-256:
`a79b05bc3e23d93fb80ba09ed8c6e32c3eeaf08af843684dd23f28a4f390b0f0`.
Evidence seal SHA-256:
`c34be12fac52b0c4cc4f68c8c6f25e5b93c09ce4700e6d93975ef96a4dc70e43`.

## Remaining Gates

Controlled active-upload quiescence and other-job continuity are demonstrated.
Original journal crash-boundary/cancellation/read-only/stopped cases retain their
coverage, but the new Intel shutdown refusal remains unresolved. Active-download
quiescence, combined selected checks over GOST, partial-piece network crash
recovery and external/live/minimum-OS acceptance are not claimed here.

Overall Torrent Task5, GUI-settings Task3 and updater Tasks7/8 stay open.
Historical bind/freeze/latency/recovery causes, broader lifecycle/native/
minimum-OS/benchmark/soak, localization/native quality, complete provenance and
reproducibility, final whole review/application/material builds and production
build allocation, signing/notarization/signed-v3 acceptance, guarded installation
and authenticated GitHub/wiki/screenshots/prerelease publication/readback remain
pending. No app/profile/key/server/VM changes, signing, installation or publication.
Production signed feeds remain last.
