# Serbian Cyrillic catalog completion

## Scope

Completed the 126 unfinished entries in `sr.ts`, covering connection settings,
Torrent actions/status, migration and backup diagnostics, chat, appearance,
help and credits. Existing finished entries and the separate Serbian Latin
catalog are unchanged. No application behavior was changed.

Published archives, updater feeds, installed app and user profile remain
unchanged. These translations require a future application build.

## Verification

- Initial focused regression run: 129 expected failures (126 unfinished entries
  plus three overlapping rich-help checks).
- Focused completion/compilation and rich-help tests pass. Qt `lrelease
  -nounfinished` and `lconvert` check every active catalog entry is nonempty,
  finished and survives compilation with its exact translated value.
- Scope check confirms exactly 126 formerly unfinished entries changed;
  source/context/location content and previously finished translations are
  unchanged. Edit-time checks preserve HTML, placeholders and newline counts.
- Disposable negative controls for unfinished status and a changed help URL
  each produced one expected failure, without errors or skips.
- Full translation suite: 46 tests passed in 51.560 seconds, with no failures,
  errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. This is not
  native-speaker certification or rendered-layout review.

Local evidence: `Work/qa/serbian-completion-20261016/`.

## Remaining

Serbian Cyrillic unfinished entries decrease from 126 to zero. Across all 25
catalogs, unfinished entries decrease from 8,523 to 8,397. These counts describe
catalog content, not fresh full-source extraction coverage.

Other-language completion (including Serbian Latin), native-speaker/layout
review, minimum-OS and physical Intel testing, wider platform QA, dependency
reproducibility, optional Developer ID/notarization and a future app build
remain separate tasks.
