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
#include "QtContextAware.h"
#include "QtContext.h"

#include <QResizeEvent>
#include <QLabel>
#include <QEvent>
#include <QMouseEvent>
#include <QStyleOptionButton>
#include <QApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QStyleOption>
#include <QLinearGradient>
#include <QBrush>
#include <QPen>
#include <QPointF>
#include <QMimeData>
#include <QDrag>
#include <QPalette>

#include "WulforUtil.h"
#include "WulforSettings.h"

#include <QDataStream>

static const int margin         = 5;
static const int LABELWIDTH     = 18;
static const int CLOSEPXWIDTH   = 14;
static const int PXWIDTH        = 16;
static const int CLOSE_RIGHT_MARGIN = 58;

TabButton *TabButton::dragSourceButton = nullptr;

TabButton::TabButton(QWidget *parent) :
    QPushButton(parent), isLeftBtnHold(false)
{
    setFlat(true);
    setCheckable(true);
    setAutoExclusive(true);
    setAutoDefault(false);
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setContentsMargins(0, 0, 0, 0);
    setFixedHeight(26);
    setContentsMargins(0, 0, 0, 0);

    parentHeight = QPushButton::sizeHint().height();

    label = new QLabel(this);
    label->setPixmap(style()->standardIcon(QStyle::SP_TitleBarCloseButton).pixmap(CLOSEPXWIDTH, CLOSEPXWIDTH));
    label->setFixedSize(QSize(CLOSEPXWIDTH, CLOSEPXWIDTH));
    label->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);

    px_label = new QLabel(this);
    px_label->setFixedSize(QSize(PXWIDTH, PXWIDTH));
    px_label->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);

    installEventFilter(this);
    label->installEventFilter(this);

    updateGeometry();
}

void TabButton::resizeEvent(QResizeEvent *e){
    e->accept();

    updateGeometry();
}

bool TabButton::eventFilter(QObject *obj, QEvent *e){
    bool ret = QPushButton::eventFilter(obj, e);

    if (e->type() == QEvent::MouseButtonRelease){
        QMouseEvent *m_e = reinterpret_cast<QMouseEvent*>(e);

        if ((m_e->button() == Qt::MiddleButton) || (childAt(m_e->pos()) == static_cast<QWidget*>(label)))
            emit closeRequest();
    }

    return ret;
}

