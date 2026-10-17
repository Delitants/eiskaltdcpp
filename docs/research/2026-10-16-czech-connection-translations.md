# Czech connection and TLS translations

## Scope

Completed 24 unfinished Czech entries for IPv6, DHT bootstrap URLs, public hub
lists, country databases and TLS policy. Previously finished translations,
source/context/location content and other locales remain unchanged.

Parameterized the Hungarian TLS compilation helper for reuse by Czech tests,
preserving its checks. Published binaries, update feeds, installed app and user
profile remain untouched; these changes require a future application build.

## Verification

- Initial focused tests produced 30 expected failures: 18 connection entries
  and six TLS entries checked in source and compiled output.
- Three focused tests pass, including the existing Hungarian TLS regression.
- Scope checks confirm exactly 24 formerly unfinished entries changed.
- Disposable negative controls reject unfinished status, a damaged file glob
  and swapped allow/require TLS wording, with only expected assertion failures
  and no errors or skips.
- Full translation suite passed: 57 tests in 47.927 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/czech-connection-20261016/`.

## Remaining

Czech unfinished entries decrease from 208 to 184. The overall backlog decreases
from 7,978 to 7,954. Counts describe catalog contents, not fresh full-source
extraction coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional Developer ID/notarization and an updated application build.
