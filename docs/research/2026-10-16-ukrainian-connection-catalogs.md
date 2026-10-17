# Ukrainian connection-settings translations

## Scope

Completed 18 unfinished entries in the Ukrainian Qt catalog, limited to
`DHTBootstrapList`, `SettingsConnection` and `UISettingsConnection`. They cover
DHT bootstrap URLs, IPv6 configuration and validation, public hub-list URLs,
and the MaxMind country database picker.

Source strings, runtime contexts, locations and unrelated translations are
unchanged. IPv6 example addresses, technical names, extensions and file-filter
syntax are preserved. This changes source catalogs only, not published pre.1
archives, updater feeds or the installed application/profile.

## Checks

- The focused regression initially failed on all 18 unfinished entries.
- The regression compiles with `lrelease -nounfinished` and reads the QM back
  with `lconvert`, checking finished nonempty text and exact round-trip values.
- Technical tokens include IPv6, WAN, DHT, URL, MaxMind, GeoLite2/MaxMind, MMDB,
  `.mmdb`, the example address, both file globs and the filter separator.
- English fallback text is rejected except for the unchanged `URL` label.
- Three disposable negative controls (missing WAN, corrupted MaxMind and
  English fallback) each produced the expected single regression failure.
- Final full suite: 27 tests passed in 66.096 seconds, with no errors, failures
  or skips, using Qt translation tools 6.11.1.
- Independent static review found no meaning error in the current entries;
  its two test-coverage findings were addressed. This is not native-speaker
  certification or visual layout testing.

Local test logs and scoped evidence: `Work/qa/ukrainian-connection-20261016/`.

## Backlog

Ukrainian unfinished entries decrease from 117 to 99. Across all 25 catalogs,
the unfinished-entry count decreases from 9,423 to 9,405. These are active TS
catalog counts, not a fresh extraction of only currently used runtime keys.

Further translation work and native-speaker review remain. Minimum-OS and
physical Intel testing, wider platform QA, dependency reproducibility and
optional Developer ID/notarization are separate tasks. A future application
build is needed to deliver these translation changes to users.
