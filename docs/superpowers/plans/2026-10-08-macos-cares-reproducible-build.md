# Maintained macOS c-ares Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task in the user's selected Native mode. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and validate a new pinned, deterministic macOS c-ares provider and offer explicit fail-closed CMake consumption without replacing existing providers.

**Architecture:** An offline Python coordinator builds in owned private work, normalizes copied objects, and publishes a validated new provider prefix. Shared input/archive/provider modules support read-only verification and opt-in CMake selection; default discovery stays unchanged.

**Tech Stack:** Python 3.11+ standard library, CMake, Apple Clang/native libtool, unittest, existing provider-header overlays.

**Spec:** [Approved integration design](../specs/2026-10-08-macos-cares-reproducible-build-design.md), approved in chat after commit `e795b094e`.

**State:** Plan prepared for review; no implementation authorized by this checkpoint. Native execution preference is preserved. The spec's original proposal-status text is historical, not a claim that implementation has begun.

## Global Constraints

- c-ares 1.34.8; source archive SHA-256 `c222b6d681096f9444d2c4863d2c1174019e27cacca0a4a5c114d36dd7d7bf78`.
- Initial reviewed tools: CMake 4.4.2, Apple Clang 21.0.0 (`clang-2100.3.34.2`), Xcode macOS 27 SDK; digests, not version strings alone, control acceptance.
- Profiles: arm64/26.0 and x86_64/14.0; preserve historical Intel duplicate flags and `-O2` before Release `-O3`.
- Static only, `HAVE_PIPE2=OFF`, upstream tool/test targets disabled, maximum two compiler jobs.
- No downloads, ambient CFLAGS/includes/provider fallback, existing-prefix overwrite, automatic resume or user-data cleanup.
- Work is `0700`; provider files `0644`, directories `0755`, metadata epoch zero, including completion manifest and envelope directory.
- No source patching; normalize only new object copies. Preserve raw archives, original objects, selected providers, inherited dirty work and sealed evidence.
- No app/profile mutation, production recipe change, signing, install, push or publication. Production signed feeds remain last.
- Default config-package/pkg-config discovery, other platforms and general CI deployment targets remain unchanged.
- Rosetta results are not native Intel/minimum-OS acceptance; dependency proof is not whole-app or release proof.

## Review Focus

1. Paths with spaces/Unicode and shell metacharacters must remain single argv values and retain correct source/dependency identity (Tasks 1 and 3).
2. Inherited xattrs/ACLs and directory mtime changes must not produce a deceptively complete deterministic prefix (Task 4).
3. A cache switched from ambient to explicit selection, or between explicit profiles, must not reuse the old imported provider (Task 5).
4. Multi-config builds and interface wrapper targets must validate the real per-config archive and transitive links before compilation (Task 5).
5. Interrupted install or manifest publication must reap owned processes and leave no valid completion; retry must not reuse the partial prefix (Tasks 2 and 4).

## File And Interface Map

All Python types below live in their owning module; CLI imports use the adjacent
`cares_macos` package, never a private QA module or caller working directory.

| Path | Responsibility |
| --- | --- |
| `dependencies/c-ares/source.json` | Version, archive/root inventory hashes and license identity |
| `dependencies/c-ares/macos-toolchain.json` | Reviewed tools/SDK, recipes, member order and expected profile identities |
| `tools/dependencies/cares_macos/__init__.py` | Package marker, no side effects |
| `tools/dependencies/cares_macos/inputs.py` | Lock/schema validation, extraction and fresh build provenance |
| `tools/dependencies/cares_macos/archive.py` | Archive parsing, copied-object normalization and native archive validation |
| `tools/dependencies/cares_macos/build.py` | Reusable fresh raw-build phase; no provider publication |
| `tools/dependencies/cares_macos/provider.py` | Exclusive ownership, process lifecycle, deterministic prefix and read-only validation |
| `tools/dependencies/build-cares-macos.py` | Offline CLI and phase coordination |
| `tools/dependencies/verify-cares-macos.py` | Read-only CLI returning canonical verified selection JSON |
| `cmake/CaresProvider.cmake` | Explicit selection and build-time validation helpers |
| `cmake/ProxyResolver.cmake`, `cmake/ProviderHeaders.cmake` | Opt-in discovery and validation ordering |
| `tests/macos/dependencies/test_cares_*.py` | Focused standard-library regression suites |
| `tests/macos/dependencies/cares_acceptance.py`, `cares_offline.c` | Separate expensive two-CPU acceptance and offline consumer |
| `tests/CMakeLists.txt`, `tools/dependencies/README.md` | Focused test registration and opt-in operator instructions |

