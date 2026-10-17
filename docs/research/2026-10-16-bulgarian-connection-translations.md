# Bulgarian connection and TLS translations

## Scope

Completed 24 unfinished Bulgarian entries for IPv6, DHT bootstrap URLs, public
hub lists, country databases and TLS policy. Previously finished translations,
source/context/location content and other locales are unchanged.

Added Bulgarian wrappers for existing connection and TLS compilation checks.
Published binaries, updater feeds, installed app and user profile remain
untouched. These translations require a future application build.

## Verification

- Initial focused tests: 30 expected failures, covering 18 connection entries
  and six TLS entries checked in both source and compiled output.
- Both focused tests pass after translation.
- Scope checks confirm exactly 24 formerly unfinished entries changed.
- Disposable negative controls reject unfinished status, a damaged file glob
  and swapped allow/require TLS wording, with only expected assertion failures
  and no errors or skips.
- Full translation suite passed: 61 tests in 53.187 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/bulgarian-connection-20261016/`.

## Remaining

Bulgarian unfinished entries decrease from 208 to 184. The overall catalog
backlog decreases from 7,770 to 7,746. These counts do not establish fresh
full-source extraction coverage or native-speaker certification.

Other translations, native-speaker/layout review, minimum-OS and physical Intel
testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and an updated application build remain separate tasks.
