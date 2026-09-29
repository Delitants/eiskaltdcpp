# Getting Started

1. Install a build from the project's Releases page, or follow the repository's
   build guide. Check the platform, architecture and release notes.
2. Set a nickname and download location in Preferences. Configure connection
   mode and any proxy before connecting to a hub or adding a Torrent.
3. Open Public Hubs or Quick Connect and choose an NMDC/ADC hub. Favorites keep
   frequently used hubs together. Hub rules may impose minimum shares or slots.
4. Choose DC share directories explicitly. Sharing a directory publishes its
   eligible contents to other users; avoid private folders.
5. Use Search or open a user's file list to queue files. A passive client cannot
   directly download from another passive client without a mutually supported
   connection mechanism. Repeating an impossible request will not make it work.
6. In a Torrent-enabled build, open Torrents and add a `.torrent` file or a
   BitTorrent magnet. Review file selection and DC-sharing policy.

`dchub://`, `adc://`, `nmdcs://` and `adcs://` identify hub connections. Operating
system link/file associations depend on the installed package and user-selected
default application; building the source alone does not register them.

The bottom Transfers panel monitors live DC and Torrent traffic. Toolbar buttons
can hide/show supported pages without losing their state. The tab overflow list
helps navigate many open hubs. Preferences and layout options vary by frontend.

## DHT Advertisement

Connection preferences separate **Enable DHT** from **Do not advertise DHT to
hubs**. The latter is off by default. It omits the DHT capability on the next
ADC/NMDC connection and does not change DHT enablement. Reconnect existing hubs
to apply it. It is not an anonymity feature and does not hide other traffic.
