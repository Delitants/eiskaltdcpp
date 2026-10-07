# Torrent Quit Settings Freeze And Compatibility Revision

Status: approved by the maintainer on 2026-10-07. This does not accept Task4 or
authorize installation, publication, signing, feeds, live profile changes or
changes to the original test helpers, holds or request budgets.

## Observed Boundary

Task4's checked runtime and composed checkpoint selectors pass on ARM:
144 and 534 assertions, respectively. The retained blocking runtime fails
exactly 22 assertions and the composed baseline fails exactly 18. The original
settings suite then fails 23 assertions across four cases, without setup errors
or skips. Its complete receipt is retained privately under the Task4 QA root.

The new MainWindow producer milestone correctly stays incomplete while Torrent
owners retire. However, settings freeze/owned persistence currently begins only
after that milestone. Dirty QSettings can receive an automatic GUI-thread sync
before then, and late settings writes remain admitted. That accounts for the
observed GUI writer, heartbeat, frozen-value and saved-byte failures.

Two assertions in the existing core-admission case check route revocation
immediately after `session.request(100)`, before the GUI event loop polls the
new Torrent retirement. That assumption conflicts with retaining the route
until publisher, engine and proxy physical receipts are collected. Core must
not drain early merely to satisfy that assumption.

## Approved Narrow Repair

1. Once retained GUI workers and layouts have completed, start the existing
   owned settings-only persistence path exactly once. Its existing freeze
   removes posted auto-sync requests, rejects later mutations and leaves GUI
   reads on the frozen snapshot. No new worker type or storage ownership path
   is introduced.
2. Poll settings and Torrent receipts independently. Torrent Pending still
   reports workers pending and producers incomplete. Core and route retirement
   remain barred until all Torrent physical receipts are collected, even if
   settings persistence has already completed or failed.
3. Cache aggregate layout/settings/Torrent outcomes. A failure stays refused;
   all physical owners remain retained. Explicit retry must reuse completed
   snapshots and checkpoints. Late completion never commits exit/install.
4. Make the smallest explicit exception to the original-test-body constraint:
   move the core route observation behind an event-pumped producer-retirement
   readiness check in its existing ordinary/Ready-update variants. Keep the
   route-revoked assertions and all other original assertions. Never increase
   the 100ms request budget, 350ms physical hold or 5000ms existing helper.
5. Add a composed regression that pins settings freeze and off-GUI writes
   while a real Torrent checkpoint remains held. Retain a pre-retirement
   route-not-revoked assertion, then require revocation only after physical
   producer retirement and correctly fenced updater admission.

## Verification And Limits

First preserve the current failed original suite as RED. Require sensitivity
to deferring settings freeze and to premature core drain. Rebuild both CPU
variants, run all four original selectors, new composed/portable selectors,
the applicable no-Torrent settings variant and complete Qt suites. Task5's
complete integrated matrix, immutable independent review, historical failure
readbacks and all broader acceptance/release gates remain required.

The alternative is to retain the absolute original-test-body constraint and
leave this composition unaccepted while the compatibility contract is
redesigned. Do not revoke routes early, skip those tests, fabricate physical
completion or call this repair complete.

## Isolated Revision Verification

Both CPU variants pass the runtime focus (144 assertions), composed checkpoint
and settings-freeze focus (648), original settings cases (688), and complete Qt
suites (754 cases each, ARM 34,163 / Intel 34,138 assertions). The applicable
no-Torrent settings consumer passes 12 cases / 656 assertions with current
GUI/test translation units and retained accepted dependencies; it is not a
fresh whole no-Torrent build.

The pre-repair composed freeze control fails exactly six frozen-read/final-byte
assertions. A private premature-core-drain control fails exactly six retained
route assertions. Original RED23 and blocking RED22/18 remain sealed. Original
test bytes are unchanged except the approved producer-readiness wait before
the existing route observation; helpers, holds and budgets are unchanged.
Thirty-six source/helper/binary/control pins, complete case inventories and
protected-state/prior-evidence readbacks pass. This verifies the focused
revision, not final Task5, native minimum-OS, live app or release acceptance.
