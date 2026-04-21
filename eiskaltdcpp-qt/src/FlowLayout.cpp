/****************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial Usage
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights.  These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: https://www.gnu.org/copyleft/gpl.html.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
** $QT_END_LICENSE$
**
****************************************************************************/

#include "FlowLayout.h"

FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing), m_rowAlignment(Qt::AlignLeft | Qt::AlignTop)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing), m_rowAlignment(Qt::AlignLeft | Qt::AlignTop)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout(){
    QLayoutItem *item;
    while ((item = FlowLayout::takeAt(0)))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item){
    itemList.append(item);
}

int FlowLayout::horizontalSpacing() const {
    if (m_hSpace >= 0) {
        return m_hSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
    }
}

int FlowLayout::verticalSpacing() const {
    if (m_vSpace >= 0) {
        return m_vSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
    }
}

int FlowLayout::count() const {
    return itemList.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const {
    return itemList.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index) {
    if (index >= 0 && index < itemList.size())
        return itemList.takeAt(index);
    else
        return nullptr;
}

void FlowLayout::setRowAlignment(Qt::Alignment alignment) {
    m_rowAlignment = alignment;
    invalidate();
}

Qt::Alignment FlowLayout::rowAlignment() const {
    return m_rowAlignment;
}

Qt::Orientations FlowLayout::expandingDirections() const {
    return Qt::Orientations();
}

bool FlowLayout::hasHeightForWidth() const {
    return true;
}

int FlowLayout::heightForWidth(int width) const {
    int height = doLayout(QRect(0, 0, width, 0), true);
    return height;
}

void FlowLayout::setGeometry(const QRect &rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const {
    return minimumSize();
}

bool FlowLayout::moveLeft(QLayoutItem *item){
    const int index = itemList.indexOf(item);

    if (index <= 0)
        return false;

    itemList.takeAt(index);
    itemList.insert(index-1, item);

    doLayout(geometry(), false);

    return true;
}

bool FlowLayout::moveRight(QLayoutItem *item){
    const int index = itemList.indexOf(item);

    if ((index < 0) || (index >= itemList.count()-1))
        return false;

    itemList.takeAt(index);
    itemList.insert(index+1, item);

    doLayout(geometry(), false);

    return true;
}

QSize FlowLayout::minimumSize() const {
    QSize size;
    for (const auto &item : itemList)
        size = size.expandedTo(item->minimumSize());

    const QMargins cm = contentsMargins();
    size += QSize(cm.left() + cm.right(), cm.top() + cm.bottom());
    return size;
}

void FlowLayout::place(QWidget *on, QWidget *what){
    if (!(on && what))
        return;

    QLayoutItem *i_on = nullptr;
    QLayoutItem *i_what = nullptr;

    for (const auto &item : itemList) {
        if (item->widget() == on)
            i_on = item;
        else if (item->widget() == what)
            i_what = item;

        if (i_on && i_what)
            break;
    }

    if (!(i_on && i_what))
        return;

    int index_on = itemList.indexOf(i_on);
    int index_what = itemList.indexOf(i_what);

    itemList.takeAt(index_what);
    itemList.insert(index_on, i_what);

    doLayout(geometry(), false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const {
    const QMargins cm = contentsMargins();
    QRect effectiveRect = rect.adjusted(cm.left(), cm.top(), -cm.right(), -cm.bottom());
    struct Row {
        QList<QLayoutItem *> items;
        int width = 0;
        int height = 0;
    };

    QList<Row> rows;
    Row currentRow;

    const int availableWidth = qMax(0, effectiveRect.width());

    for (const auto &item : itemList) {
        QWidget *wid = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1)
            spaceX = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);

        const int itemWidth = item->sizeHint().width();
        const int projectedWidth = currentRow.items.isEmpty() ? itemWidth : currentRow.width + spaceX + itemWidth;

        if (!currentRow.items.isEmpty() && projectedWidth > availableWidth) {
            rows.append(currentRow);
            currentRow = Row();
        }

        if (currentRow.items.isEmpty()) {
            currentRow.width = itemWidth;
        } else {
            currentRow.width += spaceX + itemWidth;
        }

        currentRow.height = qMax(currentRow.height, item->sizeHint().height());
        currentRow.items.append(item);
    }

    if (!currentRow.items.isEmpty())
        rows.append(currentRow);

    int y = effectiveRect.y();
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const Row &row = rows.at(rowIndex);

        int x = effectiveRect.x();
        if (m_rowAlignment.testFlag(Qt::AlignHCenter))
            x += qMax(0, (availableWidth - row.width) / 2);
        else if (m_rowAlignment.testFlag(Qt::AlignRight))
            x += qMax(0, availableWidth - row.width);

        for (int itemIndex = 0; itemIndex < row.items.size(); ++itemIndex) {
            QLayoutItem *item = row.items.at(itemIndex);
            QWidget *wid = item->widget();

            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

            if (itemIndex + 1 < row.items.size()) {
                int spaceX = horizontalSpacing();
                if (spaceX == -1)
                    spaceX = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
                x += item->sizeHint().width() + spaceX;
            }
        }

        if (rowIndex + 1 < rows.size()) {
            QWidget *wid = row.items.first()->widget();
            int spaceY = verticalSpacing();
            if (spaceY == -1)
                spaceY = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
            y += row.height + spaceY;
        } else {
            y += row.height;
        }
    }

    return y - rect.y() + cm.bottom();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const {
    QObject *parent = this->parent();
    if (!parent) {
        return -1;
    } else if (parent->isWidgetType()) {
        QWidget *pw = static_cast<QWidget *>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    } else {
        return static_cast<QLayout *>(parent)->spacing();
    }
}