Common failure type: `inputs.BuildError(code: str, message: str)`, with a stable `code`
and concise message, not raw environment dumps. CLIs return 0 only for verified
success, 1 for input/build/verification failure, 2 for invalid arguments, and 130
after checked cancellation. Codes are `source`, `tools`, `archive`, `ownership`,
`build`, `provenance`, `metadata`, `manifest`, `selection`, or `cancelled`.
All diagnostics/log paths remain in private work. Test snippets below show the
decisive assertions; each named suite owns its temporary fixtures and imports.

### Task 1: Pinned Inputs, Extraction And Compile Provenance

**Files:** Create both lock files, `cares_macos/__init__.py`, `inputs.py`, and `test_cares_inputs.py`.

**Interfaces:**
- `load_locks(source_lock: Path, tool_lock: Path) -> Locks`; `Locks` exposes `source`, `tools`, `sdk`, `profiles`, `member_order`, `metadata_epoch`.
- `verify_tools(locks: Locks, selected: dict[str, Path]) -> dict[str, ToolIdentity]`; `ToolIdentity` contains role, invocation path, resolved path, version and digest.
- `extract_source(archive: Path, destination: Path, locks: Locks) -> dict[str, str]`: relative file name to SHA-256.
- `audit_build(source: Path, build: Path, locks: Locks, profile: str) -> BuildProvenance`; contains ordered `objects: list[Path]`, payload hashes, generated header hashes and canonical compile/dependency records.

- [ ] **Step 1: Write failing tests** `test_pinned_source`, `test_tool_digest_not_version`, `test_archive_paths`, `test_compile_inventory`, `test_spaced_unicode_roots`. Assert version/digest above, exactly 553 source entries/532 regular files, one-to-one 91 object/compile records, and rejected links, traversal, duplicate canonical names, special modes, missing headers/dependencies, wrong CPU/flags and bool-valued numeric fields. For hostile argv characters assert no shell execution and exact dependency paths.

```python
self.assertEqual(locks.source["version"], "1.34.8")
self.assertEqual(len(extract_source(archive, destination, locks)), 532)
self.assertEqual(len(provenance.objects), 91)
```
- [ ] **Step 2: Run RED** `python3 -B -m unittest discover -s tests/macos/dependencies -p test_cares_inputs.py -v`. Before implementation expect missing `cares_macos.inputs`; retain the exact result, not a blanket nonzero waiver. After API creation, negative cases must fail for their named contract rather than unrelated exceptions.
- [ ] **Step 3: Implement interfaces and reviewed locks.** Derive the complete source inventory from the pinned local archive and canonical recipe/member order from sealed probe evidence, checking current bytes before copying values. Store path-independent role/SDK identities and profile flags. Pin actual invoked CMake, Python, compiler dispatcher/resolved compiler, make, xcrun, libtool, nm and otool; additions absent from prior pins require fresh digest readback and review. Use a 5,000-entry/50,000,000-byte extraction ceiling, exclusive regular-file creation, and verify the exact source tree before/after build. Parse compile argv and escaped dependency files without shell splitting or substring-based origin removal; label only proven source/build/SDK roots. Unexpected tool/SDK inputs fail rather than refreshing locks automatically.
- [ ] **Step 4: Run GREEN** the Step 2 command; expect `OK`, every intended test executed, no unexpected errors/skips. Verify installed tool identities and sealed evidence again; SDK dependency readbacks must not be described as immutable snapshots.
- [ ] **Step 5: Commit** only these files after focused self-review: `feat: add pinned macOS c-ares input validation`.

### Task 2: Exclusive Staging And Owned Process Lifecycle

**Files:** Create `provider.py`, `build-cares-macos.py`, and `test_cares_ownership.py`.

