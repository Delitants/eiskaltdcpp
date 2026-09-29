#pragma once

#include <QPainter>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QScrollBar>

class DownloadQueueTreeView : public QTreeView {
public:
    explicit DownloadQueueTreeView(QWidget *parent = nullptr) : QTreeView(parent)
    {
        setUniformRowHeights(true);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override
    {
        // QTreeView sets Alternate only inside its base drawRow implementation.
        // Uniform heights and pixel scrolling give the visible-row ordinal,
        // including expanded children, without walking a potentially large queue.
        const int row = (option.rect.top() + verticalScrollBar()->value()) /
                        qMax(1, option.rect.height());
        auto role = alternatingRowColors() && (row & 1)
            ? QPalette::AlternateBase : QPalette::Base;
        if (selectionModel() && selectionModel()->isRowSelected(index.row(), index.parent()))
            role = QPalette::Highlight;
        const auto group = !(option.state & QStyle::State_Enabled) ? QPalette::Disabled :
            (option.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive);
        // Unlike a flat list, alternation follows visible rows across expanded groups.
        painter->fillRect(QRect(0, option.rect.y(), viewport()->width(), option.rect.height()),
                          option.palette.brush(group, role));
        QTreeView::drawRow(painter, option, index);
    }
};
