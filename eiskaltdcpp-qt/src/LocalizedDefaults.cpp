/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "LocalizedDefaults.h"

#include "dcpp/stdinc.h"
#include "dcpp/SettingsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSet>
#include <QTranslator>

namespace {

const char *awayMessageContext()
{
    return "SettingsPersonal";
}

const char *awayMessageSource()
{
    return "I'm away. State your business and I might answer later if you're lucky.";
}

void insertMessage(QSet<QString> &messages, const QString &message)
{
    const QString normalized = message.trimmed();
    if (!normalized.isEmpty())
        messages.insert(normalized);
}

QString translatedAwayMessage(QTranslator &translator)
{
    return translator.translate(awayMessageContext(), awayMessageSource());
}

QString translateFromFile(const QString &translationFile)
{
    if (translationFile.trimmed().isEmpty())
        return QString();

    QTranslator translator;
    if (!translator.load(QDir::fromNativeSeparators(translationFile)))
        return QString();

    return translatedAwayMessage(translator);
}

QString translateFromLocale(const QString &localeName, const QString &translationsPath)
{
    if (localeName.trimmed().isEmpty())
        return QString();

    QTranslator translator;
    if (!translator.load(localeName, QDir::fromNativeSeparators(translationsPath)))
        return QString();

    return translatedAwayMessage(translator);
}

QSet<QString> knownAwayMessages(const QString &translationsPath)
{
    QSet<QString> messages;
    insertMessage(messages, QString::fromLatin1(awayMessageSource()));
    insertMessage(messages, LocalizedDefaults::awayMessage());

    QDir dir(QDir::fromNativeSeparators(translationsPath));
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.qm"),
                                           QDir::Files | QDir::NoSymLinks,
                                           QDir::Name);

    for (const QString &file : files)
        insertMessage(messages, translateFromFile(dir.absoluteFilePath(file)));

    return messages;
}

}

namespace LocalizedDefaults {

QString awayMessage()
{
    return QCoreApplication::translate(awayMessageContext(), awayMessageSource());
}

QString awayMessageForTranslationFile(const QString &translationFile,
                                      const QString &translationsPath)
{
    QString message = translateFromFile(translationFile);
    if (!message.trimmed().isEmpty())
        return message;

    message = translateFromLocale(QLocale::system().name(), translationsPath);
    if (!message.trimmed().isEmpty())
        return message;

    message = translateFromLocale(QStringLiteral("en"), translationsPath);
    if (!message.trimmed().isEmpty())
        return message;

    return QString::fromLatin1(awayMessageSource());
}

bool isAwayMessageDefault(const QString &message, const QString &translationsPath)
{
    const QString normalized = message.trimmed();
    if (normalized.isEmpty())
        return true;

    return knownAwayMessages(translationsPath).contains(normalized);
}

void refreshAwayMessageSetting(dcpp::SettingsManager *settings,
                               const QString &translationsPath,
                               const QString &translationFile)
{
    if (!settings)
        return;

    const QString current = QString::fromStdString(
                settings->get(dcpp::SettingsManager::DEFAULT_AWAY_MESSAGE, true));
    if (!isAwayMessageDefault(current, translationsPath))
        return;

    const QString updated = translationFile.isNull()
            ? awayMessage()
            : awayMessageForTranslationFile(translationFile, translationsPath);
    settings->set(dcpp::SettingsManager::DEFAULT_AWAY_MESSAGE, updated.toStdString());
}

}
