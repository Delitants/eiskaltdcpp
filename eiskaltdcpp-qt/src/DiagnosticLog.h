/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QElapsedTimer>
#include <QMutex>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <exception>

class QObject;
class QTimer;

class DiagnosticLog
{
public:
    static DiagnosticLog& instance();

    void install();
    void configure(bool enabled, const QString& logDirectory, const QString& fileName);
    void startHeartbeat(QObject* parent);
    void logLine(const QString& message);

    [[nodiscard]] QString path() const;
    [[nodiscard]] bool isEnabled() const;

private:
    DiagnosticLog();
    ~DiagnosticLog();

    DiagnosticLog(const DiagnosticLog&) = delete;
    DiagnosticLog& operator=(const DiagnosticLog&) = delete;

    void closeLog();
    void rotateIfNeeded(const QString& path) const;
    void writeLineUnlocked(const QString& message);

    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
    static void terminateHandler();

#if !defined(Q_OS_WIN)
    static void fatalSignalHandler(int signalNumber);
    static void installSignalHandlers();
#endif

    mutable QMutex mutex_;
    QElapsedTimer uptime_;
    QTimer* heartbeatTimer_ = nullptr;
    QString path_;
    int fd_ = -1;
    bool enabled_ = false;
    bool installed_ = false;
    QtMessageHandler previousQtMessageHandler_ = nullptr;
    std::terminate_handler previousTerminateHandler_ = nullptr;

    static std::atomic<int> signalFd_;
};
