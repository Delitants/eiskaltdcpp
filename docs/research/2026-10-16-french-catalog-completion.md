# French catalog completion

## Scope

Completed 91 unfinished French entries covering connection settings, chat and
Torrent controls, configuration migration notices, appearance settings and
diagnostic logging. Previously finished French entries and other locales are
unchanged. Source strings, runtime contexts and locations are preserved.

No published archive, signed update feed, installed application or user profile
is modified. These source translations require a future build to reach users.

## Verification

- The initial French completeness test failed on exactly 91 unfinished entries.
- The focused completed-catalog test passed after translation. Ukrainian and
  French now share the same completeness and compiled-value helper, retaining
  separate test entry points.
- Qt `lrelease -nounfinished` and `lconvert` verify every active French entry
  is finished, nonempty and survives compilation with its exact translated value.
- Scope checks preserve HTML tags/attributes, links, newlines, placeholders,
  IPv6 example addresses, technical names, units and file-filter syntax.
- Disposable negative controls reverting an entry to unfinished or changing
  a help link each produced one expected regression failure.
- Independent static review found no actionable findings in the catalog or
  shared-helper refactoring. Native-speaker and rendered-layout review remain.
- The full 31-test suite passed in 61.915 seconds without failures, errors or
  skips, using Qt translation tools 6.11.1.

Evidence is retained in `Work/qa/french-completion-20261016/`.

## Remaining

French unfinished entries decrease from 91 to zero. The total across all 25
catalogs decreases from 9,306 to 9,215. These are catalog-content counts, not
fresh full-source extraction coverage or native-speaker certification.

Other-language completion, native-speaker/rendered-layout review, minimum-OS
and physical Intel testing, wider platform QA, dependency reproducibility,
optional Developer ID/notarization and a future build remain separate tasks.
