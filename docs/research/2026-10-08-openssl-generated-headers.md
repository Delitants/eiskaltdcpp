# Independent OpenSSL Public-Header Regeneration

## Scoped Gate Passed

Following the [header-isolation repair](2026-10-08-openssl-header-isolation.md),
fresh, separate source directories independently regenerate the public OpenSSL
3.6.3 header namespace for each selected macOS CPU provider.

| CPU | Public Headers | Direct Source Headers | Independently Generated | Byte Mismatches |
| --- | ---: | ---: | ---: | ---: |
| arm64 | 142 | 114 | 28 | 0 |
| x86_64 | 142 | 114 | 28 | 0 |

The release archive is retained with SHA-256
`243a86649cf6f23eeb6a2ff2456e09e5d77dd9018a54d3d96b0c6bdd6ba6c7f1`.
All four checksum-verified recipe patches are applied to each fresh extraction.
Each resulting source tree has 5,857 pinned inputs before configuration. Those
inputs remain unchanged after generation. No installed headers, generated
headers or configuration modules are copied into the fresh source trees.

`Configure` creates `configuration.h`; `make -j2 build_generated` generates the
other mandatory public headers from the retained templates. All 28 generated
headers have a matching declared template in fresh `configdata.pm`; their
outputs did not exist in the captured pre-configuration input set. All 114
direct headers still match their captured source bytes.

The independently generated namespace matches both selected installed packages
and every staged-header digest and consumed-header digest from the previous
285-object provider and 950-object consumer readbacks per CPU. The selected SSL
and crypto library byte hashes remain unchanged. This binds the header result
to that previous build evidence; it is not a new application or library build.

## Configuration And Limits

ARM follows the installed Homebrew formula's `darwin64-arm64-cc` target,
`enable-ec_nistp_64_gcc_128`, `no-ssl3`, `no-ssl3-method`, `no-zlib` and library
layout. The installed provider is a bottle, not a local source rebuild. Its
historical bottle-build environment/configuration module is unavailable; fresh
header generation does not establish bottle or binary reproducibility.

Intel follows the retained `darwin64-x86_64-cc` source-build recipe. Fresh target
metadata, public API/feature/system/other defines, compiler selection and
architecture/minimum-version flags match the retained configuration. Disposable
prefix paths differ intentionally. Compiler-dependency detection also differs:
the fresh configuration marks `makedepend` unavailable instead of selecting the
retained `gcc` dependency scheme. This difference is retained in the readback;
it does not change the generated public headers and is not waived for any
future full-library build reproduction.

Both runs use clean process environments, disposable homes, temporary folders
and prefixes, with arm64/macOS 26.0 or x86_64/macOS 14.0 generation flags. They
use retained local inputs without downloads. No object/library compilation,
installation, provider replacement, app/profile/key/server/VM modification,
signing, feed activation or publication occurs. These are host-side generation
checks, not native minimum-OS runtime acceptance.

## Controls And Preservation

Ten private audit controls pass. Five initially failed precisely because the
first auditor did not reject changed templates, pre-existing generated output
inputs, jointly rewritten direct/installed bytes, symlinked generated outputs
or unclassified headers. Those RED results and the first auditor are retained.
The final auditor rejects all five and also rejects empty, missing, extra or
byte-mismatched header sets while classifying a valid independent fixture.

Earlier immutable QA artifacts and inherited runtime/test repairs are preserved.
Historical combined OpenSSL build/test failure, the original Intel shutdown
refusal and other diagnostic failures remain unwaived. This audit adds no
product runtime change and does not attribute a historical crash, freeze or
proxy error to OpenSSL.

## Remaining

This closes independent regeneration of the selected public OpenSSL headers.
Matching libtorrent release-material refresh, other dependency/resource/source/
notice closure, legacy attribution and whole-binary reproducibility remain.

The original Intel shutdown refusal, Torrent5/GUI-settings3/updater7/8,
native/minimum-OS/live/lifecycle/soak/platform/translation QA, final whole-branch
review/application/material builds and production numeric allocation remain.
Developer ID signing/notarization, signed-v3 update acceptance, guarded
closed-app installation, authenticated GitHub/wiki/screenshots/prerelease
publication and public readback remain pending. Production signed feeds are last.
