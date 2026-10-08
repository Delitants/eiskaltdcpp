# Intel Qt Framework Metadata Linkage

The ten previously unmatched Intel Qt framework `Info.plist` files now have a
byte-exact released-template reconstruction. No framework, app resource or
original unresolved evidence was edited. This closes the missing generator-
linkage witness only, not historical build provenance or release acceptance.

## Source-Linked Reconstruction

The official CMake 3.28.6 source archive matches retained release checksum
`c39c733900affc4eb0e9688b4d1a45435a732105d9bf9cc1e75dd2b9b81a36bb`.
Its unmodified `Modules/MacOSXFrameworkInfo.plist.in` is identical to the template
in the previously pinned official macOS binary distribution. The source archive's
original `Copyright.txt` is retained with the template. These are released source
bytes, not a hand-edited copy of the mismatching newer template.
Primary inputs are from [Kitware's CMake 3.28.6 release](https://github.com/Kitware/CMake/releases/tag/v3.28.6).

Qt version `6.11.1` is verified in the pinned upstream Qt Base, SVG and Multimedia
source archives. Framework identifier/version property definitions are verified
in Qt Base's original `QtModuleHelpers.cmake`. Module names come from the retained
deployment ownership records, not extracted expected plist values. Minimal
`configure_file` recipes are checked against those inputs and exact output paths.

Both the actual pinned CMake 3.28.6 binary and current CMake 4.4.2 independently
generate all ten matching outputs using that same unmodified source template:

| Qt Frameworks | Outputs Per Generator |
| --- | ---: |
| Concurrent, Core, DBus, Gui, Multimedia, Network, Sql, Svg, Widgets, Xml | 10 |

All twenty generated files match the original Intel providers and historical
candidate files byte-for-byte. There is no post-generation key deletion,
whitespace normalization or dictionary-equivalence acceptance.

The older released template omits `CFBundleName`; the newer installed template
includes it. This explains the previous reconstruction mismatch. It does **not**
prove that CMake 3.28.6 was the historical Qt producer: another matching template
version or custom generator could have produced the same bytes.

## Gate And Controls

The original 33-file linkage check initially fails on exactly the ten missing
Intel Qt records. A derived mapping now extends those ten records and retains
the previous 23 byte-exact receipts, copied with explicit inherited provenance.
The checker itself is copied byte-for-byte unchanged. All 33 original unresolved
files satisfy that linkage check; original `source_proof` labels remain unchanged.
The old failed mapping, mismatching generated outputs and receipts remain intact.

Four input-binding controls first expose acceptance of missing recipes, missing
Qt property inputs, a self-consistent unrelated template and a substituted
generator binary. A separate recipe-value control first exposes a forged version
recipe with an updated hash. These exact negative receipts precede the repairs.
The final 17-control suite checks complete coverage, duplicates, stale/escaped
outputs, expected hashes, complete input roles, original notice/source-member
bindings, native binary/version identity, recipe values and rejection of invented
historical-producer claims. A bounded independent review identified a same-module
cross-generator output substitution gap; its exact rejection RED precedes the
version-specific output binding repair. All controls pass without errors or skips.
Inherited predecessor counts are explicitly labelled historical, separately from
the derived mapping's 10 new / 23 inherited / 33 total / zero missing result.
Mutation controls and gate readbacks run serially. This is not a second independent
review of the correction or final whole-branch review.

Private evidence is retained in `Work/qa/intel-qt-plist-template-20261008/`.
This is a source/template reconstruction, not a new Qt build, whole-framework
reproducibility claim or final whole-branch review. Matching generator linkage
does not certify artistic attribution, licensing or the original compiler,
configuration, patches and build recipe. Final materials still need the original
applicable notices and reviewed-source/package readback.

## Shutdown Hold And Remaining Work

The original Intel large-scope shutdown receipt still records only a failed
`engine.shutdownChecked()` assertion. Later passing probes do not establish its
refused boundary or cause. No repeated passing fixture, guessed runtime repair,
deadline extension or retrospective waiver is introduced here. The 9.169-second
whole-case duration is not a selected-check drain measurement.

External origins for 423 legacy images, other dependency/resource/source/notice
closure and binary reproducibility remain open. Original Intel shutdown and
broader Torrent/GUI/updater lifecycle, native minimum-OS, live/soak and translation
QA remain open. Independent final whole review, complete application/material
builds, production numbering/signing/notarization, signed update acceptance,
guarded installation and GitHub/wiki/screenshots/prerelease publication/readback
remain separate gates. Nothing installed or published; production signed feeds
remain last.
