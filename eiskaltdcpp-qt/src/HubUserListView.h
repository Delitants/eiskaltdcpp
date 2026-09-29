#pragma once

#include <QPainter>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include "ViewLayout.h"

class HubUserListView : public QTreeView {
public:
    explicit HubUserListView(QWidget *parent = nullptr) : QTreeView(parent) {}

    bool restoreHeaderState(const QByteArray &state)
    {
        if (!view_layout::restoreHeader(header(), state))
            return false;

        int visibleWidth = 0;
        for (int column = 0; column < header()->count(); ++column)
            visibleWidth += header()->sectionSize(column);
        if (header()->length() != visibleWidth) {
            // Some legacy states retain a nonzero span for a hidden section.
            // Re-hide it through Qt's API, preserving its remembered width.
            const QSignalBlocker blocker(header());
            for (int column = 0; column < header()->count(); ++column) {
                if (header()->isSectionHidden(column)) {
                    header()->showSection(column);
                    header()->hideSection(column);
                }
            }
        }
        doItemsLayout();
        return true;
    }

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override
    {
        QPalette::ColorGroup group = QPalette::Disabled;
        if (option.state & QStyle::State_Enabled)
            group = option.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive;

        // Hub users are flat (including the filter proxy). Paint the entire row,
        // since QTreeView only fills visible sections, not the spare viewport.
        QPalette::ColorRole role = alternatingRowColors() && (index.row() & 1)
            ? QPalette::AlternateBase : QPalette::Base;
        if (selectionBehavior() == QAbstractItemView::SelectRows && selectionModel()
            && selectionModel()->isRowSelected(index.row(), index.parent()))
            role = QPalette::Highlight;

        painter->fillRect(QRect(0, option.rect.y(), viewport()->width(), option.rect.height()),
                          option.palette.brush(group, role));
        QTreeView::drawRow(painter, option, index);
    }
};
