/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "dcpp/version.h"

namespace ClientTagPresets {

struct Preset {
    QString label;
    QString nmdc;
    QString adc;
};

inline QList<Preset> presets()
{
    return {
        { QObject::tr("Default (EiskaltDC++)"), QString(), QString() },
        { QStringLiteral("DC++ 0.883"), QStringLiteral("++ V:0.883"), QStringLiteral("++ 0.883") },
        { QStringLiteral("ApexDC++ 1.6.5"), QStringLiteral("ApexDC++ V:1.6.5"), QStringLiteral("ApexDC++ 1.6.5") },
        { QStringLiteral("StrongDC++ 2.42"), QStringLiteral("StrgDC++ V:2.42"), QStringLiteral("StrongDC++ 2.42") },
        { QStringLiteral("AirDC++ 4.30"), QStringLiteral("AirDC++ V:4.30"), QStringLiteral("AirDC++ 4.30") },
        { QStringLiteral("FlylinkDC++ r505"), QStringLiteral("FlylinkDC++ V:r505"), QStringLiteral("FlylinkDC++ r505") },
        { QStringLiteral("LinuxDC++ 1.2.0"), QStringLiteral("LinuxDC++ V:1.2.0"), QStringLiteral("LinuxDC++ 1.2.0") }
    };
}

inline QStringList nmdcTags()
{
    QStringList tags;
    tags << QString::fromStdString(dcpp::fullNMDCVersionString);
    const auto list = presets();
    for (const Preset &preset : list) {
        if (!preset.nmdc.isEmpty())
            tags << preset.nmdc;
    }
    return tags;
}

inline QStringList adcTags()
{
    QStringList tags;
    tags << QString::fromStdString(dcpp::fullADCVersionString);
    const auto list = presets();
    for (const Preset &preset : list) {
        if (!preset.adc.isEmpty())
            tags << preset.adc;
    }
    return tags;
}

}
