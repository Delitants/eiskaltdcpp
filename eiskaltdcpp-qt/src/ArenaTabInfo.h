#pragma once

#include "ArenaWidget.h"
#include "HubFrame.h"
#include "TabNavigation.h"

namespace tab_navigation {

inline QString title(ArenaWidget *widget)
{
    const auto text = widget->getArenaShortTitle();
    return widget->role() == ArenaWidget::Hub ? compactTitle(text) : text;
}

inline QString details(ArenaWidget *widget)
{
    QString text = widget->getArenaTitle();
    if (auto *hub = dynamic_cast<HubFrame*>(widget)) {
        const auto url = hub->getHubUrl();
        if (!url.isEmpty() && !text.contains(url))
            text += QLatin1Char('\n') + url;
    }
    return text;
}

inline QString toolTip(ArenaWidget *widget)
{
    return QStringLiteral("<qt>") + details(widget).toHtmlEscaped().replace(
        QLatin1Char('\n'), QStringLiteral("<br/>")) + QStringLiteral("</qt>");
}

}
