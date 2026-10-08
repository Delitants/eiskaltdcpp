# c-ares Deterministic Archive Probe

## Result

**HELD: the installed native `libtool -static -D` is insufficient by itself.**
Two private candidates per CPU preserve all 91 selected object payloads and their
order, but changing copied input modes from `0600` to `0644` changes the resulting
archives. Each CPU pair differs in exactly 91 member-mode fields and 182 bytes.
No differing byte lies outside member metadata. No production recipe, dependency
provider, source repair, installed app or profile was changed.

The [machine-readable summary](2026-10-08-cares-deterministic-archive-probe.json)
records exact archive hashes and comparisons. Audit base is `c3226e8e5`; private
evidence is under `Work/qa/cares-deterministic-probe-20261008/`. This is an isolated
feasibility probe, not archive or release acceptance.

| Case | CPU Pairs | Object Payloads And Order Match | Changed Fields Per Pair | Whole Archives Equal |
| --- | ---: | --- | --- | --- |
| Native `libtool -static` | 2 | Yes | 92 timestamps, 91 modes | No |
| Native `libtool -static -D` | 2 | Yes | 91 modes | No |

## Native Inputs And Measurements

Eight archives were created from copies of the previously verified fresh c-ares
1.34.8 objects, with no source recompilation. Each input set has 91 objects in
the selected provider's recorded archive order. First copies use timestamp
1,000,000,000 and mode `0600`; second copies use timestamp 1,700,000,000 and mode
`0644`. Only copies are perturbed. The outer QA directory remains private.

The installed tool reports `Apple Inc. version cctools_ld-27037.1`. Actual native
commands, exit codes, input stat/hash records and full member inventories are
retained. Each archive has 92 members including one symbol index. With `-D`, all
date, UID and GID fields are zero; the symbol index mode is `0100644`. Object
modes retain their respective input values. Both input groups have the same
ownership; cross-user ownership perturbation was not tested. No upstream source
revision is asserted to match this installed binary.

No-D pairs differ in 274 bytes; `-D` pairs differ in 182. Complete layout/body
comparison includes symbol-index payloads, member names and padding. All object
bytes and order match the retained per-CPU providers. There is no byte rewriting,
header patching, permission normalization or provider promotion to force equality.

The independent whole-archive gate exits **1** with exactly two CPU equality
failures for the baseline and the same two failures for the candidate. Successful
measurement commands do not waive that required gate.

## Offline Consumers And Retained Fixture Failure

Four small corrected consumers link explicitly to the four `-D` candidates and
pass version, library initialization, fixed valid DNS parsing and truncated DNS
rejection checks. They create no resolver channel and perform no network query.
Symbol readbacks define the called c-ares APIs without unresolved c-ares symbols;
dynamic dependencies are only system `libresolv` and `libSystem`. Link maps name
the candidate archives. Exact commands and dependency files are retained.

The initial four consumers ran successfully but used the source-tree
`ares_build.h`: the quoted include inside `ares.h` took precedence over the
generated-header search path. They are **unaccepted controlled-header evidence**,
retained with the first failed validator. A consumer-only correction makes private
include views containing byte-matched upstream headers and generated
`ares_build.h`, then links four new consumers. Dependency readbacks verify those
specific copies. Fixture, archive and header hashes match before and after each
corrected link; no archives are recreated or replaced.

Apple Silicon consumers run natively on the current host; Intel consumers run
under Rosetta. Deployment flags target macOS 26 and 14 respectively, but these
are not actual minimum-OS or native-Intel execution checks, nor complete app builds.

## Controls And Custody

Fourteen named synthetic member-report controls pass, covering inventory/order/
payload substitutions and omissions, metadata types and nonzero date/owner fields.
Complete reports with either input mode are valid measurements, not evidence of
whole-archive equality. These are report-level controls, not raw-container mutation
tests. Native archive payload/layout results are independently recomputed from
actual files. Final verification requires the exact retained failure names/counts,
four corrected consumer receipts and the held status.

Custody covers 3,530 enumerated source/repair paths, the inherited source patch,
382 object/header/tool/provider input pins, nine protected profile file hashes and
the entire preceding 3,390-artifact checkpoint. Status documents are append-only;
the prior report remains unchanged. Other historical QA trees and additional
untracked/profile paths are not comprehensively inventoried. Independent bounded
review of the probe and consumer evidence found no material correctness issues;
that review did not cover custody/seal/docs or production integration. Final
checkpoint readback is separate from release acceptance.

## Remaining

A follow-up design must specify fixed object-copy permissions together with
native deterministic timestamps, rather than modifying archive bytes. That
normalization is not implemented or accepted here. Historical retained-archive
and whole-app reproducibility remain held.

Other dependency/resource/preferred-source/notices closure; original Intel
shutdown/bind causes and broader GUI/runtime/native/minimum-OS/language QA; overall
Torrent/settings/updater integration; whole-branch review and complete reviewed
app builds; production numbering/signing/notarization/signed-v3 acceptance;
guarded installation and GitHub/wiki/screenshots/prerelease publication/readback
remain pending. Production signed feeds last. Nothing installed or published.
