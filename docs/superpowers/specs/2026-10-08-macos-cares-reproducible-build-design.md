# Maintained macOS c-ares Reproducible Build Design

Status: Proposed; awaiting written-spec approval. Design only.
Date: 2026-10-08
Baseline: `b9a44e696a81c9b204902e8754f20533ee4cb88c`

## Purpose And Approval Boundary

Move the verified fresh-source c-ares recipe into maintained, opt-in macOS
dependency tooling. Success means a caller can build a new checksum-pinned static
provider, validate its provenance and archive metadata, and explicitly select it
without changing existing providers or ordinary build discovery.

The user approved preparation of this written design, not implementation. The
next gate is review of this spec, then a written implementation plan and execution
selection. No existing dependency prefix, application, profile, production build
recipe, signing material or update feed is modified by this design checkpoint.

## Evidence And Limits

The [fresh-source probe](../../research/2026-10-08-cares-fresh-fixed-mode.md)
establishes four fresh builds, 91 objects per build, paired generated headers,
exact repeated fixed archive bytes per CPU and four offline consumer passes.
Its source archive is c-ares 1.34.8, SHA-256
`c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`.
The selected toolchain is CMake 4.4.2, Apple Clang 21.0.0
(`clang-2100.3.34.2`) and Xcode's macOS 27 SDK. The probe targets macOS 26.0
on arm64 and 14.0 on x86_64.

Intel execution was through Rosetta, not native Intel or the minimum OS. SDK
dependency hashes were post-build readbacks, not immutable pre-compilation SDK
snapshots. Whole installed-prefix metadata, relocatable upstream exports, strict
CMake consumption and complete application reproducibility have not been proven.
Those are new acceptance gates, not inherited passes.

## Alternatives

| Approach | Benefit | Trade-off |
| --- | --- | --- |
| New offline builder plus explicit provider prefix, recommended | Isolates ownership, pins inputs and preserves default discovery | Requires a small manifest validator and opt-in CMake path |
| Override the application's archive tools globally | One central switch | Changes unrelated libraries and does not fix copied object modes or header provenance |
| Rewrite existing installed provider archives | Less new plumbing | Mutates historical evidence and risks mismatched headers, ownership and rollback |

Use the first approach. Reuse the repository's dependency-builder location and
provider-header overlay contracts, not its private QA scripts or workstation
paths. The existing libtorrent builder is a naming/interface precedent, not a
safe-output or reproducibility guarantee to inherit.

## Maintained Components

- `dependencies/c-ares/source.json`: reviewed version, archive digest, upstream
  source identity, expected source inventory and license/notices identity.
- `dependencies/c-ares/macos-toolchain.json`: reviewed versions, resolved tool
  digests, SDK identity, architecture profiles and canonical configure arguments.
- `tools/dependencies/build-cares-macos.py`: Python 3.11+ standard-library
  coordinator, local input validation, safe extraction, build, copied-object
  normalization, native archive invocation and output verification.
- `tools/dependencies/verify-cares-macos.py`: shared schema and artifact validator,
  with a CLI for readback and CMake invocation; no download or repair behavior.
- `cmake/ProxyResolver.cmake`: explicit `EISKALT_CARES_PREFIX` opt-in, preserving
  current config-package/pkg-config discovery when the option is absent.
- `tests/macos/dependencies/`: builder, output ownership and provider-selection
  regressions, registered separately from expensive two-CPU native acceptance.

The implementation will populate tool digests from the sealed evidence and
freshly verify the installed tools before accepting them. Version text alone is
not a pin. No personal path, private proxy endpoint, credential, cache-runtime
location or QA directory becomes a tracked input.

## Inputs And Toolchain Contract

The CLI requires a local source archive, a new output prefix, a new work directory
and one architecture profile. Optional explicit tool paths select installations;
they do not override reviewed hashes or flags. The initial profiles are exactly
the probe's arm64/26.0 and x86_64/14.0 settings. These names describe build targets,
not certified OS compatibility. No universal output is implied.

