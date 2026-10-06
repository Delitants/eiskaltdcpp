/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/
/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 */

#pragma once

#include <QObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <functional>

#include "dcpp/stdinc.h"
#include "dcpp/NonCopyable.h"

#include "QtContextAware.h"

namespace updates { class ApplicationQuitCoordinator; }

class SearchBlacklist:
        public QObject,
        public QtContextAware
{
    Q_OBJECT

public:
    enum Argument {
        NAME,
        TTH
    };

    explicit SearchBlacklist(dcpp::DCContext& ctx);
    ~SearchBlacklist() override;

    /// Access through QtContext — NOT a singleton.
    /// Returns nullptr if no QtContext is active or blacklist not yet created.

    bool ok(const QString &exp, Argument type);
    QList<QString> getList(Argument arg) const { return (list[arg]); }
    void setList(Argument arg, const QList<QString> &l) { if (!quitPrepared) list[arg] = l; }
    // Close owner-thread mutations without performing physical I/O.
    [[nodiscard]] bool freezeForQuitChecked();
    [[nodiscard]] bool saveForQuitChecked();

private:
    friend class updates::ApplicationQuitCoordinator;
    std::function<bool()> quitSaveOperation() const;
    friend class QtContext;  // QtContext owns and constructs us

    void loadLists();
    void saveLists();

    QList<QString> list[2];
    bool quitPrepared = false, quitSaved = false;
};
