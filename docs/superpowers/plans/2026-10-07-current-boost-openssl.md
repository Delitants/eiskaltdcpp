# Current Boost And OpenSSL Input Audit

> **For agentic workers:** Use superpowers:executing-plans inline for this approved dependency-provenance follow-up.

**Goal:** Trace the Boost/OpenSSL headers, linked libraries and retained notices selected by the fresh both-CPU test builds, distinguishing exact upstream files from generated/configured inputs.

**Architecture:** Read existing compiler dependency and link receipts, hash current selected inputs, and stream the pinned source archives without extraction. Compare retained package notices with original archive members. Report generated OpenSSL headers separately, with actual Intel source/build records and ARM bottle metadata; do not infer reproducible binaries from version agreement.

**Tech Stack:** Python, unittest, tar streams, SHA-256, retained CMake records and Mach-O inspection.

**Spec:** `docs/superpowers/specs/2026-09-29-built-in-updates-design.md`, final matching source/notice gate.

## Constraints And Rulings
- Continue in the existing approved branch with a new private QA directory; preserve the seven dirty repairs and allocation fixture.
- No network, application/helper launch, compilation, installation, key/profile/server/VM changes, signing, push or feed publication. Read-only native preprocessing may inspect search order; it creates no object or executable.
- Keep old sealed QA immutable, including original failed evidence. Check permitted documentation appends against stored prefixes, never reseal old checkpoints.
- Ruling: this slice audits consumed header subsets and selected OpenSSL library provenance. Full source reconstruction, generated-header regeneration, complete dependency/resource closure and binary reproducibility remain distinct gates.
- Do not count unbuilt link recipes as actual consumers. Dependency receipts and current bytes are post-build evidence, not immutable compiler-input receipts.

## Task 1: Audit Current Selected Inputs
- [x] Freeze prior evidence and candidate input pins in `Work/qa/current-boost-openssl-20261007`.
- [x] Test real archive-subset validation: empty selections, unsafe paths, duplicate/missing/altered members and a valid control; retain exact RED then GREEN results.
- [x] Derive selected headers from existing built-object dependency records, including current libtorrent provider inputs; compare upstream headers and classify generated OpenSSL inputs explicitly.
- [x] Read back both CPUs' five fresh consumer binaries/link receipts and selected OpenSSL library identities; compare Intel source-build libraries and ARM installed-provider receipts.
- [x] Verify retained Boost/OpenSSL sources, notices and metadata with original-member provenance where available.
- [x] Independently review the bounded audit, rerun readbacks, preserve prior pins, record exact outcomes and locally commit only public documentation.

## Diagnostic Rulings

- Ruling: preserve strict first refusals, then produce an explicitly HELD diagnostic inventory rather than abort before recording the mismatch. Distinct origins must remain distinct; reporting them does not waive source acceptance.
- Ruling: fresh objects consume 78 old-origin OpenSSL headers, while provider objects consume 77; retain and correct the initial diagnostic's unjustified identical-count assumption, not the failed evidence.
- Ruling: preserve the historical combined OpenSSL build/test exit 2 and record separate retry/install history without assuming original success. Source-build library byte identity is not a passing historical test claim.

## Outcome

The header/source gate is HELD, not completed: both CPUs consume ambient
OpenSSL 3.4.1 headers against selected 3.6.3 libraries. Per CPU, 360 fresh-build
objects and 96 provider objects are affected; 78/77 wrong-origin headers are
retained respectively. The Boost header subset and original package notices
match pinned sources. Next is a focused OpenSSL isolation repair and new
both-CPU builds/materials. Nothing installed or published; signed feeds last.

## Review Focus
- Empty or omitted selections accidentally passing a source gate.
- Unsafe/duplicate archive members or mismatched source root accepted.
- Generated headers mislabeled as exact upstream files.
- Matching package version mistaken for library rebuild proof.
- Retained notice hashes mistaken for upstream origin proof.