Preserve the Intel profile's historical duplicate architecture/deployment flags
and `-O2` before Release `-O3`; unrelated flag cleanup requires a separate change
and new comparison. Preserve `HAVE_PIPE2=OFF`, static-only c-ares and disabled
upstream tool/test targets. Build with at most two compiler jobs.

Resolve and record actual invoked tools, dispatcher paths where relevant, selected
developer directory and SDK. Verify their reviewed identities before use and
recheck after processing. Use a clean explicit environment, private HOME/cache/tmp,
and no ambient CFLAGS, include paths or package prefixes. No download, package
installation, global environment change or direct-network fallback is provided.
A clean environment alone is not an OS-level network-isolation claim.

Source extraction accepts only regular files and directories under the expected
single root. Reject traversal, absolute names, duplicate canonical destinations,
links, devices, special permissions and excessive entries/expanded sizes. Hash
all regular files against the reviewed archive before compilation and afterwards.
Reject missing, changed or unexpected source files.

Generated headers and actual compile/dependency records bind objects to this
source, architecture, flags and SDK. External SDK/toolchain inputs get before/after
identity checks and dependency readbacks; this is not immutable snapshot custody
or protection against an adversarial concurrent SDK mutation. A changed or
unreviewed toolchain fails validation and requires a reviewed lock refresh.

## Output Ownership And Failure Behavior

Work and output must be distinct, non-overlapping paths within a caller-selected
owned staging root. Reject symlinks in destination components, dot traversal,
source overlap and existing destinations, including empty ones. Claim new
directories exclusively rather than checking existence and then overwriting.
Existing Homebrew, selected providers and application bundles are never targets.

Keep work directories private (`0700`), containing logs, raw archives, original
objects and failed evidence. Normalize only newly created object copies to
`0644`; leave original payloads and raw containers unchanged. Build the new static
archive with the selected native `libtool -static -D` in validated object order.
Verify every payload, symbol-index body and member header, including zero
date/UID/GID and the expected object/symbol-index modes.

Run upstream install only into an exclusively owned intermediate staging prefix.
Replace only that newly owned stage's raw library with the verified fixed archive;
do not rewrite an existing provider. Check upstream exports for relocation and
unexpected absolute build paths before copying a minimal provider tree to the new
output. Includes must contain fresh generated `ares_build.h` and matching public
headers; build-private `ares_config.h` remains provenance evidence, not an ambient
consumer substitute. Include upstream license material.

Regular provider files use `0644`, directories `0755`, and a reviewed fixed mtime.
The initial metadata epoch is Unix time zero, recorded in the recipe lock; an
ambient `SOURCE_DATE_EPOCH` does not override it. The envelope directory containing
the provider also has its mode and mtime checked.
This contract covers relative inventory, bytes, file kind, mode and mtime, not
filesystem ownership, inode identity, ACL/xattr portability or archive packaging.
Reject unexpected extended metadata in the controlled candidate tree rather than
silently changing external files. Generated package metadata must be relocatable
and path-independent. Full-tree reproducibility is a new gate.

Publish the completion manifest last using exclusive creation within the claimed
output directory. Finalize the manifest/envelope mode and mtime, then repeat
readback before returning success; creating the manifest changes directory mtime
and cannot be the final metadata check. A manifest is valid only after exact
validation of the final tree and all artifacts, not merely because it exists.
No consumer accepts a partial prefix. Interrupted or failed
outputs remain incomplete and require a different new destination on retry.
There is no automatic resume, overwrite, recursive user-data cleanup or plain
rename that can replace an existing destination. Cancellation terminates and
reaps owned build processes without claiming completion.

## Manifest And Consumption

