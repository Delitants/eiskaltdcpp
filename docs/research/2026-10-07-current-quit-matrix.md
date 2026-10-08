# Current Consistent Quit Matrix

## Result And Scope

The approved current-source rebuild and complete serial matrix pass. Candidate
is `b9338079f` plus the existing seven-file local retirement repair patch and
test-only publisher allocation helper. Source bytes match the independently
reviewed composite; the intervening commit changes documentation only.

Evidence: `Work/qa/quit-current-matrix-20261007/`. All previous QA and the local
repair patch remain unchanged. This closes the current consistent test-build
and matrix gates, not overall Torrent Task5, GUI-settings Task3, updater Tasks7/8
or prerelease acceptance. Historical bind/timing failures remain unwaived.

## Fresh Builds

Empty build directories produce five test consumers and two test-only
interposers for each of ARM and Intel, plus core/Qt/settings consumers in a
whole ARM no-Torrent configuration: 13 consumers total. All configurations
build successfully with the approved presets/dependency prefixes and at most
two compiler jobs. No prior objects are substituted into these consumers.
Only declared private broken controls replace a single object afterward.

Actual Mach-O/cache/Qt readback verifies ARM minimum target 26, Intel target
14, Qt 6.11.1 and the no-Torrent OFF configuration. Execution is ARM/macOS 27
and Intel under Rosetta; native minimum-OS/native Intel acceptance is not claimed.
The QA build number remains 1, not a production allocation. These are test
consumers, not final application/material rebuilds or distributable assets.

## Complete Matrix

All 47 prescribed runs pass, with no failures, errors, skips, fatal exits or
missing receipts. All 37 inventory comparisons pass. Every earlier case/section
path is retained; the only ordinary additions are the two backend diagnostic
cases, one runtime diagnostic Qt case and four paired-port Qt cases.

| Consumer | Case Instances | Assertions |
| --- | ---: | ---: |
| ARM core | 1,111 | 150,529 |
| ARM Qt | 760 | 34,279 |
| ARM Torrent | 244 | 18,605 |
| Intel/Rosetta core | 1,111 | 150,530 |
| Intel/Rosetta Qt | 760 | 34,463 |
| Intel/Rosetta Torrent | 244 | 18,605 |
| No-Torrent Qt | 552 | 23,225 |
| Entire 47-run matrix, including repeated focused/hidden coverage | 6,743 | 621,713 |

Full GUI layouts retain 126/126/124 instances per CPU, with 6,064/5,972/5,878
assertions respectively. The table's ordinary rows are subsets of the total;
the total is not a unique-test count. Assertion counts can vary with observed
paths; inventory comparisons, not count equality, prove retained coverage.

The matrix includes ordinary core/Qt/Torrent, settings, physical component
selectors, publisher allocation and proxy launch cases, all three GUI layouts,
hidden core/settings/auxiliary writes, composed settings-freeze/Torrent
retirement and the nine no-Torrent runs. Only the original sidebar navigation
exclusion applies. Real external interop is not rerun by `~[interop]`.

Original seed 594128903, test bodies, 100ms request budgets, 350ms holds, 5000ms
helper and existing watchdogs are unchanged. Each run uses a fresh disposable
HOME/XDG, offscreen Qt, matching provider/Qt plugins and verified external
network denial with loopback-only access. Compilation and heavy hashing do
not overlap timing runs; no failed-suite replay or source repair is needed.

## Controls And Input Readback

Fresh current-source private controls reproduce exact intended assertion
multisets: engine blocking 14, proxy done-as-exit 9, failed retraction accepted
14, and premature core drain 6. No unrelated failure/error/skip is accepted.
Expected broken-control failures are separate from the passing matrix total.

Compile inventories match the 1,896 direct linked test-consumer objects across
all 13 consumers. All 2,867 captured regular-file source/object/library/metadata
inputs verify, as do 97 frozen source/helper/binary/control pins, the complete
tracked-source manifest plus allocation helper and the original repair patch.
This is captured-input readback, not a complete platform dependency provenance
or reproducibility claim for bare linker/system framework dependencies.

Historical app trees and strict signatures, five archive/notice pins, 58
provider resources and protected/audit/negative file manifests remain intact.
Earlier component, GUI compatibility, swarm and independent-review seals verify,
including the original failed 519-file Task5 checkpoint. No original failed
receipt is rewritten, superseded as a pass or retrospectively explained.

## Remaining Gates

Task5 consistent rebuild and full-matrix steps now pass, alongside its scoped
independent source review. Overall Task5 remains open: original ARM bind cause,
Intel UDP `61112` conflicting owner, historical `102/180/354ms` stalls and
natural recovery causes are unwaived. Later GREEN is not their diagnosis.
GUI-settings Task3 and updater Tasks7/8 remain open.

The deferred established TCP/UDP relay-across-Fence regression, broader
owner/lifecycle, real peer/bootstrap/selected-check, native/minimum-OS/live,
benchmark/soak, localization/native quality, complete dependency provenance
and reproducibility gates remain separate. Final whole updater/prerelease
review, reviewed application/material builds, numeric production allocation,
signing/notarization, signed-v3 acceptance, guarded closed-app install/readback
and authenticated GitHub/wiki/screenshots/prerelease publication/readback remain
pending. Signed feeds stay last.

No installed app/profile/transfer/key/server/VM/network/feed mutation, signing,
installation, push or publication occurred. Earlier source repairs stay local
and separate; this is a documentation/evidence checkpoint only.
