#pragma once

#include <QStringList>

namespace eiskalt::torrent {

inline QStringList defaultTrackers()
{
    // Announce-tested 2026-09-07. These are creation defaults, not trackers
    // injected into existing/private torrents or contacted by the dialog.
    return {
        QStringLiteral("udp://dhtb.hublist.eu:6969/announce"),
        QStringLiteral("http://dhtb.hublist.eu:6969/announce"),
        QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
        QStringLiteral("udp://open.demonii.com:1337/announce")
    };
}

}
