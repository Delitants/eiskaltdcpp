# Polish hubs, notes and magnet translations

## Scope

Completed 31 unfinished public-hub status, notes/debug-console, magnet-sharing,
multiline-input and hashing-progress entries. Previously finished entries and
source/context/location content are unchanged. Published binaries, update feeds,
installed app and profile remain untouched.

## Verification

- Initial focused regression failed on all 31 unfinished entries.
- Focused Qt compilation/readback check passes with explicit required keys.
- Assertions protect placeholders, literal percent signs, Shift+Enter and other
  technical tokens, plus exact clear/find/copy/refresh actions.
- Scope checks confirm exactly 31 formerly unfinished entries changed.
- Disposable percent-sign, shortcut, action and placeholder controls produced
  only expected assertion failures, without errors or skips.
- Full translation suite passed: 71 tests in 56.658 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/polish-hubs-20261016/`.

## Remaining

Polish unfinished entries decrease from 172 to 141; the overall catalog backlog
decreases from 7,441 to 7,410. Counts describe catalog content, not fresh
full-source extraction coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
