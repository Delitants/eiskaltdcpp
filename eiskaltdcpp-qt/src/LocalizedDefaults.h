/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QString>

namespace dcpp {
class SettingsManager;
}

namespace LocalizedDefaults {

QString awayMessage();
QString awayMessageForTranslationFile(const QString &translationFile,
                                      const QString &translationsPath);
bool isAwayMessageDefault(const QString &message, const QString &translationsPath);
void refreshAwayMessageSetting(dcpp::SettingsManager *settings,
                               const QString &translationsPath,
                               const QString &translationFile = QString());

}
