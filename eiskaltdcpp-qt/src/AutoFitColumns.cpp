#include "AutoFitColumns.h"
#include <QEvent>
#include <QHeaderView>
#include <QScopedValueRollback>
#include <QTreeView>

AutoFitColumns::AutoFitColumns(QTreeView *table) : QObject(table), view(table), pending(this)
{
    auto *header = view->header();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    for (int column = 0; column < header->count(); ++column) shrinkColumns.insert(column);
    pending.setSingleShot(true);
    pending.setInterval(100);
    connect(&pending, &QTimer::timeout, this, &AutoFitColumns::fit);
    connect(header, &QHeaderView::sectionResized, this, [this](int column, int oldSize, int newSize) {
        if (internalChange || modelChanges || oldSize <= 0 || newSize <= 0) return;
        manualWidths[column] = newSize;
        shrinkColumns.remove(column);
        emit layoutChanged();
    });
    connect(header, &QHeaderView::sectionMoved, this, [this] {
        if (!internalChange && !modelChanges) emit layoutChanged();
    });
    connect(header, &QHeaderView::sortIndicatorChanged, this, [this] {
        scheduleFit();
        if (!internalChange && !modelChanges) emit layoutChanged();
    });
    connect(header, &QHeaderView::sectionHandleDoubleClicked, this, &AutoFitColumns::fitToContents);
    connect(header, &QHeaderView::sectionCountChanged, this, &AutoFitColumns::scheduleFit);
    connect(header, &QHeaderView::geometriesChanged, this, &AutoFitColumns::scheduleFit);
    connect(view, &QTreeView::expanded, this, &AutoFitColumns::scheduleFit);
    if (auto *model = view->model()) {
        connect(model, &QAbstractItemModel::dataChanged, this, &AutoFitColumns::scheduleFit);
        connect(model, &QAbstractItemModel::layoutChanged, this, &AutoFitColumns::scheduleFit);
        connect(model, &QAbstractItemModel::headerDataChanged, this, &AutoFitColumns::scheduleFit);
        const auto beginChange = [this] { ++modelChanges; };
        const auto endChange = [this] { --modelChanges; scheduleFit(); };
        connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, beginChange);
        connect(model, &QAbstractItemModel::rowsInserted, this, endChange);
        connect(model, &QAbstractItemModel::columnsAboutToBeInserted, this, beginChange);
        connect(model, &QAbstractItemModel::columnsInserted, this, endChange);
        connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, this, beginChange);
        connect(model, &QAbstractItemModel::columnsRemoved, this, endChange);
        connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
            ++modelChanges;
            beforeReset = view->header()->saveState();
        });
        connect(model, &QAbstractItemModel::modelReset, this, [this] {
            {
                const QScopedValueRollback<bool> guard(internalChange, true);
                view->header()->restoreState(beforeReset);
            }
            --modelChanges;
            scheduleFit();
        });
    }
    view->installEventFilter(this);
    header->installEventFilter(this);
    scheduleFit();
}

QVariantMap AutoFitColumns::saveState() const
{
    QVariantMap manual;
    for (auto it = manualWidths.cbegin(); it != manualWidths.cend(); ++it)
        manual.insert(QString::number(it.key()), it.value());
    return {{QStringLiteral("version"), 1}, {QStringLiteral("header"), view->header()->saveState()},
            {QStringLiteral("manual"), manual}};
}

bool AutoFitColumns::restoreState(const QVariantMap &state, const QByteArray &legacyHeader)
{
    auto *header = view->header();
    const bool current = state.value(QStringLiteral("version")).toInt() == 1;
    const QByteArray bytes = current ? state.value(QStringLiteral("header")).toByteArray() : legacyHeader;
    {
        const QScopedValueRollback<bool> guard(internalChange, true);
        if (bytes.isEmpty() || !header->restoreState(bytes)) return false;
        QMap<int, int> legacyWidths;
        if (!current) {
            // Settle the restored sizes without stretching them to this window.
            header->resizeSections(QHeaderView::Interactive);
            // Disabling stretch restores Qt's narrower pre-stretch width.
            // Capture what the user actually saw before changing that policy.
            for (int column = 0; column < header->count(); ++column)
                if (!header->isSectionHidden(column)) legacyWidths[column] = header->sectionSize(column);
        }
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setStretchLastSection(false);
        manualWidths.clear();
        shrinkColumns.clear();
        if (current) {
            const auto manual = state.value(QStringLiteral("manual")).toMap();
            for (auto it = manual.cbegin(); it != manual.cend(); ++it) {
                bool valid = false;
                const int column = it.key().toInt(&valid);
                const int width = it.value().toInt();
                if (valid && column >= 0 && width > 0)
                    manualWidths[column] = qBound(header->minimumSectionSize(), width, header->maximumSectionSize());
            }
        } else {
            // Older layouts did not distinguish automatic widths from user choices.
            // Preserve every saved width until the user explicitly opts into fitting.
            for (int column = 0; column < header->count(); ++column) {
                const bool hidden = header->isSectionHidden(column);
                if (hidden) header->showSection(column);
                manualWidths[column] = legacyWidths.value(column, header->sectionSize(column));
                header->resizeSection(column, manualWidths.value(column));
                if (hidden) header->hideSection(column);
            }
        }
    }
    scheduleFit();
    return true;
}

void AutoFitColumns::fitToContents(int column)
{
    if (column < 0) {
        manualWidths.clear();
        for (int index = 0; index < view->header()->count(); ++index) shrinkColumns.insert(index);
    } else if (column < view->header()->count()) {
        manualWidths.remove(column);
        shrinkColumns.insert(column);
    }
    fit();
    emit layoutChanged();
}

void AutoFitColumns::scheduleFit()
{
    if (!internalChange && !pending.isActive()) pending.start();
}

void AutoFitColumns::fit()
{
    pending.stop();
    if (!view->model() || modelChanges) return;
    bool changed = false;
    {
        const QScopedValueRollback<bool> guard(internalChange, true);
        auto *header = view->header();
        // Use the public base API to dispatch QTreeView's delegate-aware sizing,
        // including icons, indentation and its bounded row sampling.
        const auto *itemView = static_cast<QAbstractItemView *>(view);
        for (int column = 0; column < header->count(); ++column) {
            if (header->isSectionHidden(column)) continue;
            const int oldWidth = header->sectionSize(column);
            int width = manualWidths.value(column, -1);
            if (width < 0) {
                width = qMax(header->sectionSizeHint(column), itemView->sizeHintForColumn(column)) + 12;
                if (!shrinkColumns.contains(column)) width = qMax(oldWidth, width);
            }
            if (width != oldWidth) {
                header->resizeSection(column, width);
                changed = true;
            }
            shrinkColumns.remove(column);
        }
    }
    if (changed) emit layoutChanged();
}

bool AutoFitColumns::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::LanguageChange || event->type() == QEvent::Show)
        scheduleFit();
    return QObject::eventFilter(object, event);
}
