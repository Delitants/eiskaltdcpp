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
#include <QDebug>

static const unsigned int SHARED_MEM_SIZE = 2048;

QtSingleCoreApplication::QtSingleCoreApplication(int &argc, char **argv, const QString &uniqueKey)
    : QApplication(argc, argv), _isRunning(false), sharedMemory(), messageTimer(nullptr)
{
    sharedMemory.setKey(uniqueKey);

    if (sharedMemory.attach())
        _isRunning = true;
    else {
        _isRunning = false;
        // attach data to shared memory.
        QByteArray byteArray("0"); // default value to note that no message is available.
        byteArray.resize(SHARED_MEM_SIZE);

        if (!sharedMemory.create(byteArray.size()))
        {
            qDebug("Unable to create single instance.");
            return;
        }
        sharedMemory.lock();
        char *to = (char*)sharedMemory.data();
        const char *from = byteArray.data();
        memcpy(to, from, qMin(sharedMemory.size(), byteArray.size()));
        sharedMemory.unlock();
        // start checking for messages of other instances.
        messageTimer = new QTimer(this);
        connect(messageTimer, SIGNAL(timeout()), this, SLOT(checkForMessage()));
        messageTimer->start(2000);
    }
}

QtSingleCoreApplication::~QtSingleCoreApplication(){
    releaseSingleInstance();
}

bool QtSingleCoreApplication::isRunning()
{
    return _isRunning;
}


bool QtSingleCoreApplication::sendMessage(QString message)
{
    if (!_isRunning || !sharedMemory.isAttached())
        return false;

    if (message.length() > sharedMemory.size() - 2)//two reserved bytes
        message = message.left(sharedMemory.size()-2);

    QByteArray byteArray("1");
    byteArray.append(message.toUtf8());
    byteArray.append('\0');

    if (!sharedMemory.lock())
        return false;

    char *to = (char*)sharedMemory.data();
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

    byteArray.remove(0, 1);

    QString message = QString::fromUtf8(byteArray.constData());

    emit messageReceived(message);

    // remove message from shared memory.
    byteArray = "0";
    sharedMemory.lock();

    char *to = (char*)sharedMemory.data();
    const char *from = byteArray.data();

    memcpy(to, from, qMin(sharedMemory.size(), byteArray.size()));

    sharedMemory.unlock();
}
