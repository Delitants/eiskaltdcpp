# Polish main-window translations

## Scope

Completed 59 unfinished MainWindow entries, including navigation, Torrent
prompts, nickname requirements, version/status labels and contributor credits.
Previously finished translations and source/context/location content are
unchanged. Published binaries, updater feeds, installed app and profile are
untouched; these source changes require a future application build.

## Verification

- Initial focused regression failed on all 59 unfinished entries.
- Focused Qt compilation/readback passes with an explicit required-key inventory.
- Assertions preserve placeholders, HTML/link attributes, technical/platform
  identifiers and trailing whitespace.
- Scope checks confirm exactly 59 formerly unfinished entries changed.
- Disposable unfinished-entry, altered-link, placeholder and trailing-space
  controls each produced one expected failure without errors or skips.
- Full translation suite passed: 72 tests in 55.128 seconds, without failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues; native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/polish-mainwindow-20261016/`.

## Remaining

Polish unfinished entries decrease from 141 to 82; the overall catalog backlog
decreases from 7,410 to 7,351. Counts describe catalog content, not fresh
full-source extraction coverage or native-speaker certification.

Remaining work includes translations, native-speaker/layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional notarization and an updated application build.
