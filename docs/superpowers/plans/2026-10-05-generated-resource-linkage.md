# Generated Resource Linkage Plan

> **For agentic workers:** Use superpowers:executing-plans inline. This continues the approved release-readiness provenance audit, not product implementation or deployment.

**Goal:** Trace and reconstruct the 33 remaining generated framework metadata files in disposable QA, retaining historical-toolchain limitations.

**Architecture:** Validate pinned Qt/Sparkle source inputs first. Reconstruct Qt framework plists using the captured CMake template and source-defined properties, and Sparkle module/PkgInfo artifacts using source-linked Xcode settings in a minimal disposable generator fixture. Require exact output bytes, not just selected fields. Keep generator reconstruction separate from original-build provenance or whole-framework reproducibility.

**Tech Stack:** Python, CMake configure_file, Xcode generator, SHA-256/plist readers.

**Spec:** `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`; approved framework provenance follow-up.

## Constraints
- Existing branch and all retained candidates, archives, providers, installed app/profile, keys, VM and services remain unchanged.
- QA outputs stay under a new owned directory. No updater/application launch, signing, network, installation, push or public-feed activation.
- Only minimal fixture compilation is permitted, with at most two workers and serial invocations. Do not build Sparkle, Qt or Eiskalt to reconstruct small metadata.
- Do not copy observed metadata values into recipes and call that source provenance. Derive settings from pinned source/package versions and captured generator inputs.
- Exact reconstruction does not prove the historical producer used the current generator version. Keep source/compiler provenance, licensing and reproducibility gates explicit.

## Steps
- [x] Record exact baseline missing generator-linkage checks for 29 Qt and four Sparkle files.
- [x] Capture pinned source members and local generator/template/license/recipe hashes.
- [ ] Generate the Qt files using source-defined framework properties; compare every output byte. Nineteen ARM files match; ten Intel files remain mismatched.
- [x] Generate Sparkle public-module and helper-package metadata in a disposable minimal fixture or explicitly retain any unsupported step.
- [x] Reject changed generator output/settings in disposable controls.
- [x] Recheck all retained candidate/provider/protected state and record outcomes/limits in public documentation.

## Observed Checkpoint

The original regression observes exactly 33 missing links: 29 Qt plists and four
Sparkle files. Current CMake 4.4.2, its captured default template, pinned Qt module
properties and six pinned Qt source version declarations reconstruct all 19 ARM
plists exactly. The ten Intel plists omit the empty `CFBundleName` emitted by the
current template. Their generated outputs and original mismatch are retained,
without trimming that key or promoting dictionary agreement to byte agreement.
Capturing an independently sourced matching template is the next narrow gate;
even a matching older template would not establish which generator produced the
historical Qt SDK.

The initial Xcode configure failed under sandbox cache restrictions and logged
a CoreDevice plug-in warning. A fresh explicit-compiler configure with approved
developer-cache access succeeded, followed by a two-worker unsigned fixture build.
Xcode's module-map and PkgInfo generation reproduce all four Sparkle files exactly.
The fixture uses disclosed dummy code/header content, never launches, and does
not rebuild Sparkle or establish native Intel acceptance. No explicit CodeSign
step appears in its build log. The failed configure and first mismatch are kept.

The unchanged full gate now fails exactly ten Intel cases. Eleven separate
evidence/control tests pass without skips, including changed bytes, altered input
hash, missing historical-limit and false Intel-promotion rejection. The original
210-file mapping test also passes. Source-proof labels remain unchanged; the 23
reconstructions are a separate tier, not historical compiler/build provenance or
whole-framework reproducibility.

Both whole retained candidate trees and strict signature readback, five previous
archive/notice pins, 58 provider resources, nine protected live-file hashes and
28 retained earlier evidence hashes pass preservation checks. The first
preservation harness overcounted the prior manifest (18 expected, 17 actually
listed); every listed hash passed. Its original failure is retained, and the
corrected harness does not change the manifest or any input. No production
code, provider, profile, installed app, key, feed, server, VM or release asset is
changed. Normal Xcode developer-cache activity is not a host-wide preservation
claim. Task 7/8 and the original release acceptance gates remain open.

## References
The generator behavior is documented by [CMake framework plist properties](https://cmake.org/cmake/help/latest/prop_tgt/MACOSX_FRAMEWORK_INFO_PLIST.html) and [Apple build settings](https://developer.apple.com/documentation/xcode/build-settings-reference). Those references explain the mechanism, not the provenance of these particular historical binaries.
