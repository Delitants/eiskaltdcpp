# OpenSSL Header Isolation Repair

## Scoped Build Gate Passed

The [previous audit](2026-10-07-current-boost-openssl.md) identified selected
OpenSSL 3.6.3 libraries compiled with ambient 3.4.1 headers. The macOS build now
stages the selected public header namespace in a distinct ordinary include
directory, before imported SYSTEM roots and Clang's ambient search paths.
It does not replace installed providers or change runtime proxy/quit behavior.

Fresh readback for each CPU identifies:

| Scope | Built Objects | Consumed OpenSSL Header Paths | Wrong Origins |
| --- | ---: | ---: | ---: |
| ABI-2 libtorrent provider | 285 | 77 | 0 |
| Five core/Qt/Torrent consumers | 950 | 79 | 0 |

Both scopes stage all 142 public headers, including configuration companions,
with canonical source paths and byte hashes. Every consumed OpenSSL header
matches that selected package. Actual Mach-O imports and raw provider link
receipts select the retained 3.6.3 libraries. Both CPU version probes report
matching header/library numbers `30600030` and Boost `109200`.

The five built consumers are core tests, Qt tests, Torrent tests, main-window
toggle tests and settings-quit tests. Architecture and minimum-version metadata
are arm64/macOS 26.0 and x86_64/macOS 14.0. Execution is on the current host,
including Rosetta, not native minimum-OS acceptance. The complete application
binary was not built or installed in this repair.

## Maintained Build Rule

`cmake/OpenSSLProviderHeaders.cmake` validates the selected namespace and its
required/quoted/angle-bracket companions, stages byte-identical headers at
configure and build time, declares byproducts and attaches explicit dependency
edges to application and provider compilation targets. Removed source headers
cannot silently survive in staging. Individual escaping header symlinks fail.

Package-manager namespace aliases are accepted only when both selected SSL
and crypto libraries resolve to the package exporting that exact canonical
namespace. Arbitrary aliases, missing library pairing and wrong SSL/crypto
packages remain refused. Unrelated include namespaces retain their precedence.

The maintained libtorrent recipe injects the rule through
`CMAKE_PROJECT_libtorrent_INCLUDE`, without modifying pinned upstream sources
or changing the selected-recheck patch/ABI. Its explicit macOS `--openssl-root`
selection pins both library cache entries, including when switching providers.
`--boost-root` names the include directory containing `boost/`. Existing default
non-Apple behavior is unchanged; Windows/Linux acceptance is still pending.

The generated refresh script initializes policies explicitly. CMake 3.x defaults
an unset [CMP0057](https://cmake.org/cmake/help/v3.28/policy/CMP0057.html) to OLD,
which does not interpret `IN_LIST`. The missing-policy failure was reproduced
with checksum-verified official CMake 3.28.6 before repair.

## Controls And Preservation

- Initial behavioral fixtures produced exactly 11 RED failures before the rule
  existed. Review controls reproduced namespace escape and cached-library
  switching; the older-CMake policy failure and legitimate-package alias
  rejection were reproduced and retained separately.
- Final 21 OpenSSL controls pass with CMake 3.28.6 and 4.4.2. They cover poisoned
  imported roots, child/wrapper targets, missing companions, Make/Ninja edits,
  multi-config builds, removed byproducts, namespace/header symlinks, both
  library pairing checks and the actual maintained provider recipe/hook.
- All 19 existing c-ares/MaxMind controls pass. Both registered header suites
  pass in the disposable, externally network-denied environment.
- Ten SSL/certificate cases pass per CPU, with 88 assertions each. Loader
  readback selects the intended OpenSSL libraries. This excludes the unresolved
  shutdown diagnostic and is not full GUI/Torrent/updater acceptance.
- Each provider retains 697 unchanged source dependency inputs and 295 installed
  headers matching source bytes. All 106 static-archive member payloads match
  their actual linked objects. Duplicate filenames remain distinct members;
  earlier inventories were sorted, not physical archive-order receipts.
- Nine private readback controls cover header-origin/byte refusal and archive
  ordering, duplicate payloads, architecture and truncation. Failed audit
  assumptions are preserved rather than rewritten into successful attempts.

The first ARM consumer retry refused Homebrew's legitimate include alias; it
remains a failed attempt. Plain CTest discovery loaded stock, unpatched
libtorrent and aborted before the requested suites; isolated discovery selects
the newly built provider and passes. An outer-sandbox version-probe refusal is
also retained separately from the successful network-denied run.

All 8,381 earlier libtorrent-material checkpoint artifacts, 50 mismatch-audit
artifacts and 11,614 earlier provider artifacts are preserved. Existing dirty
runtime/test repairs remain unchanged and outside this commit. No historical
crash, freeze, proxy failure or Intel shutdown refusal is reclassified as fixed.

## Remaining

This closes the scoped current-header/library mismatch, not complete source
material, reproducibility or release acceptance. Evidence is post-build byte
and dependency readback, not immutable historical compiler-input proof.
Generated-header regeneration, matching release-material refresh, other
dependency/resource/source/notice closure and legacy attribution remain open.

The original Intel shutdown refusal, Torrent5/GUI-settings3/updater7/8,
native/minimum-OS/live/lifecycle/soak/platform/translation acceptance, final whole
review/application/material rebuilds and production numeric allocation remain
pending. Developer ID signing/notarization, signed-v3 update acceptance,
guarded closed-app installation, GitHub/wiki/screenshots/prerelease publication
and public readback remain pending. Production signed feeds remain last.
