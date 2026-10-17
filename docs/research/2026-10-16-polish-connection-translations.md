# Polish connection and TLS translations

## Scope

Completed 24 unfinished Polish entries for IPv6, DHT bootstrap URLs, public
hub lists, country databases and TLS choices. Other connection text, including
Type-of-Service help, remains for a later batch. Previously finished entries,
source/context/location content and other locales are unchanged.

Published binaries, updater feeds, installed app and user profile remain
untouched. These translations require a future application build.

## Verification

- Initial focused tests: 30 expected failures, covering 18 connection entries
  and six TLS entries checked in source and compiled output.
- Both focused tests pass after translation.
- Scope checks confirm exactly 24 formerly unfinished entries changed.
- Disposable controls reject unfinished status, damaged file-filter syntax
  and swapped allow/require TLS wording with only expected assertion failures
  and no errors or skips.
- Full translation suite passed: 65 tests in 60.461 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/polish-connection-20261016/`.

## Remaining

Polish unfinished entries decrease from 293 to 269; the overall catalog backlog
decreases from 7,562 to 7,538. These counts do not establish fresh full-source
extraction coverage or native-speaker certification.

Translation completion, native-speaker/layout review, minimum-OS and physical
Intel testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and an updated application build remain separate tasks.
