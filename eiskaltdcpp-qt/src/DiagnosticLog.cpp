/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "DiagnosticLog.h"

#include "VersionGlobal.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTimer>

#include <cerrno>
#include <cstring>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#define diagnostic_close _close
#define diagnostic_open _open
#define diagnostic_write _write
#ifndef O_APPEND
#define O_APPEND _O_APPEND
#endif
#ifndef O_CREAT
#define O_CREAT _O_CREAT
#endif
#ifndef O_WRONLY
#define O_WRONLY _O_WRONLY
#endif
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define diagnostic_close ::close
#define diagnostic_open ::open
#define diagnostic_write ::write
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif

namespace {
constexpr qint64 MAX_DIAGNOSTIC_LOG_BYTES = 32LL * 1024LL * 1024LL;
constexpr int HEARTBEAT_INTERVAL_MS = 60 * 1000;

QByteArray severityName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QByteArrayLiteral("debug");
    case QtInfoMsg: return QByteArrayLiteral("info");
    case QtWarningMsg: return QByteArrayLiteral("warning");
    case QtCriticalMsg: return QByteArrayLiteral("critical");
    case QtFatalMsg: return QByteArrayLiteral("fatal");
    }

    return QByteArrayLiteral("unknown");
}

void appendLiteral(char* buffer, size_t& pos, const char* value)
{
    while (*value != '\0')
        buffer[pos++] = *value++;
}

void appendInt(char* buffer, size_t& pos, int value)
{
    if (value == 0) {
        buffer[pos++] = '0';
        return;
    }

    if (value < 0) {
        buffer[pos++] = '-';
        value = -value;
    }

    char digits[16];
    size_t count = 0;
    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }

    while (count > 0)
        buffer[pos++] = digits[--count];
}
} // namespace

std::atomic<int> DiagnosticLog::signalFd_ {-1};

DiagnosticLog& DiagnosticLog::instance()
{
    static DiagnosticLog logger;
    return logger;
}

DiagnosticLog::DiagnosticLog()
{
    uptime_.start();
}

DiagnosticLog::~DiagnosticLog()
{
    closeLog();
}

void DiagnosticLog::install()
{
    QMutexLocker locker(&mutex_);
    if (installed_)
        return;

    previousQtMessageHandler_ = qInstallMessageHandler(DiagnosticLog::qtMessageHandler);
    previousTerminateHandler_ = std::set_terminate(DiagnosticLog::terminateHandler);
#if !defined(Q_OS_WIN)
    installSignalHandlers();
#endif
    installed_ = true;
}

void DiagnosticLog::configure(bool enabled, const QString& logDirectory, const QString& fileName)
{
    QMutexLocker locker(&mutex_);

    closeLog();
    enabled_ = enabled;
    if (!enabled_)
        return;

    QString directory = logDirectory.trimmed();
    if (directory.isEmpty())
        directory = QDir::homePath();

    QString cleanFileName = fileName.trimmed();
    if (cleanFileName.isEmpty())
        cleanFileName = QStringLiteral("Diagnostic.log");

    const QFileInfo fileInfo(cleanFileName);
    path_ = fileInfo.isAbsolute()
            ? fileInfo.absoluteFilePath()
            : QDir(directory).absoluteFilePath(cleanFileName);

    const QFileInfo targetInfo(path_);
    QDir().mkpath(targetInfo.absolutePath());
    rotateIfNeeded(path_);

    fd_ = diagnostic_open(QFile::encodeName(path_).constData(),
                          O_WRONLY | O_CREAT | O_APPEND | O_BINARY,
                          0644);

    if (fd_ < 0) {
        enabled_ = false;
        path_.clear();
        signalFd_.store(-1);
        return;
    }

    signalFd_.store(fd_);
    writeLineUnlocked(QStringLiteral("=== diagnostic log enabled; version=%1 pid=%2 app=%3 ===")
                      .arg(QString::fromStdString(eiskaltdcppVersionString))
                      .arg(QCoreApplication::applicationPid())
                      .arg(QCoreApplication::applicationFilePath()));
}