**Interfaces:**
- Consumes Task 1's `BuildError`, `Locks`, and verified tool identities.
- `claim_paths(root: Path, work: Path, prefix: Path, source_archive: Path) -> OwnedPaths`; records newly claimed directories and open directory ownership handles.
- `run_owned(argv: list[str], env: dict[str, str], cwd: Path, log: Path) -> None`: checked exit, cancellation/termination and reap; no shell or detached children.
- CLI: required `--source-archive PATH --staging-root PATH --work-dir PATH --prefix PATH --profile arm64|x86_64`; optional `--jobs 1|2` and repeated `--tool ROLE=PATH`. No lock override or deployment-floor override.

- [ ] **Step 1: Write failing tests** `test_existing_even_empty_refused`, `test_symlink_component_refused`, `test_claim_race`, `test_overlap_refused`, `test_owned_cancel_reaps`, `test_no_ambient_environment`. Assert no pre-existing bytes/modes change, no valid manifest appears on failure, only one concurrent claim succeeds, distinct destinations remain under the owned root, and a fixture child/grandchild is reaped on cancel. Reject source/work/output overlap and unsupported profile/jobs; ignore ambient `SOURCE_DATE_EPOCH`.

```python
prefix.mkdir()
with self.assertRaises(BuildError) as caught:
    claim_paths(root, work, prefix, archive)
self.assertEqual(caught.exception.code, "ownership")
self.assertEqual(list(prefix.iterdir()), [])
```
- [ ] **Step 2: Run RED** `python3 -B -m unittest discover -s tests/macos/dependencies -p test_cares_ownership.py -v`; expect missing ownership API, then verify each contract-specific failing case before repair.
- [ ] **Step 3: Implement ownership and CLI validation.** Open validated directory components without following links, claim new work/output exclusively, and create private HOME/cache/tmp. Pass explicit clean argv/environment from locked profiles. Use one owned process group with bounded terminate/kill and checked wait on cancellation; retain failure logs. Until Tasks 3/4 exist, the CLI reports incomplete implementation and cannot emit a success manifest. Never remove a user-selected partial directory on retry.
- [ ] **Step 4: Run GREEN** the Step 2 command plus Task 1 suite; expect `OK` and zero surviving fixture processes. Recheck claimed parent identity before writes; tests must cover symlink/parent substitution, not only an initial existence check.
- [ ] **Step 5: Commit** only Task 2 files: `feat: add exclusive c-ares staging and owned build processes`.

### Task 3: Native Archive Construction From Fresh Copied Objects

**Files:** Create `archive.py`, `build.py`, `test_cares_archive.py`; modify the build CLI to configure/build and audit fresh inputs.

**Interfaces:**
- Consumes `audit_build`, `BuildProvenance`, `run_owned` and `OwnedPaths`.
- `build_raw(owned: OwnedPaths, locks: Locks, tools: dict[str, ToolIdentity], profile: str, jobs: int) -> BuildProvenance` in `build.py`; never normalizes or publishes a provider.
- `read_archive(path: Path) -> list[ArchiveMember]`; each member has name, date, uid, gid, mode, payload bytes and offset.
- `normalize_archive(provenance: BuildProvenance, copies: Path, output: Path, tools: dict[str, ToolIdentity], run: Callable) -> ArchiveProof`; records source/copy facts, native member inventory and output SHA-256.
- `require_exact_red(code: int, report: str, pairs: dict[str, tuple[Path, Path]]) -> None`: acceptance-test guard, never an ordinary builder prerequisite.

- [ ] **Step 1: Write failing tests** `test_payload_order_unchanged`, `test_fixed_member_metadata`, `test_unknown_warning_refused`, `test_exact_red_guard`, `test_raw_inputs_preserved`, `test_escaped_dependency_names`. Assert 91 object members plus the symbol index, dates/UID/GID zero, object mode `0644`, symbol-index mode `0100644`, identical symbol-index body, original hash/stat preservation, and rejected malformed/truncated/duplicate/out-of-order payloads. Exact-RED negatives include errors, skips, mixed results and extra headers; normalization callbacks must remain uncalled for rejected receipts.