The path-independent manifest records schema/recipe version, source and toolchain
lock digests, profile, library/header hashes, archive inventory and deterministic
provider-file metadata. It includes no wall-clock run date or private absolute
path. Private work receipts retain command paths, logs, timing and provenance.
The completion manifest excludes itself from its payload inventory to avoid
self-reference; the validator independently enforces its schema, `0644` mode and
fixed mtime. It enforces that single file exclusion and rejects extra files.

If `EISKALT_CARES_PREFIX` is supplied, require macOS, the matching single
architecture/deployment profile, the complete manifest, verified archive/headers
and the selected validator. Do not permit a cached imported target or fallback
discovery to satisfy a failed explicit selection. Configure the reviewed package
from that prefix only, and verify the resolved library/includes/system link
requirements against its manifest before assigning `EISKALT_CARES_TARGET`.

Use the existing `eiskalt_use_provider_headers` overlay for dcpp. Explicit-mode
validation runs at configure and again before dependent compilation/linking so
post-configure replacement is not silently accepted. The overlay must prioritize
fresh generated headers even when stale source or ambient headers are visible.
Actual dependency files and link maps prove the consumed paths; target names
alone are not proof. Imported-target transitive requirements must be validated,
including system libraries used by the controlled consumer.

With no explicit prefix, retain current c-ares >=1.18.1 config-package/pkg-config
behavior. Linux, Windows, GTK, regular Homebrew builds and current CI minimum-OS
settings remain unchanged. Wiring release automation to this mode is a later
reviewed promotion, not part of merely creating the builder.

## Verification And Acceptance

These are required future tests; none is claimed executed by this design.

- Unit failures: unsafe/duplicate archive paths, wrong source/tool hashes, missing
  generated header, missing/duplicate object or compile records, wrong CPU/flags,
  altered payload, unexpected warning, malformed manifest and unrecognized schema.
- Ownership failures: existing/partial prefix, symlink destination, work/output
  overlap, cancellation and write failure; no completed manifest and no mutation
  outside owned work/output.
- Provider failures: changed files after configure, stale cached target, ambient
  header/library temptation, profile mismatch and non-relocatable export. All
  fail closed when explicit selection is requested.
- Compatibility fixtures: default discovery unchanged, selected headers precede
  stale search paths, transitive system links intact, clean single-/multi-config
  Ninja dependency ordering and no implicit selection for other platforms.
- Native acceptance: two independent fresh builds per CPU under different umasks,
  timestamps and roots; exact source/object/header provenance, archive bytes,
  complete provider tree metadata and relocated CMake consumers must match.
- Offline consumer readback: CPU, c-ares version/init/fixed DNS parse/truncated
  rejection, actual header dependencies, link map and system-only dynamic links.
  No live resolver query or application/profile execution.

Keep the exact two-case raw-archive RED and fixed-archive GREEN regression. Its
preflight must reject extra failures, errors, skips, mixed status or malformed
receipts before the test coordinator normalizes candidates. Controlled raw
metadata perturbation supplies the intended RED even if future raw tool output
becomes deterministic. Ordinary one-provider builds validate inputs/artifacts
directly; they do not require a deliberately failing test receipt.

Retain exact reviewed native archiver warning cases; reject unexpected diagnostics.
Do not convert exit zero or a broadly matching warning prefix into acceptance.
Independent review covers the immutable patch and ownership/consumption paths
before opt-in provider acceptance. Complete app builds and runtime compatibility
remain separate from this dependency suite.

## Rollout And Remaining Gates

After written-spec and implementation-plan approval, implement isolated tooling
and regressions without selecting it for the app. Review the patch, run maintained
two-CPU acceptance, then separately review explicit CMake integration. Preserve
the selected historical providers and failed evidence throughout.

Other dependencies, preferred-source/license closure, bundle/package determinism,
whole-application reproducibility, native Intel/minimum-OS QA, outstanding runtime
issues and translations remain pending. Production numeric build allocation,
Developer ID signing/notarization, signed-v3 updater acceptance, guarded closed-app
installation and GitHub/wiki/pre-release publication require their own evidence.
Production signed update feeds remain the final publication gate.