void TabButton::dragEnterEvent(QDragEnterEvent *event){
    if (event->mimeData()->hasFormat("application/x-eiskalt-tab") &&
        dragSourceButton &&
        dragSourceButton != this) {
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void TabButton::dragMoveEvent(QDragMoveEvent *event){
    if (event->mimeData()->hasFormat("application/x-eiskalt-tab") &&
        dragSourceButton &&
        dragSourceButton != this) {
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void TabButton::dropEvent(QDropEvent *e){
    if (e->mimeData()->hasFormat("application/x-eiskalt-tab") &&
        dragSourceButton &&
        dragSourceButton != this) {
        emit dropped(dragSourceButton, this);
        e->acceptProposedAction();
        return;
    }

    e->ignore();
}

void TabButton::mousePressEvent(QMouseEvent *e){
    QPushButton::mousePressEvent(e);

    if (e->button() == Qt::LeftButton){
        dragStartPos = e->pos();
        emit clicked();
        isLeftBtnHold = true;
    }
}

void TabButton::mouseMoveEvent(QMouseEvent *e){
    QPushButton::mouseMoveEvent(e);

    if (!isLeftBtnHold)
        return;

    if (!(e->buttons() & Qt::LeftButton))
        return;

    if ((e->pos() - dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;

    auto *mime = new QMimeData();
    mime->setData("application/x-eiskalt-tab", QByteArray("tab"));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    dragSourceButton = this;
    drag->exec(Qt::MoveAction);
    dragSourceButton = nullptr;
}

void TabButton::mouseReleaseEvent(QMouseEvent *e){
    QPushButton::mouseReleaseEvent(e);

    isLeftBtnHold = false;
}

void TabButton::paintEvent(QPaintEvent *e){
    QPushButton::paintEvent(e);
}

QSize TabButton::sizeHint() const {
    ensurePolished();
    return QSize(normalWidth(), 26);
}

QSize TabButton::minimumSizeHint() const {
    return sizeHint();
}

int TabButton::normalWidth() const {
    QFontMetricsF metrics(qApp->font());
    const bool showClose = qtCtx()->settings()->getBool(WB_APP_TBAR_SHOW_CL_BTNS);
    const int closeWidth = showClose ? (CLOSEPXWIDTH + CLOSE_RIGHT_MARGIN + 4) : 10;
    return PXWIDTH + closeWidth + qRound(metrics.horizontalAdvance(text())) + margin * 5 + 18;
}

int TabButton::normalHeight() const {
    return 26;
}

void TabButton::setWidgetIcon(const QPixmap &px){
    px_label->setPixmap(px.scaled(PXWIDTH, PXWIDTH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void TabButton::updateStyles() {
    label->setStyleSheet(QStringLiteral("QLabel { margin: 0px; padding: 0px; background: transparent; }"));
    px_label->setStyleSheet(QString("QLabel { margin-right: %1; background: transparent; }").arg(margin * 2));

    const bool showClose = qtCtx()->settings()->getBool(WB_APP_TBAR_SHOW_CL_BTNS);
    const QPalette pal = palette();
    const QColor textColor = pal.color(QPalette::ButtonText);
    const QColor borderColor = pal.color(QPalette::Mid);
    const QColor baseButton = pal.color(QPalette::Button);
    const QColor checkedBg = pal.color(QPalette::Window);
    const bool darkAppearance = checkedBg.lightness() < 128;
    const QColor hoverBg = darkAppearance ? baseButton.lighter(114) : baseButton.darker(102);
    const QColor pressedBg = darkAppearance ? baseButton.darker(118) : baseButton.darker(108);

    QString style = QString(R"(
QPushButton {
    margin: 0px;
    padding-top: 0px;
    padding-bottom: 0px;
    border: 1px solid %1;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    border-bottom-left-radius: 0px;
    border-bottom-right-radius: 0px;
    background: %2;
    color: %3;
    text-align: center;
    min-height: 26px;
    max-height: 26px;
}

QPushButton:hover {
    background: %4;
    border-color: %1;
}

QPushButton:checked {
    background: %5;
    border-color: %1;
    border-bottom-color: %5;
}

QPushButton:pressed {
    background: %6;
}
)").arg(borderColor.name(), baseButton.name(), textColor.name(), hoverBg.name(), checkedBg.name(), pressedBg.name());

    if (showClose) {
        style += QString(R"(
QPushButton {
    padding-left: %1px;
    padding-right: %2px;
}
QPushButton:checked {
    padding-left: %1px;
    padding-right: %2px;
}
)").arg(PXWIDTH + 10).arg(CLOSEPXWIDTH + CLOSE_RIGHT_MARGIN + 2);
    } else {
        style += QString(R"(
QPushButton {
    padding-left: %1px;
    padding-right: %2px;
}
QPushButton:checked {
    padding-left: %1px;
    padding-right: %2px;
}
)").arg(PXWIDTH + 10).arg(10);
    }

    setStyleSheet(style);
}

void TabButton::updateGeometry() {
    setMinimumWidth(normalWidth());
    setMaximumWidth(normalWidth());
    setMinimumHeight(normalHeight());
    setMaximumHeight(normalHeight());

    if (qtCtx()->settings()->getBool(WB_APP_TBAR_SHOW_CL_BTNS)) {
        if (!label->isVisible())
            label->show();

        // Position close button at the right edge with proper vertical centering
        const int x = width() - CLOSEPXWIDTH - CLOSE_RIGHT_MARGIN;
        const int y = qMax(0, (height() - CLOSEPXWIDTH) / 2);
        label->setGeometry(x, y, CLOSEPXWIDTH, CLOSEPXWIDTH);
    } else {
        label->hide();
    }

    px_label->setGeometry(8,
                          (height() - PXWIDTH) / 2,
                          PXWIDTH,
                          PXWIDTH);

    updateStyles();
}
