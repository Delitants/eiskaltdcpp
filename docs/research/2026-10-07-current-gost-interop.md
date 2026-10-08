# Current-Candidate GOST Interoperability

## Scope And Outcome

The approved next verification checkpoint passes existing real-GOST fixtures on
both current cold-built Qt consumers. Candidate is `a6c88ff3c` plus the preserved
seven-file local retirement repair. No production or test-body changes, relink,
new build, installed-app/profile/server/VM mutation, signing or publication.

Private evidence: `Work/qa/gost-current-interop-20261007/`. Disposable credentials,
certificates, generated payloads and service logs remain private; they are not
release or documentation assets.

| Consumer | Public Cases | Public Assertions | Child Receipts | Child Assertions | Stopped Fixtures |
| --- | ---: | ---: | ---: | ---: | ---: |
| ARM/macOS27 | 8 | 448 | 13 | 1,824 | 9 |
| Intel/Rosetta on macOS27 | 8 | 448 | 13 | 1,752 | 9 |

Every public and child receipt passes, with zero failures/errors/skips. Each
parent JUnit report contains exactly the eight named public cases, including the
global core case selected by `[interop]`. Child compact receipts are counted
separately, not added to parent JUnit totals. Assertions vary with observed child
paths; all four transfer modes and both negative-auth/trust branches are retained.

## Actual Transport Evidence

The unmodified fixtures exercise authenticated GOST TLS TCP and UDP tunnels,
plain authenticated SOCKS5 on the same listener, IPv4/IPv6 destinations, remote
hostname resolution, UDP source isolation and anonymous/bad-auth rejection.
Generated two-way Torrent payloads transfer over TCP-only/uTP-only IPv4/IPv6
peers. Native byte comparisons and counters verify download and upload, tracker
announces, matching DHT replies and remote tracker/DHT/restart DNS names.
Completed-job resume and a fresh connection after a GOST restart also pass.
Invalid authentication and CA trust yield no payload, tracker/DHT/DNS traffic
or direct fallback. Global DC TCP/search/DHT traffic and route revocation pass.

The coordinator deliberately runs outside outer Seatbelt: macOS rejects nested
sandbox installation. Existing GOST supervisor/peer policies retain loopback-only
access; client children retain exact-port allowlists and blocked mDNS access.
Native direct TCP/UDP attempts to IPv4/IPv6 destinations must return `EPERM`.
No containment assertion is removed. The existing test-only pinned-port forwarder
cannot dial destinations; the separate actual-adapter case has no forwarder.

Each run uses seed `594128903`, a fresh mode-0700 HOME/XDG/TMP root, offscreen Qt,
matching CPU provider/plugins and no interposer. Runs are serial with no concurrent
compilation or heavy hashing. Parent and child dyld logs confirm the corresponding
libtorrent provider. The server executable is the previously checksum-verified
GOST 3.3.0 ARM binary for both client CPUs; an Intel GOST server is not tested.
All 18 supervisor logs are empty and all control sockets are removed at teardown.

## Input And Preservation Readback

Before/after checks verify 1,058 compiled-source/fixture/binary/helper pins,
candidate HEAD and the complete dirty patch. Both binaries match the preceding
AntiSpam final cold-build metadata: ARM target26 and Intel target14. Runtime is
ARM/macOS27 and Intel/Rosetta, not native minimum-OS compatibility.
The original seven-file repair remains unchanged and outside this docs checkpoint.

Both historical whole app trees and strict signatures, five archive/notice pins,
58 provider resources and the 9/17/8/3 protected/audit/negative manifests verify
before and after. New QA and completed temporary fixture trees are sealed with
an exact 605-file inventory and a full hash readback; prior evidence is untouched.

Results SHA-256:
`9d6a9ad26d6e3b10efcff9ee863be1a4e54c984f9364d43319182ff499f497ad`.
Evidence seal SHA-256:
`0b60bbb7724ae9b9cd4a5fe552c007635f55685b2cba4b6c778756068eb29e05`.

## Remaining Gates

Latest-candidate loopback GOST interoperability is closed. This is not external
live-server acceptance, partial-piece crash recovery, selected-file checking
during live peer traffic, full application/native Cocoa/minimum-OS acceptance,
benchmark/soak, complete dependency provenance or reproducibility proof.
Historical bind/freeze/latency/recovery causes remain unwaived by these passes.
Overall Torrent Task5, GUI-settings Task3 and updater Tasks7/8 remain open.

Broader lifecycle, localization/native quality, final whole updater/prerelease
review, reviewed application/material builds, numeric production build allocation,
Developer ID signing/notarization, signed-v3 updater acceptance, guarded closed-app
installation/readback and authenticated GitHub/wiki/screenshots/prerelease
publication/readback remain pending. Production signed feeds remain last.