void DiagnosticLog::startHeartbeat(QObject* parent)
{
    if (heartbeatTimer_)
        return;

    heartbeatTimer_ = new QTimer(parent);
    heartbeatTimer_->setInterval(HEARTBEAT_INTERVAL_MS);
    QObject::connect(heartbeatTimer_, &QTimer::timeout, [] {
        DiagnosticLog::instance().logLine(
                    QStringLiteral("heartbeat uptime_s=%1")
                    .arg(DiagnosticLog::instance().uptime_.elapsed() / 1000));
    });
    heartbeatTimer_->start();

    logLine(QStringLiteral("heartbeat started interval_ms=%1").arg(HEARTBEAT_INTERVAL_MS));
}

void DiagnosticLog::logLine(const QString& message)
{
    QMutexLocker locker(&mutex_);
    writeLineUnlocked(message);
}

QString DiagnosticLog::path() const
{
    QMutexLocker locker(&mutex_);
    return path_;
}

bool DiagnosticLog::isEnabled() const
{
    QMutexLocker locker(&mutex_);
    return enabled_ && fd_ >= 0;
}

void DiagnosticLog::closeLog()
{
    signalFd_.store(-1);
    if (fd_ >= 0) {
        diagnostic_close(fd_);
        fd_ = -1;
    }
    path_.clear();
}

void DiagnosticLog::rotateIfNeeded(const QString& path) const
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < MAX_DIAGNOSTIC_LOG_BYTES)
        return;

    const QString rotatedPath = path + QStringLiteral(".1");
    QFile::remove(rotatedPath);
    QFile::rename(path, rotatedPath);
}

void DiagnosticLog::writeLineUnlocked(const QString& message)
{
    if (!enabled_ || fd_ < 0)
        return;

    QByteArray line;
    line.reserve(message.size() + 64);
    line += '[';
    line += QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8();
    line += "] ";
    line += message.toUtf8();
    line += '\n';

    const char* data = line.constData();
    qsizetype remaining = line.size();
    while (remaining > 0) {
        const auto written = diagnostic_write(fd_, data, static_cast<unsigned int>(remaining));
        if (written <= 0)
            break;
        data += written;
        remaining -= written;
    }
}

void DiagnosticLog::qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QString line = QStringLiteral("qt.%1 %2").arg(QString::fromLatin1(severityName(type)), message);
    if (context.file && *context.file) {
        line += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line);
    }

    DiagnosticLog::instance().logLine(line);

    if (DiagnosticLog::instance().previousQtMessageHandler_)
        DiagnosticLog::instance().previousQtMessageHandler_(type, context, message);

    if (type == QtFatalMsg)
        abort();
}

void DiagnosticLog::terminateHandler()
{
    DiagnosticLog::instance().logLine(QStringLiteral("std::terminate called"));

    if (DiagnosticLog::instance().previousTerminateHandler_)
        DiagnosticLog::instance().previousTerminateHandler_();

    abort();
}

#if !defined(Q_OS_WIN)
void DiagnosticLog::fatalSignalHandler(int signalNumber)
{
    const int fd = signalFd_.load();
    if (fd >= 0) {
        char buffer[160];
        size_t pos = 0;
        appendLiteral(buffer, pos, "\n[FATAL] signal ");
        appendInt(buffer, pos, signalNumber);
        appendLiteral(buffer, pos, " received; last heartbeat above\n");
        diagnostic_write(fd, buffer, static_cast<unsigned int>(pos));
    }

    ::kill(::getpid(), signalNumber);
}

void DiagnosticLog::installSignalHandlers()
{
    // A closed peer on a socket/pipe must be reported as EPIPE, not allowed to
    // terminate the whole client. macOS does not provide MSG_NOSIGNAL, so keep
    // this process-wide guard in the GUI startup path.
    struct sigaction pipeAction;
    memset(&pipeAction, 0, sizeof(pipeAction));
    pipeAction.sa_handler = SIG_IGN;
    sigemptyset(&pipeAction.sa_mask);
    sigaction(SIGPIPE, &pipeAction, nullptr);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = DiagnosticLog::fatalSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;

    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGBUS,  &action, nullptr);
    sigaction(SIGFPE,  &action, nullptr);
    sigaction(SIGILL,  &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}
#endif
