# Hungarian connection and TLS translations

## Scope

Completed 24 unfinished Hungarian connection entries, covering DHT bootstrap
URLs, IPv6, country databases, public hub lists and TLS choices. Existing
finished entries and source/context/location content are unchanged.

Published binaries, updater feeds, installed app and user profile remain
untouched. These source translations require a future application build.

## Verification

- Initial focused run: 30 expected failures (18 connection entries and six TLS
  entries checked in both source and compiled output).
- Focused connection and TLS compilation/readback tests pass. TLS assertions
  distinguish disable, allow and require, plus untrusted hub/client certificates.
- Scope checks confirm exactly 24 formerly unfinished entries changed.
- Disposable negative controls reject unfinished status, a damaged file glob
  and swapped TLS policy wording. Each produced only the expected assertion
  failures, without errors or skips.
- Full translation suite passed: 53 tests in 49.272 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/hungarian-connection-20261016/`.

## Remaining

Hungarian unfinished entries decrease from 165 to 141; the overall catalog
backlog decreases from 8,143 to 8,119. These counts describe catalog content,
not fresh full-source extraction coverage or native-speaker certification.

Other translations, native-speaker/layout review, minimum-OS and physical Intel
testing, wider platform QA, dependency reproducibility, optional Developer
ID/notarization and a future app build remain separate tasks.
