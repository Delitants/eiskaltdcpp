# Compact Native GUI

The native Qt interface follows the selected Compact Native Superdesign direction.
It remains a desktop application, not an embedded web interface.

- Action buttons use compact, content-sized labels. Icon-only, text-only, text-beside-icon and text-under-icon modes remain available in toolbar customization.
- Hidden enabled actions remain reachable in the native overflow menu. Tabs stay on their own row.
- Hub chat starts with a compact composer and a wider draggable splitter. Users can still resize it.
- Connection errors have an inline recovery banner. Authentication recovery opens the current hub's favourite editor; general connection recovery opens Preferences.
- Shared input, menu and Preferences styles use compact spacing and system-palette colours. Native checkbox and radio indicators remain native.
- Superdesign is a separate default icon theme. Prism, Contour, Slate, Reborn and legacy themes remain selectable; existing saved choices are not silently rewritten. New artwork includes explicit light/dark SVG variants and preserves original application branding.

## Preferences Layout

- Connection separates incoming connectivity, Proxy, Limits and Advanced. Proxy contains Global, Torrents and Hub lists profiles; the Torrent page no longer duplicates the proxy editor. Existing routing and saved values remain unchanged.
- Advanced groups reconnection, discovery and TLS identity management. The TLS policy selector is content-sized rather than stretched across the page.
- Downloads combines default and incomplete folders with saved destination shortcuts. Shortcuts are named destinations offered by download actions, not another default folder.
- Status icon settings use an inline Off checkbox instead of a checkable group title. The coloured/monochrome choice is retained while Off is selected.
- On macOS, checkbox/radio artwork is aligned with its caption and default list checkmarks are painted in their own rows. Specialized delegates and wrapped updater captions keep their own layout paths.
- Under-icon toolbar captions retain their full text. Whole actions move into the overflow menu when space is insufficient; captions are not shortened to fit more buttons.
- Preferences use borderless page/tab containers and headings above meaningful section frames. Compact numeric forms stay at the leading edge instead of centering in surplus space.
- Updates uses a fully sized list popup and natural-size wrapping actions. Disabled checkbox artwork remains visible without flattening checked/mixed states.
- Main, History, Chat and Torrent forms keep related fields adjacent. Connection Limits separates default and scheduled limits; Search reserves space between its checkbox and shared-file filter.
- Queue priority/drop settings use content-sized numeric column pairs instead of spreading fields across the entire page. Native font/style changes recompute their size hints.
- Shortcuts displays native platform key names and macOS modifier glyphs while keeping editing and saved bindings in the portable format.
- Public Hubs prioritizes `hublist.eu` in fresh defaults and existing source lists containing it. Other sources and saved custom list text are preserved; displayed and downloaded source indices use the same ordering.
- A Torrent storage error takes precedence over the empty-files metadata hint. This does not change storage paths or resume networking.
- Quit timeout warnings retain the original prefix and add the last observed phase/status, elapsed and budget milliseconds, last preparation-call duration, and timeout branch. Cached GUI-producer, Torrent and settings observations do not claim a worker is still pending. Deadlines, explicit retry and normal-quit update consent are unchanged.

## Design Coverage

The full-GUI pass uses ten navigable families: workspace/chat, Connection,
core Preferences, appearance/productivity Preferences, Torrent workspace,
Torrent Preferences/dialogs, hubs/favourites, search/files, transfers/diagnostics,
and tools/auxiliary dialogs.

The design pass covered all 45 Designer forms and code-built Torrent
panes/preferences, GOST, certificates, updater settings and auxiliary dialogs.
Private generated design compositions are not part of the source release. Those
HTML prototypes are
visual references, not implementations of networking, certificate generation,
update installation or file operations. Native Qt controls and existing security
policies remain authoritative. OS-owned file pickers are not redesigned.

The icon source, licence and offline regeneration instructions are documented in
`tools/icon-themes/SUPERDESIGN.md`.

## Authentication

Legacy ADC challenge compatibility is disabled by default and applies only to a
saved favourite on its next connection. The failure banner never enables it.
See `adc-legacy-authentication.md` for the weaker-challenge warning and scope.
