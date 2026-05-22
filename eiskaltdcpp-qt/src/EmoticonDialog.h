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

#include <QDialog>
#include <QEvent>
#include <QPixmap>
#include <QPointer>

class QLabel;
class QGridLayout;
class FlowLayout;
class QScrollArea;
class QWidget;

class EmoticonDialog : public QDialog {
    Q_OBJECT

public:
    /** construtor */
    EmoticonDialog(QWidget * parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    /** destructor */
    virtual ~EmoticonDialog();

    QString getEmoticonText() const { return selectedSmile; }
    void preparePopupGeometry(int preferredWidth, int maxWidth, int maxHeight);
    void showAnchoredAbove(QWidget *anchor, int preferredWidth, int maxWidth, int maxHeight, int verticalGap = 6);
    void showCenteredAbove(QWidget *centerWidget, QWidget *anchor, int preferredWidth, int maxWidth, int maxHeight, int verticalGap = 6);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void smileClicked();

private:
    void placeCenteredAbove(QWidget *centerWidget, QWidget *anchor, int preferredWidth, int maxWidth, int maxHeight, int verticalGap);

    /** */
    FlowLayout * m_pLayout;
    QScrollArea * m_scrollArea;
    QWidget * m_scrollContent;
    QString selectedSmile;
    QPointer<QWidget> currentAnchor_;
    bool appFilterInstalled_ = false;
};
