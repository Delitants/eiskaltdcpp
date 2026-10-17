# Georgian connection translations

## Scope

Completed 20 unfinished entries covering IPv6, DHT bootstrap URLs, public hub
list URLs, country database selection and hub/client certificate-trust options.
The existing TLS disable/allow/require translations remain unchanged and are
now covered by explicit compiled-catalog regression assertions.

Published binaries, updater feeds, installed app and profile are unchanged.
These source changes require a future application build.

## Verification

- Initial two-test regression produced 22 expected failures: 18 connection
  entries and two certificate entries checked in both source and compiled form.
- Both focused tests passed after translation, including Qt compilation/readback.
- Full translation suite passed: 76 tests in 71.939 seconds, without skips.
- `git diff --check` passed.
- Independent scope checks confirmed exactly 20 formerly unfinished entries
  changed and all non-translation XML content remained unchanged.
- Disposable unfinished-entry and IPv6-token controls each produced one expected
  failure. An altered certificate-trust label produced two expected failures,
  one in source and one in compiled readback.
- The control harness initially expected only one certificate-label failure;
  that failed run is retained. Inspection confirmed the helper checks both
  catalogs, and the corrected control count passed without production changes.
- Independent static review found no actionable findings. Native-speaker and
  rendered-layout review remain pending.

Local evidence: `Work/qa/georgian-connection-20261016/`.

## Remaining

Georgian unfinished entries decrease from 328 to 308; overall unfinished entries
decrease from 7,269 to 7,249. These catalog counts do not certify full-source
extraction coverage or translation quality.

Remaining work includes other translations, native-speaker/layout review,
minimum-OS and physical Intel testing, wider platform QA, dependency
reproducibility, optional notarization and an updated application build.
