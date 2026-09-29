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


#include "TabButton.h"
#include "TabNavigation.h"
#include "AppIconTheme.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "WulforSettings.h"

#include <QApplication>
#include <QDrag>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QStyle>
#include <QToolButton>

static const int PXWIDTH = 16;
QPointer<TabButton> TabButton::dragSourceButton;

TabButton::TabButton(QWidget *parent) : QPushButton(parent), isLeftBtnHold(false)
{
    setFlat(true);
    setCheckable(true);
    setAutoExclusive(true);
    setAutoDefault(false);
    setAcceptDrops(true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumWidth(80);
    setMaximumWidth(tab_navigation::MaximumWidth);
    closeButton = tab_navigation::makeCloseButton(this, tr("Close"));
    closeButton->setCursor(Qt::ArrowCursor);
    connect(closeButton, &QToolButton::clicked, this, &TabButton::closeRequest);
    px_label = new QLabel(this);
    px_label->setObjectName(QStringLiteral("tabWidgetIcon"));
    px_label->setFixedSize(PXWIDTH, PXWIDTH);
    px_label->setAlignment(Qt::AlignCenter);
    px_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    resetGeometry();
}

void TabButton::resizeEvent(QResizeEvent *event)
{
    QPushButton::resizeEvent(event);
    positionChildren();
}

void TabButton::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-eiskalt-tab") && dragSourceButton && dragSourceButton != this)
        event->acceptProposedAction();
    else
        event->ignore();
}

void TabButton::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-eiskalt-tab") && dragSourceButton && dragSourceButton != this)
        event->acceptProposedAction();
    else
        event->ignore();
}

void TabButton::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-eiskalt-tab") && dragSourceButton && dragSourceButton != this) {
        emit dropped(dragSourceButton, this);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TabButton::mousePressEvent(QMouseEvent *event)
{
    QPushButton::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        dragStartPos = event->pos();
        isLeftBtnHold = true;
    }
}

void TabButton::mouseMoveEvent(QMouseEvent *event)
{
    QPushButton::mouseMoveEvent(event);
    if (!isLeftBtnHold || !(event->buttons() & Qt::LeftButton) ||
        (event->pos() - dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;
    isLeftBtnHold = false;
    setDown(false);
    const QPointer<TabButton> alive(this);
    click();
    if (!alive)
        return;
    auto *mime = new QMimeData();
    mime->setData("application/x-eiskalt-tab", QByteArray("tab"));
    QPointer<QDrag> drag = new QDrag(this);
    drag->setMimeData(mime);
    dragSourceButton = this;
    drag->exec(Qt::MoveAction);
    dragSourceButton.clear();
    // A tab can be removed while the native drag loop is running.
    if (drag)
        drag->deleteLater();
}

void TabButton::mouseReleaseEvent(QMouseEvent *event)
{
    isLeftBtnHold = false;
    if (event->button() == Qt::MiddleButton) {
        emit closeRequest();
        event->accept();
        return;
    }
    QPushButton::mouseReleaseEvent(event);
}

QRect TabButton::titleRect() const
{
    const int left = tab_navigation::EndInset + PXWIDTH + tab_navigation::ContentGap;
    const int right = tab_navigation::EndInset +
        (showClose ? tab_navigation::CloseSize + tab_navigation::ContentGap : 0);
    return rect().adjusted(left, 3, -right, -3);
}

QString TabButton::elidedTitle() const
{
    QFont titleFont = font();
    titleFont.setBold(isChecked());
    return QFontMetrics(titleFont).elidedText(text(), Qt::ElideRight, qMax(0, titleRect().width()));
}

void TabButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto pal = palette();
    QColor background = pal.color(isChecked() ? QPalette::Base : QPalette::Button);
    if (underMouse() && !isChecked())
        background = background.lightness() < 128 ? background.lighter(115) : background.darker(105);
    painter.setBrush(background);
    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    if (isChecked())
        painter.fillRect(QRect(6, height() - 3, qMax(0, width() - 12), 3), pal.color(QPalette::Highlight));
    if (hasFocus())
        tab_navigation::drawFocusRing(painter, rect(), pal);
    QFont titleFont = font();
    titleFont.setBold(isChecked());
    painter.setFont(titleFont);
    painter.setPen(pal.color(QPalette::ButtonText));
    painter.setClipRect(titleRect());
    painter.drawText(titleRect(), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elidedTitle());
}

QSize TabButton::sizeHint() const
{
    return QSize(qMin(maximumWidth(), normalWidth()), normalHeight());
}

QSize TabButton::minimumSizeHint() const
{
    return QSize(qMin(maximumWidth(), tab_navigation::MinimumWidth), normalHeight());
}

int TabButton::normalWidth() const
{
    QFont titleFont = font();
    titleFont.setBold(true);
    const int content = QFontMetrics(titleFont).horizontalAdvance(text()) +
        2 * tab_navigation::EndInset + PXWIDTH + tab_navigation::ContentGap +
        (showClose ? tab_navigation::CloseSize + tab_navigation::ContentGap : 0);
    return qBound(tab_navigation::MinimumWidth, content, tab_navigation::MaximumWidth);
}

int TabButton::normalHeight() const
{
    return qMax(30, fontMetrics().height() + 10);
}

void TabButton::setWidgetIcon(const QIcon &icon)
{
    px_label->setPixmap(icon.pixmap(QSize(PXWIDTH, PXWIDTH), devicePixelRatioF()));
}

void TabButton::resetGeometry()
{
    if (qtCtx() && qtCtx()->settings())
        showClose = qtCtx()->settings()->getBool(WB_APP_TBAR_SHOW_CL_BTNS);
    closeButton->setVisible(showClose);
    closeButton->setIcon(app_icon_theme::icon(app_icon_theme::activePath(), QStringLiteral("dialog-close"),
        palette(), style()->standardIcon(QStyle::SP_TitleBarCloseButton)));
    setFixedHeight(normalHeight());
    positionChildren();
    QWidget::updateGeometry();
    update();
}

void TabButton::positionChildren()
{
    closeButton->move(width() - tab_navigation::CloseSize - tab_navigation::EndInset,
                      (height() - tab_navigation::CloseSize) / 2);
    px_label->move(tab_navigation::EndInset, (height() - PXWIDTH) / 2);
}
