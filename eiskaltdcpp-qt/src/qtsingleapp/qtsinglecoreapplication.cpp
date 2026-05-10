/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/


#include "qtsinglecoreapplication.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <signal.h>
#endif
#if defined(Q_OS_MAC)
#include <libproc.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

static const unsigned int SHARED_MEM_SIZE = 2048;

static qint64 singleInstancePidFromData(const QByteArray &byteArray)
{
    const QList<QByteArray> parts = byteArray.split('\n');
    if (parts.size() < 2)
        return -1;

    bool ok = false;
    const qint64 pid = parts.at(1).trimmed().toLongLong(&ok);

    return ok ? pid : -1;
}

static QByteArray singleInstancePayload(const char messageFlag, const qint64 pid, const QByteArray &message = QByteArray())
{
    QByteArray byteArray;
    byteArray.append(messageFlag);
    byteArray.append('\n');
    byteArray.append(QByteArray::number(pid));
    byteArray.append('\n');
    byteArray.append(QCoreApplication::applicationFilePath().toUtf8());
    byteArray.append('\n');
    byteArray.append(message);
    byteArray.append('\0');
    byteArray.resize(SHARED_MEM_SIZE);

    return byteArray;
}

static QString canonicalProcessPath(const qint64 pid)
{
#if defined(Q_OS_MAC)
    char pathBuffer[PROC_PIDPATHINFO_MAXSIZE];
    const int pathLength = proc_pidpath(static_cast<int>(pid), pathBuffer, sizeof(pathBuffer));
    if (pathLength <= 0)
        return QString();

    return QFileInfo(QString::fromLocal8Bit(pathBuffer, pathLength)).canonicalFilePath();
#elif defined(Q_OS_LINUX)
    char pathBuffer[4096];
    const QByteArray procPath = QByteArrayLiteral("/proc/") + QByteArray::number(pid) + QByteArrayLiteral("/exe");
    const ssize_t pathLength = readlink(procPath.constData(), pathBuffer, sizeof(pathBuffer) - 1);
    if (pathLength <= 0)
        return QString();

    pathBuffer[pathLength] = '\0';
    return QFileInfo(QString::fromLocal8Bit(pathBuffer)).canonicalFilePath();
#else
    Q_UNUSED(pid);
    return QString();
#endif
}

QtSingleCoreApplication::QtSingleCoreApplication(int &argc, char **argv, const QString &uniqueKey)
    : QApplication(argc, argv),
      _isRunning(false),
      _instanceOwnerPid(-1),
      sharedMemory(),
      messageTimer(nullptr)
{
    sharedMemory.setKey(uniqueKey);

    if (sharedMemory.attach()) {
        if (attachedInstanceIsAlive()) {
            _isRunning = true;
            return;
        }

        // Older builds could leave stale shared-memory markers after forced
        // termination. Detaching lets Qt remove orphaned segments on Unix.
        sharedMemory.detach();
    }

    _isRunning = false;
    if (!createSingleInstanceMemory())
        qDebug("Unable to create single instance.");
}

QtSingleCoreApplication::~QtSingleCoreApplication(){
    releaseSingleInstance();
}

bool QtSingleCoreApplication::isRunning()
{
    return _isRunning;
}

qint64 QtSingleCoreApplication::instanceOwnerPid() const
{
    return _instanceOwnerPid;
}

QString QtSingleCoreApplication::instanceOwnerPath() const
{
    return _instanceOwnerPath;
}


bool QtSingleCoreApplication::sendMessage(QString message)
{
    if (!_isRunning || !sharedMemory.isAttached())
        return false;

    if (message.length() > sharedMemory.size() - 32)
        message = message.left(sharedMemory.size() - 32);

    if (!sharedMemory.lock())
        return false;

    QByteArray current = QByteArray(static_cast<const char*>(sharedMemory.constData()), sharedMemory.size());
    qint64 ownerPid = singleInstancePidFromData(current);
    if (ownerPid <= 0)
        ownerPid = QCoreApplication::applicationPid();

    const QByteArray byteArray = singleInstancePayload('1', ownerPid, message.toUtf8());
    char *to = static_cast<char*>(sharedMemory.data());
    const char *from = byteArray.data();

    memcpy(to, from, qMin(sharedMemory.size(), byteArray.size()));

    sharedMemory.unlock();

    return true;
}

void QtSingleCoreApplication::releaseSingleInstance()
{
    if (messageTimer)
        messageTimer->stop();

    if (sharedMemory.isAttached())
        sharedMemory.detach();

    _isRunning = false;
}

bool QtSingleCoreApplication::attachedInstanceIsAlive()
{
    _instanceOwnerPid = -1;
    _instanceOwnerPath.clear();

    if (!sharedMemory.isAttached())
        return false;

    if (!sharedMemory.lock())
        return true;

    const QByteArray byteArray = QByteArray(static_cast<const char*>(sharedMemory.constData()), sharedMemory.size());
    sharedMemory.unlock();

    const qint64 pid = singleInstancePidFromData(byteArray);
    if (pid <= 0)
        return false;

    _instanceOwnerPid = pid;
    _instanceOwnerPath = canonicalProcessPath(pid);

#if defined(Q_OS_UNIX)
    errno = 0;
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM)
        return false;

    if (_instanceOwnerPath.isEmpty())
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
        return false;
#else
        return true;
#endif

    const QString currentPath = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    return !currentPath.isEmpty() && _instanceOwnerPath == currentPath;
#else
    return true;
#endif
}

bool QtSingleCoreApplication::createSingleInstanceMemory()
{
    QByteArray byteArray = singleInstancePayload('0', QCoreApplication::applicationPid());

    if (!sharedMemory.create(byteArray.size()))
        return false;

    sharedMemory.lock();
    char *to = static_cast<char*>(sharedMemory.data());
    const char *from = byteArray.data();
    memcpy(to, from, qMin(sharedMemory.size(), byteArray.size()));
    sharedMemory.unlock();

    // Start checking for messages from secondary instances.
    messageTimer = new QTimer(this);
    connect(messageTimer, SIGNAL(timeout()), this, SLOT(checkForMessage()));
    messageTimer->start(2000);

    return true;
}

static QString singleInstanceMessageFromData(const QByteArray &byteArray)
{
    const QList<QByteArray> parts = byteArray.split('\n');
    if (parts.size() >= 3)
        return parts.size() >= 4
                ? QString::fromUtf8(byteArray.mid(parts.at(0).size() + parts.at(1).size() + parts.at(2).size() + 3).constData())
                : QString::fromUtf8(byteArray.mid(parts.at(0).size() + parts.at(1).size() + 2).constData());

    QByteArray legacy = byteArray;
    legacy.remove(0, 1);
    return QString::fromUtf8(legacy.constData());
}


void QtSingleCoreApplication::checkForMessage()
{
    if (!sharedMemory.isAttached())
        return;

    if (!sharedMemory.lock())
        return;

    QByteArray byteArray = QByteArray((char*)sharedMemory.constData(), sharedMemory.size());

    sharedMemory.unlock();

    if (byteArray.left(1) != "1")
        return;

    const qint64 ownerPid = singleInstancePidFromData(byteArray);
    QString message = singleInstanceMessageFromData(byteArray);

    emit messageReceived(message);

    // remove message from shared memory.
    byteArray = singleInstancePayload('0', ownerPid > 0 ? ownerPid : QCoreApplication::applicationPid());
    sharedMemory.lock();

    char *to = static_cast<char*>(sharedMemory.data());
    const char *from = byteArray.data();

    memcpy(to, from, qMin(sharedMemory.size(), byteArray.size()));

    sharedMemory.unlock();
}
