#pragma once

#include <QCryptographicHash>
#include <QFontMetrics>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QTreeView>
#include <QVariant>
#include <functional>

namespace view_layout {
inline void markEdited(QWidget* owner) {
    // GUI-thread sequence shared across views that persist the same layout key.
    static quint64 sequence = 0;
    owner->setProperty("_layoutEditSequence", QVariant::fromValue(++sequence));
}

inline quint64 editSequence(QWidget* owner) {
    return owner->property("_layoutEditSequence").toULongLong();
}

inline void watchEdits(QWidget* owner, QHeaderView* header,
                       std::function<void(quint64)> onEdit = {}) {
    // Attach after initial restore so opening an untouched tab is not an edit.
    const auto changed = [owner, onEdit] {
        markEdited(owner);
        if (onEdit) onEdit(editSequence(owner));
    };
    QObject::connect(header, &QHeaderView::sectionResized, owner, changed);
    QObject::connect(header, &QHeaderView::sectionMoved, owner, changed);
    QObject::connect(header, &QHeaderView::sectionCountChanged, owner, changed);
    QObject::connect(header, &QHeaderView::sortIndicatorChanged, owner, changed);
}

inline bool canSaveKey(QObject* store, const QString& key, quint64 revision) {
    // Context-owned, non-persistent precedence survives closing/switching views.
    auto revisions = store->property("_layoutSavedSequences").toMap();
    if (revision < revisions.value(key).toULongLong()) return false;
    revisions.insert(key, QVariant::fromValue(revision));
    store->setProperty("_layoutSavedSequences", revisions);
    return true;
}

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
