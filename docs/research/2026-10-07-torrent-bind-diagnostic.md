# Torrent Bind Failure Investigation

## Scope And Status

Diagnostic-only follow-up to the unaccepted nonblocking Torrent quit Task5
checkpoint. No production code, test assertions, listener policy or live state
was changed. Evidence is under
`Work/qa/torrent-bind-investigation-20261007/`.

- Controlled TCP and UDP collision mechanism: confirmed.
- Original full ARM Qt failure: not reproduced, not waived.
- Overall Task5: not accepted.
- Installed app, profile, production networking, signing, installation,
  GitHub publication and signed feeds: untouched.

## Original Failure

The sealed `full-qt-arm64.xml` records one failure in
`Runtime unchanged settings proxy and country path do not reconfigure or republish`,
at `test_torrentruntime.cpp:502`. The failed branch was `Created source seed`,
with a direct route and one error signal. Publication had already succeeded;
the settings-reload invariance checks had not started. The error was
`Torrent could not bind a configured listening endpoint.`

The original runtime discarded the native alert's endpoint, operation, transport,
error category and code. Consequently that receipt cannot distinguish an
occupied port from another bind/setup error. It is not evidence of a SOCKS5
connection failure or a DC sharing hash failure.

## Verified Source Trace

`RuntimeFixture` enables `randomizePort`, and its comment claims that runtime
uses port zero. Actual `TorrentEngine::Impl` instead chooses an integer in
49152..65535 with `QRandomGenerator::global()`, then passes it as the session's
listen port. This generator is independent of the Catch seed. Selection does
not reserve or check that endpoint. Engine settings explicitly disable both
bind retries and system-port fallback. The approved original behavior keeps
the chosen port stable; this investigation did not change that contract.

Both created-source seeds and downloaded jobs call the same `createSession()`.
The actual SDK's `session_impl::setup_listener()` binds TCP, then UDP on the same
port. A UDP-only collision can therefore cause a listener error even when
the fixture disables DHT and uTP. A TCP-only collision can fail earlier.

Provider identity was checked against its existing receipt and the actual dyld
readback. The ARM provider is `arm64-complete/lib/libtorrent-rasterbar.2.1.1.dylib`,
SHA-256 `443623937eb68b296e87c43334ca8d1f9244bac66b4b21b28f4bd0fb1c8f130c`.
Its source snapshot uses patch `43c451f74613ba1959150c99d402b75752cda029b6882556c1ac1f580802012e`;
`src/session_impl.cpp` matches the provenance receipt at SHA-256
`c722258ac2ea1568de31728017a6b346f068f4e27a7a6f49f099913f808205d1`.

## Experiments

The contract was written before execution. All runs use the existing disposable
HOME, offscreen Qt, loopback-only Seatbelt runner with verified external-network
denial. The pinned historical telemetry consumer and unchanged historical test
objects were used, rather than substituting the current production candidate.

| Experiment | Case Instances | Assertions | Result |
| --- | ---: | ---: | --- |
| Full historical ordinary Qt suite, seed 594128903 | 755 | 34,361 | Pass; no native bind-failure alert |
| Reserved TCP endpoint released before session creation | 1 | 155 | Pass; TCP and UDP confirmed on chosen port |
| Reserved TCP endpoint held | 1 | 27 | Exact expected single failure |
| Reserved UDP endpoint released before session creation | 1 | 158 | Pass; TCP and UDP confirmed on chosen port |
| Reserved UDP endpoint held | 1 | 27 | Exact expected single failure |

The replay retains the complete original case/section-path inventory. Controls
substitute only the random-port expression in private engine copies. They leave
the original test, assertions, timeout and selected source-seed branch intact.
Each held reservation belongs to its diagnostic process and is released at exit.

Both held controls reproduce exactly the original `lastError().isEmpty()`
assertion, at line 502, with source seed enabled, no local proxy and one error
signal. SDK telemetry identifies `sock_bind` (operation 13), `system` error 48
(`EADDRINUSE`), TCP type 0 or UDP type 3 respectively. No unrelated assertion,
setup failure, fatal exit, error or skip is accepted by the checker.

## Conclusion And Next Gate

An independently chosen occupied port is a demonstrated mechanism for this
exact symptom and fixture branch. It remains a hypothesis for the historical
failure because that receipt contains no native code. The full replay and
released controls cannot supply that missing fact retroactively.

The next focused engineering step is a reviewed native bind-diagnostic design
that retains operation, transport and error category/code safely, without
changing random/fixed-port policy, retry behavior, routing or fallback. Any
listener-policy or fixture-ownership repair needs its own explicit contract and
regression proof. Do not close Task5 merely by repeating green runs, correcting
the misleading comment, relaxing assertions or enabling fallback.

Broader lifecycle/timing causes, independent whole-candidate review,
localization/provenance, real-peer/selected-check/bootstrap and native
minimum-OS/live/soak acceptance remain pending. Final reviewed material rebuild,
numeric production build allocation, signing/notarization, signed-v3 acceptance,
guarded closed-app install/readback and authenticated publication remain gated.
Signed feeds stay last.
