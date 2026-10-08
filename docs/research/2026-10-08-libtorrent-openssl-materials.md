# OpenSSL-Isolated Libtorrent Material Refresh

## Current Build Selection

The [earlier material checkpoint](2026-10-07-current-libtorrent-materials.md)
precedes the [OpenSSL header-isolation repair](2026-10-08-openssl-header-isolation.md).
It remains preserved, but does not match the new provider binaries. A real
selection check against it fails exactly five tests: current provider bytes,
built-consumer byte receipts, isolation recipe files, generated header materials
and the independent OpenSSL witness. The current source lock/license still pass.

Fresh materials now match the verified OpenSSL-isolated ABI-2 providers and
built consumers, without recompiling or launching them.

| Readback | Apple Silicon | Intel |
| --- | ---: | ---: |
| Installed source-matching headers | 295 | 295 |
| Source inputs named by compiler dependencies | 697 | 697 |
| Accounted compiler objects | 285 | 285 |
| Matching members in three embedded static archives | 106 | 106 |
| Generated OpenSSL compiler-header inputs | 77 | 77 |
| Remaining external compiler-input receipts | 3,314 | 3,319 |
| Built consumers selecting the exact provider | 4 | 4 |

Five built test consumer binaries per CPU match the previous sealed readback;
four select the current provider path and encode its actual install ID. The
core-only binary has no libtorrent import. Unbuilt link rules are not counted.
The built/installed provider bytes, raw link and CMake configuration hashes,
object/dependency hashes, static archive bytes and member payloads match the
preceding repair evidence. Legitimate duplicate static member filenames remain
separate members, checked against actual linked objects.

## Reconstructed Source And Modes

The checksum-pinned libtorrent 2.1.1 archive and unchanged selected-recheck patch
reconstruct all 1,653 modified-source files without patch fuzz or offsets. All
source bytes match the current retained build source. Its private extraction
has owner-only modes `600/700`, while the archive reconstruction uses `644/755`.
The first attempt correctly refused that difference under the original exact
mode rule; it remains a failed attempt.

Both inventories are retained. A narrow source validator requires the exact
original mode masked with `0700`; it does not ignore modes generally. Six mode
controls cover valid private read/executable mapping and byte changes, incorrect
execute bits, extra permissions and unexpectedly unmasked source modes. Three
exact RED failures precede the repair. Installed headers and staged/material/
archive modes continue to use strict, unmasked inventory comparison. No source
permissions are changed in the existing build tree.

## Refreshed Material Set

A fresh private 2,002-file set retains:

- Original pinned source archive, current source lock, patch and build guide.
- Complete reconstructed modified sources and both source-mode inventories.
- Twelve selected standalone notices, byte-identical to the original archive.
- Current maintained provider recipe and both header-isolation CMake rules.
- Both CPUs' raw configuration/link/flag receipts, staged OpenSSL namespace and
  origin records, generated refresh scripts and built consumer link receipts.
- Current provider/source/object/input inventories and the sealed build and
  [independent public-header regeneration](2026-10-08-openssl-generated-headers.md)
  readback witnesses.

All 142 staged OpenSSL public headers per CPU match the independent witness;
77 are named by the provider's actual dependency records. The source/header
comparison and final input check pin 12,753 current file inputs. External
Boost/OpenSSL/SDK inputs remain pinned readbacks, not a newly completed external
source/notice superset.

Two independently written tar archives of this frozen set are byte-identical:
`66cf7ec2cfecb16dfe8d566798346b3ad32a86bced6b943d261d0d958d6c4495`.
Every member name, content hash, permission mode, normalized owner and timestamp
is read back. This demonstrates repeatable material packing, not reproducible
provider/application binaries or cross-host builds.

## Verification And Preservation

Forty-one local selection/mode/material/log/review controls and 30 existing release-
profile fixtures pass. Refusal controls cover changed source bytes/modes,
missing notices, extra files, symlinks, retry overwrite, altered archive payloads
and duplicate distribution members. Release-profile fixtures use small Mach-O
and ad-hoc-signature test assets, not production signing or a full application.

The first temporary-mode fixture used a noncanonical macOS temporary-root alias;
its setup failures remain separately retained, not behavioral RED proof. An
initial release-profile invocation omitted its required repository argument;
that harness failure is also retained separately from the successful run.
The first aggregate log gate rejected legitimate unittest failure tracebacks.
Separate RED cases cover that rejection and diagnostic words echoed in failure
source lines; seven log controls now require the exact
failure identities, count and expected traceback count, rejecting contaminated
logs, wrong identities/counts, errors and extra summaries.

Independent review found two audit gaps: special permission bits were masked
away, and release-profile dot logs could accept an appended failure row. Both
are repaired. Eleven review controls reproduce ten exact RED failures against
the retained pre-fix helpers, then all pass with the repaired helpers. Full mode
readback rejects setuid/setgid/sticky bits on files, directories, roots and
expected private-source mappings. Release logs require exactly 30 success dots
and refuse injected status rows or altered/duplicate progress records.

Initial sandbox permission fixtures silently lost setuid/setgid bits. That
failure is retained separately; the authoritative replay and passing controls
assert actual mode readback outside the sandbox on disposable user-owned,
non-executable fixture files. No code is executed with special privileges.

Previous immutable checkpoints and inherited dirty runtime/test repairs remain
unchanged. No historical compiler-input proof, loader routing, crash/freeze/proxy
fix or Intel shutdown acceptance is inferred from this material refresh.
No app/profile/key/server/VM modification, signing, installation, GitHub push,
publication or production feed activation occurs.

## Remaining

This closes material refresh for the selected OpenSSL-isolated libtorrent build.
Other dependency/resource/source/notice closure, legacy attribution and whole-
binary reproducibility remain pending. This is not the final release source
archive or complete application material set.

The original Intel shutdown refusal, Torrent5/GUI-settings3/updater7/8,
broader native/minimum-OS/live/lifecycle/soak/platform/translation QA, final whole-
branch review/application/material builds and production numeric allocation
remain. Developer ID signing/notarization, signed-v3 update acceptance, guarded
closed-app installation, authenticated GitHub/wiki/screenshots/prerelease
publication and public readback remain pending. Production signed feeds are last.
