#pragma once

#include <QLocale>
#include <QString>

namespace country_names {
inline QString fromCode(const QString& countryCode) {
    const QString code = countryCode.trimmed().toUpper();
    if(code.size() != 2)
        return {};
    const auto territory = QLocale::codeToTerritory(code);
    if(territory == QLocale::AnyTerritory)
        return {};
    return QLocale::territoryToString(territory);
}
}
