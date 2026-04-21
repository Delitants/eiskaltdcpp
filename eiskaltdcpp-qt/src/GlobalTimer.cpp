/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "GlobalTimer.h"

#include <QTimer>

GlobalTimer::GlobalTimer(dcpp::DCContext& ctx)
    : QObject(nullptr),
      QtContextAware(ctx),
      timer(new QTimer()),
      tickCount(0),
      stopped_(false)
{
    timer->setInterval(1000);
    connect(timer.get(), &QTimer::timeout, this, &GlobalTimer::slotTick);
    timer->start();
}

GlobalTimer::~GlobalTimer() {
    stop();
}

void GlobalTimer::stop() {
    if (stopped_)
        return;

    stopped_ = true;

    if (timer) {
        timer->stop();
        QObject::disconnect(timer.get(), nullptr, this, nullptr);
    }
}

void GlobalTimer::slotTick() {
    if (stopped_)
        return;

    ++tickCount;
    emit second();

    if ((tickCount % 60) == 0)
        emit minute();
}

quint64 GlobalTimer::getTicks() const {
    return tickCount;
}
