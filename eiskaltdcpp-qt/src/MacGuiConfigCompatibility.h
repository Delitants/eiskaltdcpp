#pragma once

#include <QString>

namespace mac_gui_config {
inline QString compatibilityVersion()
{
    // Bump only for an incompatible GUI schema, never for an application release.
    return QStringLiteral("qt6-native-palette-v1");
}

inline bool needsMigration(const QString &stored)
{
    // These releases already applied the same palette/layout migration.
    return stored != compatibilityVersion() && stored != QStringLiteral("2.5.6") &&
           stored != QStringLiteral("3.0.0");
}
}