```python
members = read_archive(candidate)
self.assertEqual(len(members), 92)
self.assertEqual(members[0].mode, 0o100644)
self.assertTrue(all(m.mode == 0o644 for m in members[1:]))
self.assertTrue(all((m.date, m.uid, m.gid) == (0, 0, 0) for m in members))
```
- [ ] **Step 2: Run RED** `python3 -B -m unittest discover -s tests/macos/dependencies -p test_cares_archive.py -v`; retain exact missing-API/contract failure evidence.
- [ ] **Step 3: Implement parser, normalizer and build phases.** Use Task 1's ordered actual records, exclusive copies fixed to `0644`, selected native `libtool -static -D`, and post-output `0644` mode/hash/stat validation. Do not patch archive bytes. Compare symbol-index/payloads with fresh raw input. Accept only the six reviewed no-symbol warning cases per profile after canonical root mapping; extra stdout/stderr diagnostics fail. `build_raw` invokes the pinned CMake Release/static recipe and rechecks source/tool/SDK provenance; the ordinary CLI calls it before normalization, without a RED-test prerequisite.
- [ ] **Step 4: Run GREEN** the Step 2 command and Tasks 1/2 suites. Use a small real native archive fixture as well as parser/report fixtures; expect all tests executed and `OK`. Raw/fixed two-CPU acceptance belongs to Task 6, not an invented unit-test claim.
- [ ] **Step 5: Commit** only Task 3 files: `feat: build deterministic c-ares archives from verified object copies`.

### Task 4: Deterministic Provider Tree And Read-Only Validator

**Files:** Extend `provider.py` and build CLI; create `verify-cares-macos.py` and `test_cares_provider.py`.

**Interfaces:**
- Consumes Tasks 1-3 proofs and owned paths.
- `assemble_provider(stage: Path, prefix: Path, proof: ArchiveProof, locks: Locks, profile: str) -> VerifiedProvider`.
- `validate_provider(prefix: Path, locks: Locks, profile: str) -> VerifiedProvider`; contains relative library/include/config paths, version, profile and manifest digest.
- Verifier CLI: `--prefix PATH --profile arm64|x86_64`; success prints one canonical JSON selection object, failure prints diagnostics to stderr with no partial success JSON.

- [ ] **Step 1: Write failing tests** `test_exact_tree_and_epoch`, `test_manifest_last_readback`, `test_prefix_relocation`, `test_xattrs_acl_refused`, `test_failure_partial_retry_refused`, `test_missing_generated_header`, `test_manifest_self_exclusion`. Assert epoch 0 and file/directory modes including envelope/manifest, no extra file exclusion, rejected extra/symlink/special files and unknown schemas, no wall-clock/private paths, and unchanged historical providers. Interrupt install/publication at each phase and assert no valid completion plus no late completion after cancel. Give the fixture an inherited xattr/extended ACL; require refusal, not silent stripping of external state.

```python
self.assertEqual((prefix / "manifest.json").stat().st_mtime_ns, 0)
self.assertEqual(prefix.stat().st_mtime_ns, 0)
self.assertEqual(validate_provider(prefix, locks, "arm64"), verified)
```
- [ ] **Step 2: Run RED** `python3 -B -m unittest discover -s tests/macos/dependencies -p test_cares_provider.py -v`; retain named failing contract checks.
- [ ] **Step 3: Implement packaging/validation and complete the CLI.** Install upstream only to the owned intermediate stage, replace only its newly owned library with the fixed archive, and copy the allowlisted public headers, CMake exports and license to the new prefix. Omit `libcares.pc`: the inspected upstream template embeds the install prefix and this explicit CMake provider does not need it. Retain that file in private staging evidence; do not patch upstream source or change default pkg-config discovery. Reject non-relocatable CMake exports rather than guessing rewrites. Serialize schema-1 `manifest.json` with locked source/tool/recipe digests, profile, member proof and relative file/directory facts; exclude only the manifest from the payload inventory and check its own schema/mode/mtime separately. Normalize newly owned metadata, reject unexpected ACL/xattrs, write manifest exclusively last, then finalize envelope/manifest metadata and validate the entire tree again before success. No resume or repair mode.
- [ ] **Step 4: Run GREEN** the Step 2 command and prior suites; expect `OK`. Relocate a completed fixture to a distinct owned test root and validate unchanged bytes/metadata/selection. Confirm failed receipts and processes remain private and reaped.
- [ ] **Step 5: Commit** only Task 4 files: `feat: publish and verify deterministic c-ares provider prefixes`.

