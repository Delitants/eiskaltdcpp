# Current Boost/OpenSSL Input Provenance

## Outcome: OpenSSL Header Gate Held

The [bounded input audit](../superpowers/plans/2026-10-07-current-boost-openssl.md)
finds a current both-CPU header/library mismatch. Fresh test-build objects and
the current ABI-2 libtorrent provider consume OpenSSL headers under
`/usr/local/include/openssl`, resolving to an installed 3.4.1 tree, although
their selected library receipts and actual Mach-O imports select 3.6.3.
Matching versions in CMake's library selection do not establish matching
compiler headers. This is not a proven cause of the historical crash,
GUI freeze, shutdown refusal or proxy errors.

Per CPU, retained built-object dependency records identify:

| Scope | Affected Objects | Wrong-Origin Headers |
| --- | ---: | ---: |
| Fresh core/Qt/Torrent test build | 360 | 78 |
| Current libtorrent provider build | 96 | 77 |

The additional fresh-build header is `md5.h`. Not every object or OpenSSL
header selection is affected; the audit retains distinct origins rather than
collapsing same-named headers into a single value. Five fresh consumer
binaries and their link records are read back per CPU. The provider dylibs,
raw link receipts and actual imports also match the retained selected
OpenSSL libraries. Intel installed libraries match retained source-build
library bytes; ARM uses an installed Homebrew bottle, not an independent
source rebuild.

## Search-Order Evidence

Affected retained compiler flags put the selected OpenSSL include roots in
`-isystem` directories. Native preprocessing-only search readbacks for both
CPU configurations put `/usr/local/include` before those selected roots.
The dependency records show the old headers actually consumed, not merely
available on disk. CMake caches selecting 3.6.3 do not override that search
order. No application, helper, installer or test executable was launched.

The previous c-ares/MaxMind header repair remains valid for its specific
providers. It does not isolate OpenSSL across the build closure. The previous
libtorrent material refresh matched its modified source and recorded external
inputs explicitly; it did not accept their external OpenSSL provenance. Those
sealed results and original failures are preserved, not rewritten.

## Audit Controls And Limits

All 2,023 selected Boost header files on ARM and 2,022 on Intel match the pinned
1.92.0 upstream archive. All 76 retained Boost notices match original archive
members. The five retained OpenSSL notices are read back: four match upstream
members directly, and the installed license matches the upstream license bytes.
Four retained package metadata records also match their pins. These are subset
and original-notice readbacks, not complete final-release material acceptance.

Each CPU has 147 distinct resolved OpenSSL header inputs across multiple
origins: 55 are exact 3.6.3 upstream matches, 61 differ from direct upstream
members, and 31 are generated-header inputs with corresponding templates.
The latter count includes separate origins of the same header name. All 78
old-origin fresh-build headers differ from their selected 3.6.3 counterparts;
the provider consumes 77 of those. The mismatch gate remains HELD.

Eight archive controls pass after four exact RED failures against the retained
older subset comparator: empty selections, unselected absolute/traversing
members and a second archive root previously passed. The new private comparator
also rejects duplicate, missing and changed selected members, streams pinned
sources without extraction, and refuses optimized Python. This changes the
new audit only; old scripts and checkpoints remain untouched.

Generated OpenSSL headers are classified with source templates rather than
claimed as exact upstream byte matches. Retained Intel configuration/build
records and ARM bottle recipe/receipt metadata are not independent header
regeneration or binary reproducibility. The original historical Intel combined
build/test script exited 2; a separate historical test retry exited 0 and
installation records exist. Neither is a new test run, nor does the retry
erase the original failure.

This is current-byte/post-build dependency evidence, not immutable historical
compiler-input proof. No complete application build, source-material release,
signing, installation, network request or publication occurred.

## Next And Remaining

Next: isolate selected OpenSSL public/configuration headers across the core,
Qt/Torrent consumers and libtorrent/libdatachannel provider targets. Prove
shadow-header refusal with fixtures, rebuild in new private both-CPU roots,
read back actual dependencies/versions/imports, and refresh matching materials.
Do not change `/usr/local`, weaken the source gate, or alter existing sealed
builds to make them appear accepted.

Generated-header regeneration, other dependency/resource/material closure,
whole-binary reproducibility and legacy attribution remain separate. The
original Intel shutdown refusal, overall Torrent5/GUI-settings3/updater7/8,
broader lifecycle/native/minimum-OS/live/soak/platform/translation acceptance,
final whole review/application/material rebuilds, numeric allocation,
signing/notarization/signed-v3/install/GitHub/wiki/prerelease publication and
public readback remain pending. Production signed feeds remain last.
