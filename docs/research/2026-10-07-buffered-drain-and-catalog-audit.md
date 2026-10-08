# Buffered Drain And Current Catalog Audit

## Shutdown Diagnosis

Candidate `2084f5b3` plus the unchanged seven-file local repair. The original
Intel large-scope shutdown failure remains **unresolved and unwaived**.
Lower-overhead private telemetry does not reproduce it. Stop repeating this
successful setup; retain the acceptance hold while independent work proceeds.

Worker-local fixed-size accumulators record complete helper timings in memory
and emit scope/terminal state only after evaluating the drain return. Original
test bodies, helpers, native flow, assertions and three-second deadlines remain
unchanged. No artificial system load, injected delay or validation omission.
Instrumentation still changes timing and is not a shipped diagnostic.

Six receipts pass 84 case instances / 1,290 assertions, zero errors/skips/failing
assertions: one unchanged Intel planner/journal/engine selection (79/1,255), four
focused Intel large-case runs (1/7 each), and one ARM comparison (1/7).
The Intel inventory matches the retained original selection exactly.

All six scoped drain summaries identify 10,000 files / 500,000 pieces. They end
with zero active checks and journal records, completed native cancellation and
successful local/final checkpoint receipts. Intel drain times range from
2,029,113us to 2,633,886us; the suite's large-case drain is 2,351,829us. ARM takes
1,458,840us. Intel metadata validation totals 1.50-1.82 seconds across two calls
per drain. This confirms repeated validation cost without per-poll logging;
it does not identify the earlier failed return's cause or justify removing checks.

Private consumers reuse 44 pinned backend link/metadata inputs; reversing only
the copied engine include substitution recovers exact original test bytes.
An initial generator compilation failure from printf newline escaping remains
retained; corrected escaping uses separate outputs. No failed receipt is erased.
Runs are serial, seed `594128903`, disposable homes, verified loopback-only
external-network denial and matching maintained libtorrent providers. Timed runs
do not overlap compilation or heavy hashing. Source/local repair, historical
app/signature, archive/provider/protected-manifest readbacks and preceding
95/64-file evidence seals verify. The new exact seal covers 54 files and six
disposable homes in `Work/qa/selected-buffered-drain-20261007/`.

Buffered seal SHA-256:
`2dd3d355cd3b18015058929cb0da4134a6bbce4104cb01f71b2e7915b7c78856`.

## Independent Localization Audit

The unchanged 23-test translation suite reports exactly **75 missing-key
failures**, zero errors/skips. Full fresh production-manifest extraction reads
356 files and identifies 2,066 runtime keys. All 25 catalogs lack exactly these
three context/source pairs:

- `TorrentEngine`: `Torrent could not accept an incoming connection.`
- `TorrentEngine`: `Torrent could not bind a configured listening endpoint.`
- `TorrentProxyAdapter`: `The proxy adapter is retiring for application quit.`

The listener messages already have finished translations under the former
`eiskalt::torrent::TorrentEngine` context. Runtime formatting now uses
`TorrentEngineText` with `Q_DECLARE_TR_FUNCTIONS(TorrentEngine)`, so those
existing entries do not satisfy the current lookup. Fifty existing translations
are reusable verbatim; the proxy-retirement message needs 25 new entries.

No catalog is edited by extraction: lupdate receives an expanded source list
and writes a fresh private TS target. Current source/catalog/tool pins and the
original dirty patch verify. Other audited format/extraction/QM tests pass; the
exact 75-failure set is independently checked, not confused with tool setup.
The fresh inventory also reports 9,327 unfinished runtime entries across locales,
including 9,159 empty entries. These are a separate inherited completion/native-
language-quality backlog, not three newly missing keys or language certification.

The bounded repair proposal reuses the existing listener wording in the correct
context, adds the retirement message in every catalog, preserves prior entries,
and adds compiled-QM regression coverage. It awaits the maintainer's design
approval; no translation or runtime change is made in this checkpoint.

Catalog evidence under `Work/qa/current-catalog-audit-20261007/` has an exact
20-file seal, including the failure log, extraction, pin and preservation receipts.
Catalog seal SHA-256:
`aca4841d70c506c061e5fe066a5f2bbf34b23f645ed8cbd3691074fe5fbb3a09`.

## Pending

The three-key catalog repair is next after its bounded-design approval. Retain
the original Intel refusal until its actual failure boundary and causal repair
are established; green repetition is not acceptance. Overall Torrent Task5,
GUI-settings Task3 and updater Tasks7/8 remain open. Historical bind/freeze/
latency/recovery causes, active-download/composed-GOST/partial-piece-crash/live/
external checks, wider lifecycle/native minimum-OS/benchmark/soak, inherited
unfinished/native-language quality, provenance/reproducibility and final whole
review/build/allocation/signing/notarization/signed-v3/install/publication/readback
remain pending. No installed app/profile/server/VM/key changes or publication;
production signed feeds remain last.
