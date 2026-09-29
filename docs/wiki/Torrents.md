# Torrents And DC Sharing

Torrent support requires a build with `USE_TORRENT=ON`, Qt6 and a compatible
libtorrent-rasterbar 2.1+ library. It is not a mixed DC/BitTorrent piece engine.

## Jobs And Files

Use the Torrents toolbar to add a file or magnet, create metadata, pause, resume,
stop, recheck, remove a job or explicitly delete its data. Read destructive
confirmations carefully: removing a job and deleting its files are different.
A stopped job remains in the Torrents tab but is absent from live transfers.
Downloading, seeding, paused and stopped states use green, blue, light orange and
grey indicators respectively, in addition to text.

Select a job to see its files, peers and security controls. File checkboxes choose
wanted files; priorities affect their scheduling. Apply the selection where
requested. An empty selection requests no files and does not delete existing
files. File selection and explicit recheck can revoke managed DC publication
until eligibility is established again.

The Peers page displays the connected endpoints, client identification, progress,
rates and connection state. Country flags depend on available local GeoIP data.
They may be missing or inaccurate and do not prove a peer's actual location.

## DC Publication

Completed-file DC sharing is enabled by default for new Torrent settings; an
explicit saved off choice is preserved. Only eligible, completed, selected and
verified files of a non-private Torrent are published after TTH validation.
Publication does not share the entire download directory.

A job's **DC++ sharing** menu can exclude that Torrent regardless of the global
setting. This exclusion does not remove a file from a separately configured
manual DC share. Private Torrents are excluded from managed publication, but the
same manual-share caveat applies.

Valid cached hashes can be reused across restart and ordinary stop/resume.
Changed, missing or unverifiable files require validation; cached sharing state
is not permission to serve unverified data.

A BitTorrent magnet identifies a Torrent, including a multi-file Torrent. DC TTH
magnets identify individual files rather than arbitrary folders. The chat-sharing
actions prepare magnets; where a folder has no file-level magnet, select the
files to share instead. Review the chat text before sending.

## Security Controls

The Security section applies globally to Torrent jobs, not just the selected
row. Apply security changes explicitly; sessions reconnect when necessary.
Configure peer encryption and the country blocklist, and optionally block
unrecognized initial BitTorrent peer IDs. Unknown clients are not necessarily
malicious, and an initial peer ID is not authenticated identity.

Country blocks apply to known IP countries; unmapped addresses remain allowed.
Proxies and VPNs obscure location. HTTP web seeds are disabled while either peer
block rule is enabled because they do not provide the same BitTorrent handshake.
These settings do not replace endpoint security or permission to distribute data.
