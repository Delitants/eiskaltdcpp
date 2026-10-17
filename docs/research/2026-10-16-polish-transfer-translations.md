# Polish transfer and command translations

## Scope

Completed 25 unfinished entries in HubFrame, Settings, TransferView and
TransferViewModel: command help, Torrent preferences, transfer actions/status.
Previously finished entries and source/context/location content are unchanged.
Published binaries, updater feeds, installed app and profile remain untouched.

## Verification

- Initial regression failed on all 25 unfinished entries.
- The disposable edit script initially refused a finished Protocol entry in a
  different context before writing. Restricting it to unfinished entries preserved
  that existing translation. Initial logs are retained; final verification uses
  a fresh full-suite run after the catalog edit.
- Focused compilation/readback check passes, with exact pause/resume/cancel and
  status wording, command prefixes, placeholders, line breaks and trailing space.
- Scope check confirms exactly 25 formerly unfinished entries changed.
- Disposable pause-label, command-token, placeholder and trailing-space controls
  each produced one expected failure, without errors or skips.
- Fresh full translation suite passed: 67 tests in 60.348 seconds, without
  failures, errors or skips. `git diff --check` passed.
- Independent static review found no actionable issues. Native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/polish-transfers-20261016/`.

## Remaining

Polish unfinished entries decrease from 261 to 236; the overall catalog backlog
decreases from 7,530 to 7,505. These counts describe catalog content, not fresh
full-source extraction coverage or native-speaker certification.

Other translations (including Polish download-policy help), native-speaker/layout
review, minimum-OS and physical Intel testing, wider platform QA, dependency
reproducibility, optional notarization and an updated app build remain pending.
