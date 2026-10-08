# Established GOST Relay Across The Quit Fence

## Result And Scope

The approved bounded test-only regression passes on ARM/macOS 27 and
Intel/Rosetta. It addresses the established TCP/UDP relay coverage recommendation
from the composite source review, without changing production routing, admission,
retirement or deadlines. No relay-interruption defect was demonstrated in the
unchanged implementation.

Evidence: `Work/qa/established-relay-fence-20261007/`. Starting HEAD is
`3c343b703`, with the same seven-file local repair candidate as the preceding
consistent matrix. The entire 56,879-byte existing adapter-test prefix is
preserved; only the new case is appended. The curated case-only commit excludes
the earlier queued-DNS test and every other existing repair hunk.

## Behavioral Contract

`Proxy quit fence preserves established authenticated GOST payloads until retirement`
executes TCP and UDP generator values against real local sockets and authenticated
TLS GOST protocol fixtures. Distinct literal binary payloads establish delivery
before Fence, after Fence and after a repeated Fence. The listener rejects a new
connection while existing control, route, worker and reservation ownership remain.

The subsequent physical retirement request must return promptly, reach Ready,
leave no workers/reservations/route and close the control connection. Repeated
retirement remains Ready and terminal restart is rejected. Fixture wire-auth
observations are read after joining; declaration order retains safe cleanup when
an assertion fails. No product test hook or new helper method is introduced.

The break this test catches is premature cancellation of established work by the
setup fence. A private adapter copy omits only `*setupComplete = true`. Both CPU
controls pass initial exchange and setup checks, then fail exactly two
`exchange(afterFence)` assertions, one for each generated transport. No unrelated
failure, error, skip or fatal exit is accepted. Production source is never mutated
to construct these controls.

## Verification

| Run | JUnit Case Instances | Assertions | Result |
| --- | ---: | ---: | --- |
| Private RED, per CPU | 1 | 34 | Exactly 2 expected payload failures |
| Focused GREEN, per CPU | 7 | 166 | Pass |
| Complete ordinary ARM Qt | 761 | 34,552 | Pass |
| Complete ordinary Intel/Rosetta Qt | 761 | 34,543 | Pass |
| Complete ordinary total | 1,522 | 69,095 | Pass |

All 760 original ordinary cases are retained per CPU; only the one new case is
added. Both CPU inventories match. Catch2 JUnit coalesces both generator values
into one node; the initial contract's two-node expectation is corrected explicitly
in the QA contract without changing transport values, assertions or RED failures.
Assertion totals may vary with observed paths; they are not a unique-test count.

Runs are serial under the existing disposable HOME/XDG, offscreen Qt and verified
loopback-only external-network denial runner. Original seed 594128903, 100ms
request checks and existing payload/retirement polling deadlines remain intact.
Compilation and heavy checksum work do not overlap suite execution.

Four private consumers freshly compile the whole changed adapter-test translation
unit (including production adapter source), linking the pinned retained
109-object/24-library inventory per consumer. This is incremental Qt verification,
not a new whole-source/application rebuild. All 291 captured current inputs and
2,866 retained prior-matrix inputs verify; the sole intentional live-source delta
is the appended test. Actual minimum targets are ARM 26 and Intel 14, SDK 27.

Protected historical bundle trees/strict signatures, five material pins, 58
provider resources and protected/audit/negative manifests verify before and after.
The prior 5,790-file current-matrix seal verifies, along with the 84/194/73-file
historical seals, the original failed 519-file checkpoint and 275 retained swarm
link inputs. No failed historical receipt is relabeled or waived.

One fresh read-only independent review finds no Critical, Important or Minor
issue in the bounded new test and private-control approach. Its verdict is scoped
source review, not independent execution or whole updater/prerelease acceptance.

## Remaining Gates

The deferred established authenticated GOST TCP/UDP Fence coverage is addressed
for this candidate. Original ARM bind cause, Intel UDP `61112` conflicting owner,
historical `102/180/354ms` stalls and natural recovery causes remain unwaived.
Overall Torrent Task5, GUI-settings Task3 and updater Tasks7/8 stay open.

Broader owner/lifecycle, real peer/bootstrap/selected-check, native/minimum-OS/live,
benchmark/soak, localization/native quality and complete dependency provenance/
reproducibility remain pending. The preceding full 47-run matrix is preserved,
not rerun or silently extended to the new test. Final whole updater/prerelease
review, reviewed app/material builds, numeric production allocation,
signing/notarization, signed-v3 acceptance, guarded closed-app install/readback
and authenticated GitHub/wiki/screenshots/prerelease publication/readback remain
separate. Earlier local source repairs stay uncommitted. Signed feeds last.

No installed app/profile/transfer/key/server/VM/network configuration or feed
mutation, release signing, installation, push or publication occurs.
