# Native Torrent Listener Diagnostics

## Scope And Status

The approved bounded diagnostic change is implemented and passes its focused
ARM and Intel/Rosetta checks. Complete-suite acceptance is **not passed**:
the ordinary Intel Qt run captures one natural UDP bind collision. Overall
Torrent quit Task5, GUI-settings Task3 and updater Tasks7/8 remain open.

Evidence: `Work/qa/torrent-bind-diagnostics-20261007/`. The original all-green
checker and aggregate full-suite runner both retain exit 1. A separate audit
validates the exact failed receipt; it does not convert that failure to a pass.
No installation, publication, signing, key, server, profile or feed change occurs.

## Implementation

`TorrentEngine` preserves the existing bind/accept error prefix and appends:

```text
[op=sock_bind transport=UDP local=127.0.0.1:61112 error=system:48]
```

Fields come from the typed native listener alert. TCP, UDP and their TLS variants
are distinguished; unknown transports retain their numeric type. IPv6 is
bracketed with a numeric scope ID, never an interface name. One QString
substitution pass preserves scopes such as `%3`.

Error-category names are limited to known SDK-native names. Arbitrary categories
become `unknown`, retaining only their numeric error code. The formatter does
not use error messages, raw alert text, configuration, credentials or paths.
`TorrentRuntime::lastError()` and its error signal preserve the message unchanged;
the existing MainWindow log route remains unchanged.

Listener history, selected ports, retries, routing and no-direct-fallback policy
are unchanged. Recoverable accept failures still preserve the confirmed listener
and its working loopback connection. Public engine headers and signals do not
change. Earlier uncommitted retirement repairs remain separate from this patch.

## Verification

The predeclared RED contract requires exactly 28 backend CHECK failures and
8 runtime CHECK failures per CPU, exclusively for missing diagnostic fields.
Both CPUs match: 72 intended failures across six case instances, no errors or
skips. These use the old engine before the product edit. Initial private-build
setup failures (missing helper include path and an unnecessary Boost dependency
in the Qt test) are retained separately and are not counted as behavioral RED.

Focused GREEN includes real TCP-only/UDP-only occupied endpoints, scoped IPv6,
TLS and unknown transport formatting, private category/message exclusion,
recoverable accept behavior, runtime propagation and the original unchanged-
settings/hash-reuse case. Each CPU passes backend 4 instances / 92 assertions
and Qt 3 instances / 497 assertions: 14 instances / 1,178 assertions in total.

| Complete Ordinary Suite | Instances | Assertions | Failures |
| --- | ---: | ---: | ---: |
| ARM Torrent backend | 244 | 18,605 | 0 |
| ARM Qt | 756 | 34,242 | 0 |
| Intel/Rosetta Torrent backend | 244 | 18,605 | 0 |
| Intel/Rosetta Qt | 756 | 34,202 | 1 |
| Total | 2,000 | 105,654 | 1 |

No errors, skips or fatal exits occur. Inventories match between CPUs and retain
all preceding cases; only the three diagnostic cases are added. Fewer Intel Qt
assertions reflect the failed REQUIRE ending that branch early, not test removal.

Runs are serial, with disposable HOME, offscreen Qt, explicit dependency-provider
readback and loopback-only Seatbelt restrictions. External-network denial is
verified for every run. The changed test and public engine translation units
are freshly compiled into private incremental consumers, reusing unchanged
objects from the preceding whole-candidate build. All 297 unique regular-file
link inputs match their recorded SHA-256 pins. Current source matches frozen
GREEN inputs. The 73-file prior investigation and 519-file failed checkpoint
seals still verify.

These consumers run on ARM/macOS 27 and Intel under Rosetta. ARM minimum target
is 26, Intel minimum target 14, SDK 27. This is not native Intel/minimum-OS proof,
an independent whole-candidate review, a final app rebuild or installed release.
Hidden/real interoperability suites are not rerun for this bounded change.

## Natural Failure And Next Gate

The Intel Qt receipt fails `observedTraffic` in
`Shared transfers show real local downloads and uploads while Torrent tab is hidden`,
`test_unifiedtransfers_swarm.cpp:237`, non-hybrid branch. The seed records native
`sock_bind`, UDP, `127.0.0.1:61112`, `system:48` (`EADDRINUSE`); downloader payload
bytes, rate and peer count remain zero. This is a bind failure, not a SOCKS5
authentication or hashing error. The original all-green checker correctly fails.

That unchanged fixture obtains seed/download ports with temporary TCP listeners,
then closes them before session creation. It does not reserve or check UDP, even
though the SDK binds TCP and UDP on its configured endpoint with DHT/uTP disabled.
This establishes a fixture selection gap and the native error for this run.
The conflicting socket's owner was not captured; do not infer an external app,
leaked earlier test or specific session. Do not waive the failure by a green rerun.

Next is a focused fixture endpoint-ownership investigation and controlled
dual-transport regression design, preserving assertions, deadlines, listener
policy and production networking. The earlier historical ARM failure still lacks
native fields and remains unexplained/unwaived; today's receipt cannot supply its
missing facts retroactively.

Broader independent review, owner/lifecycle and historical 102/180/354ms causes,
localization/native quality, provenance/reproducibility, real-peer/selected-check/
bootstrap, native minimum-OS/live/benchmark/soak acceptance remain pending. Final
reviewed app/material rebuild, numeric production build allocation, signing/
notarization, signed-v3 acceptance, guarded closed-app install/readback and
authenticated GitHub/wiki/screenshots/prerelease publication/readback remain gated.
Signed feeds stay last.
