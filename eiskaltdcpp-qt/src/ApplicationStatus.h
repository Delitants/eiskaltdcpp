#pragma once
#include "dcpp/LogManager.h"
#include <QLabel>
#include <QFontMetrics>
#include <QStringList>

namespace application_status {
inline void diagnostic(dcpp::LogManager &logs, const QString &message)
{
    if (message.isEmpty()) return;
    dcpp::ParamMap params;
    params["message"] = message.toStdString();
    // Do not emit LogManager::Message, which replaces the main status banner.
    logs.log(dcpp::LogManager::SYSTEM, params, false);
}

inline void record(dcpp::LogManager &logs, const QString &message)
{
    if (message.isEmpty()) return;
    dcpp::ParamMap params;
    params["message"] = message.toStdString();
    logs.log(dcpp::LogManager::STATUS, params, false);
}

inline void display(QLabel &label, const QString &message, QStringList &history, int limit, bool warning = false)
{
    label.setPalette(QPalette());
    if (warning) {
        auto palette = label.palette();
        const bool dark = palette.color(QPalette::Window).lightness() < 128;
        palette.setColor(QPalette::WindowText, QColor(dark ? "#ff7b72" : "#b42318"));
        label.setPalette(palette);
    }
    label.setTextFormat(Qt::PlainText);
    label.setText(QFontMetrics(label.font()).elidedText(message, Qt::ElideRight, label.width()));
    if (history.isEmpty() || history.last() != message) history.append(message);
    while (history.size() > qMax(0, limit)) history.removeFirst();
    label.setToolTip("<qt>" + history.join('\n').toHtmlEscaped().replace("\n", "<br/>") + "</qt>");
}
}
