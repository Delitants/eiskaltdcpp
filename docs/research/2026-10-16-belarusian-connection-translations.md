# Belarusian connection translations

## Scope

Completed 18 unfinished Belarusian entries in DHTBootstrapList,
SettingsConnection and UISettingsConnection. The entries cover DHT bootstrap
URLs, IPv6 addresses, country databases and public hub list configuration.
Local bind addresses remain distinct from external addresses. Other translations
and source/context/location content are unchanged.

Parameterized the existing Ukrainian connection compilation helper and added
a Belarusian wrapper without changing its checks. Published binaries, update
feeds, installed app and user profile remain unchanged.

## Verification

- The new test initially failed on all 18 unfinished entries.
- Both Belarusian and Ukrainian focused tests pass after translation.
- Qt compilation/readback checks finished, nonempty translations and exact
  compiled values, along with IPv6/WAN/DHT/URL/database identifiers and globs.
- Scope checks confirm exactly 18 formerly unfinished entries changed.
- Disposable unfinished-entry and damaged-file-filter controls each produced
  one expected failure, without errors or skips.
- Full translation suite: 49 tests passed in 46.851 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/belarusian-connection-20261016/`.

## Remaining

Belarusian has 109 unfinished entries remaining, down from 127. The overall
catalog backlog is 8,252, down from 8,270. These counts do not establish fresh
full-source extraction coverage or native-speaker certification.

Remaining work includes translation completion, native-speaker/layout review,
minimum-OS and physical Intel testing, wider platform QA, dependency
reproducibility, optional Developer ID/notarization and a future application
build incorporating postrelease changes.