### Task 5: Explicit Fail-Closed CMake Consumption

**Files:** Create `cmake/CaresProvider.cmake` and `test_cares_selection.py`; modify `cmake/ProxyResolver.cmake`, `cmake/ProviderHeaders.cmake`, `tests/CMakeLists.txt`; create dependency README.

**Interfaces:**
- Consumes Task 4 verifier JSON: version, profile, manifest digest, relative library/include/config paths.
- `eiskalt_select_cares_provider(prefix out_target)` validates the matching single architecture/floor and configures only that verified package; output is a verified imported target assigned to `EISKALT_CARES_TARGET`.
- `eiskalt_guard_cares_provider(consumer cares_target)` adds selected-provider readback before overlay staging and dependent compilation/linking; no-op for default discovery.
- Cache input `EISKALT_CARES_PREFIX` is optional; explicit mode requires Python 3.11+, macOS and the profile's exact deployment target. Empty/unset preserves existing discovery.

- [ ] **Step 1: Write failing tests** `test_explicit_no_ambient_fallback`, `test_cache_provider_switch`, `test_after_configure_tamper`, `test_wrapper_transitive_links`, `test_ninja_multiconfig_order`, `test_default_discovery_unchanged`. Use miniature consumer projects, poisoned ambient providers/headers and per-config imported interface wrappers. Assert missing/modified manifest, wrong architecture/floor, non-macOS explicit selection and stale imported target fail before compilation. An explicit-to-explicit or ambient-to-explicit cache switch must never retain the previous target; permit safe reimport where possible, otherwise require a clean build directory with a precise error. Default static/shared/pkg-config paths retain behavior without Python-3.11 enforcement.

```python
self.assertNotEqual(tampered_build.returncode, 0)
self.assertIn("c-ares provider validation failed", tampered_build.stderr)
self.assertEqual(compiler_invocations_after_tamper, [])
```
- [ ] **Step 2: Run RED** `python3 -B -m unittest discover -s tests/macos/dependencies -p test_cares_selection.py -v`; preserve the exact targeted failure set, not unrelated fixture-tool failures.
- [ ] **Step 3: Implement opt-in selection/guard.** Find package config only at the validated relative config directory with no default paths. Check resolved archive, selected includes and transitive system requirements before target assignment. Reject unexplained generator expressions/locations, rather than accepting a target name. Wire the guard before the existing provider-header overlay for all dependent consumers; an up-to-date build must still reject changed provider bytes. Keep existing non-explicit code behavior and current CMake floors; no JSON parser/version-floor requirement outside explicit mode. Register the five focused suites as macOS CTests, without adding expensive native acceptance to ordinary CI. Document CLI, fresh-prefix retry, evidence boundaries and how to select a validated prefix; no production recipe wiring.
- [ ] **Step 4: Run GREEN** the Step 2 command, all focused suites, and `python3 -B tests/macos/bundle/provider-headers.py "$PWD"`. In the owned configured test build run `ctest --test-dir "$TEST_BUILD" -R 'eiskalt-macos-(cares|provider-headers)' --output-on-failure`; require all intended tests executed with zero failures/skips. Record Ninja single-/multi-config dependency files and link maps. Existing unrelated app/source tests are not automatically claimed rerun.
- [ ] **Step 5: Commit** only Task 5 files after a focused review of the explicit-selection patch: `feat: add explicit verified macOS c-ares provider selection`.

### Task 6: Maintained Two-CPU Native Acceptance And Review

**Files:** Create `cares_acceptance.py`, `cares_offline.c`, `test_cares_acceptance.py`; update `tests/CMakeLists.txt` and the dependency README; append evidence only to `docs/third-party-release-inventory.md` and `docs/wiki/Development-Status.md`.

**Interfaces:**
- Uses Task 3's `build_raw`, `normalize_archive`, exact-RED guard and Task 4's `assemble_provider` plus verifier CLI; no private QA imports.
- Acceptance CLI: `--source-archive PATH --staging-root NEW_PATH`; builds both profiles twice serially with at most two jobs and creates private receipts only.
- Offline consumer: version 1.34.8, library init, fixed DNS question ID `0x1234`, valid parse and truncated rejection; never creates a resolver channel or query.

