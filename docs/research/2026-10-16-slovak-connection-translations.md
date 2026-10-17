# Slovak connection translations

## Scope

Completed 24 unfinished entries covering IPv6, DHT bootstrap URLs, public hub
list URLs, country database selection, TLS modes and certificate-trust exceptions.
Existing finished translations and non-translation XML remain unchanged.

Published binaries, updater feeds, installed app and profile are untouched.
These source changes require a future application build.

## Verification

- Initial two-test regression produced 30 expected failures: 18 connection
  entries plus six TLS choices checked in both source and compiled form.
- Focused Qt compilation/readback passed after translation.
- Full translation suite passed: 91 tests in 80.944 seconds, without skips.
- `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout validation remain pending.
- Existing helper assertions preserve required keys, technical tokens, IPv6
  example and database file-filter syntax; TLS choices use exact assertions.
- Independent scope checks confirm exactly 24 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry and altered-address controls each produced one
  expected failure. Changed TLS-policy and certificate-trust controls each
  produced two expected failures, covering source and compiled readback.
- No control errors or skips occurred.

Local evidence: `Work/qa/slovak-connection-20261016/`.

## Remaining

Slovak unfinished entries decrease from 403 to 379; the overall catalog backlog
decreases from 6,941 to 6,917. These counts do not certify full-source extraction
coverage or native-speaker accuracy.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
