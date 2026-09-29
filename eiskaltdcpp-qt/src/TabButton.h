/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QPushButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QMouseEvent>
#include <QPointer>

class QResizeEvent;
class QPaintEvent;
class QLabel;
class QEvent;
class QToolButton;

class TabButton : public QPushButton
{
Q_OBJECT
public:
    explicit TabButton(QWidget *parent = nullptr);

    QSize sizeHint() const;
    QSize minimumSizeHint() const;
    void setWidgetIcon(const QIcon &icon);
    void resetGeometry();
    QRect titleRect() const;
    QString elidedTitle() const;
    int normalWidth() const;
    int normalHeight() const;

protected:
    virtual void resizeEvent(QResizeEvent *);
    virtual void dragEnterEvent(QDragEnterEvent *);
    virtual void dragMoveEvent(QDragMoveEvent *);
    virtual void dropEvent(QDropEvent *);
    virtual void mousePressEvent(QMouseEvent *e);
    virtual void mouseReleaseEvent(QMouseEvent *e);
    virtual void mouseMoveEvent(QMouseEvent *e);
    virtual void paintEvent(QPaintEvent *e);

signals:
    void closeRequest();
    void dropped(TabButton *source, TabButton *target);

private:
    QPoint dragStartPos;

    static QPointer<TabButton> dragSourceButton;
    void positionChildren();

    QToolButton *closeButton;
    QLabel *px_label;
    bool showClose = true;
    bool isLeftBtnHold;
};
