#pragma once

#include <QCryptographicHash>
#include <QFontMetrics>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTreeView>

namespace view_layout {
inline QString hubStateKey(const QString& hub) {
    return QStringLiteral("hubframe/layout/") + QString::fromLatin1(
        QCryptographicHash::hash(hub.toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline bool restoreHeader(QHeaderView* header, const QByteArray& state) {
    if(state.isEmpty() || !header->restoreState(state))
        return false;
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    return true;
}

inline void setModelPreservingHeader(QTreeView* view, QAbstractItemModel* model) {
    const QByteArray state = view->header()->saveState();
    const QSignalBlocker blocker(view->header());
    view->setModel(model);
    restoreHeader(view->header(), state);
}

inline void fitColumnText(QHeaderView* header, int column,
                          const QFontMetrics& metrics, const QString& text) {
    const int required = metrics.horizontalAdvance(text) + 24;
    if(!header->isSectionHidden(column) && required > header->sectionSize(column))
        header->resizeSection(column, required);
}
}
