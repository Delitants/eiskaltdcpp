# Polish browser and search translations

## Scope

Completed 29 unfinished entries covering file-browser media metadata, search
modes/progress, share restrictions, chat-editor labels and settings diagnostics.
Previously finished entries and source/context/location content are unchanged.
Published binaries, feeds, installed app and profile remain untouched.

## Verification

- Initial focused regression failed on all 29 unfinished entries.
- Focused compilation/readback test passes with an explicit required-key list.
- Assertions protect restriction actions, search modes, GB units, placeholders,
  HTML tags, nonbreaking spaces and trailing whitespace.
- Scope checks confirm exactly 29 formerly unfinished entries changed.
- Disposable swapped-action, placeholder, unit and trailing-space controls each
  produced one expected failure without errors or skips.
- Full translation suite passed: 70 tests in 52.285 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Literal percent signs
  and IPv6 text were reviewed manually, not explicitly asserted by the new test.
  Native-speaker and rendered-layout review remain pending.

Local evidence: `Work/qa/polish-browser-20261016/`.

## Remaining

Polish unfinished entries decrease from 201 to 172; the overall catalog backlog
decreases from 7,470 to 7,441. Counts describe catalog content, not fresh
full-source extraction coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