- [ ] **Step 1: Write failing acceptance-coordinator controls** in `test_cares_acceptance.py`: `test_two_exact_cases_before_normalization`, `test_wrong_failure_set_stops`, `test_tree_metadata_difference_fails`, `test_provenance_not_just_library_hash`, `test_no_skips_as_acceptance`. Assert exactly `test_arm64_whole_archive_equal` and `test_x86_64_whole_archive_equal` fail in the raw phase, no extra errors/skips, both pass after fixed native normalization, and all four consumers run. Deliberately perturb raw metadata in owned test copies if raw output happens to become deterministic; never alter preserved provider/raw evidence.

```python
with self.assertRaises(BuildError):
    require_exact_red(1, mixed_failure_report, raw_pairs)
self.assertEqual(normalizer_calls, [])
self.assertEqual(success_receipt["offline_consumers_passed"], 4)
```
- [ ] **Step 2: Run RED** `python3 -B -m unittest discover -s tests/macos/dependencies -p test_cares_acceptance.py -v`; retain exact expected failures before implementing the coordinator.
- [ ] **Step 3: Implement coordinator and offline consumer.** Vary roots, timestamps and process umasks `077`/`022`; independently extract/build each run through maintained `build_raw`. Run and validate raw RED before calling any normalizer or provider assembler; then normalize/package all four candidates and run fixed GREEN. Compare ordered objects, generated headers, canonical records, archive bytes, complete relative provider tree/manifest bytes and mode/mtime facts, including relocated package consumers. Record executable CPU, header dependency files, archive link map, c-ares symbols and system-only dynamic dependencies. Preserve raw RED and fixed GREEN reports separately; assert real raw inequalities in addition to exact report shape. Register only the lightweight coordinator controls in CTest. No app launch, network test, provider promotion or installation.
- [ ] **Step 4: Run GREEN** focused controls, then `python3 -B tests/macos/dependencies/cares_acceptance.py --source-archive "$CARES_SOURCE_ARCHIVE" --staging-root "$NEW_ACCEPTANCE_ROOT"`. Both variables must identify locally validated inputs/new owned paths, not guessed or reused destinations. Require four fresh builds, 91 objects each, two equal fixed archive pairs, two matching deterministic provider-tree pairs and four offline consumer passes; capture exact platform/Rosetta status. If exports/tree metadata fail, retain evidence and repair the maintained code within scope rather than marking the archive-only probe as full acceptance.
- [ ] **Step 5: Review and checkpoint.** Request a fresh immutable-patch review covering all six tasks, ownership boundaries, no-fallback selection, diagnostics and acceptance evidence. Resolve findings with RED/GREEN regressions and rerun affected native gates before an explicit-path local commit: `test: verify maintained macOS c-ares provider reproducibility`. Recheck inherited dirty patch, profile hashes and sealed predecessor artifacts; documentation must state which gates actually passed and which remain pending. No push, app build, release-number allocation, signing or production feed action.

## Plan Self-Review And Handoff

Input/tool/source requirements map to Task 1; ownership/cancellation to Task 2;
archive/native-warning/provenance rules to Task 3; tree/manifest/relocation to Task 4;
explicit selection/default compatibility to Task 5; repeat/native/offline readback
and fresh review to Task 6. All five Review Focus cases have named test owners.
Package-root changes, SDK snapshots and native Intel/minimum-OS execution are not
silently promoted from this plan's dependency evidence.

After plan review, use the preserved Native execution method. Before implementation,
apply the worktree skill's isolation checks and carry forward the user's dirty-state
preservation boundaries; never reset or add unrelated paths. Complete Tasks 1-4
without selecting their provider for the app, then review opt-in selection and
complete Tasks 5-6. A changed pin, non-relocatable export requiring source changes,
or new production boundary is a stop-and-review condition, not an implicit waiver.

Remaining after this plan: other dependency/source/license and bundle/package
closure, whole-app reproducibility, unresolved runtime/platform/translation QA,
final branch review, reviewed app builds, numeric build allocation, signing and
notarization, signed-v3 updater acceptance, guarded closed-app installation and
GitHub/wiki/pre-release publication. Production signed feeds remain last.
