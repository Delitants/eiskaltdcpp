# macOS Open Handlers

The application bundle advertises `dchub`, `adc`, `nmdcs`, and `adcs` links.
Torrent-enabled builds also advertise `org.bittorrent.torrent` (`.torrent`).
The application does not repeatedly take over defaults when it starts.

After building and installing the signed application, the maintenance helper can
refresh LaunchServices registration and explicitly select it as the default:

```sh
xcrun swiftc tools/macos/open-handlers.swift -o /tmp/eiskalt-open-handlers
/tmp/eiskalt-open-handlers status
/tmp/eiskalt-open-handlers register '/Applications/EiskaltDC++.app'
/tmp/eiskalt-open-handlers check '/Applications/EiskaltDC++.app'
/tmp/eiskalt-open-handlers set-defaults '/Applications/EiskaltDC++.app' /tmp/eiskalt-previous-handlers.json
/tmp/eiskalt-open-handlers verify-defaults '/Applications/EiskaltDC++.app'
```

`set-defaults` requires a new backup filename and saves the affected associations
before changing them. It changes only the four DC schemes and the torrent file
type, not magnets or unrelated file types. macOS may request user consent.
Changes are independent: if one fails, inspect `status` and the backup before
retrying. The helper does not bypass consent or silently fall back to older APIs.
For builds without torrent support, use `check APP --no-torrent`.

Native APIs and bundle declarations follow Apple's documentation:
- [URL and data type declarations](https://developer.apple.com/documentation/bundleresources/data-and-storage)
- [Default URL handlers and consent](https://developer.apple.com/documentation/appkit/nsworkspace/setdefaultapplication(at:toopenurlswithscheme:completion:))
