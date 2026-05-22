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

#include "EmoticonDialog.h"
#include "QtContextAware.h"
#include "QtContext.h"

#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>
#include <QVBoxLayout>
#include <QFrame>
#include <QSet>

#include "EmoticonFactory.h"
#include "FlowLayout.h"

/** */
EmoticonDialog::EmoticonDialog(QWidget * parent, Qt::WindowFlags f)
: QDialog(parent, f), m_scrollArea(nullptr), m_scrollContent(nullptr) {
    setWindowTitle(tr("Select emoticon"));
    setWindowModality(Qt::NonModal);

    // Create scroll area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    // Create content widget
    m_scrollContent = new QWidget();
    m_pLayout = new FlowLayout(m_scrollContent);
    m_pLayout->setRowAlignment(Qt::AlignHCenter);
    m_pLayout->setContentsMargins(2, 2, 2, 2);
    m_pLayout->setSpacing(2);

    QSize s;
    int initialCount = 0;
    if (qtCtx()->emoticonFactory()) {
        qtCtx()->emoticonFactory()->fillLayout(m_pLayout, s);
        initialCount = m_pLayout->count();
    }

    // Supplement with Unicode emojis when the selected theme is sparse or unavailable.
    if (initialCount < 140) {
        QSet<QString> existingEntries;
        const QList<EmoticonLabel*> existingLabels = m_scrollContent->findChildren<EmoticonLabel*>(QString(), Qt::FindDirectChildrenOnly);
        for (const auto *label : existingLabels)
            existingEntries.insert(label->toolTip());

        static const QStringList unicodeEmojis = {
            QString::fromUtf8("😀"), QString::fromUtf8("😃"), QString::fromUtf8("😄"), QString::fromUtf8("😁"),
            QString::fromUtf8("😆"), QString::fromUtf8("😅"), QString::fromUtf8("🤣"), QString::fromUtf8("😂"),
            QString::fromUtf8("🙂"), QString::fromUtf8("🙃"), QString::fromUtf8("😉"), QString::fromUtf8("😊"),
            QString::fromUtf8("😇"), QString::fromUtf8("🥰"), QString::fromUtf8("😍"), QString::fromUtf8("🤩"),
            QString::fromUtf8("😘"), QString::fromUtf8("😗"), QString::fromUtf8("😚"), QString::fromUtf8("😙"),
            QString::fromUtf8("😋"), QString::fromUtf8("😛"), QString::fromUtf8("😜"), QString::fromUtf8("🤪"),
            QString::fromUtf8("😝"), QString::fromUtf8("🤑"), QString::fromUtf8("🤗"), QString::fromUtf8("🤭"),
            QString::fromUtf8("🤫"), QString::fromUtf8("🤔"), QString::fromUtf8("🤐"), QString::fromUtf8("🤨"),
            QString::fromUtf8("😐"), QString::fromUtf8("😑"), QString::fromUtf8("😶"), QString::fromUtf8("😏"),
            QString::fromUtf8("😒"), QString::fromUtf8("🙄"), QString::fromUtf8("😬"), QString::fromUtf8("🤥"),
            QString::fromUtf8("😌"), QString::fromUtf8("😔"), QString::fromUtf8("😪"), QString::fromUtf8("🤤"),
            QString::fromUtf8("😴"), QString::fromUtf8("😷"), QString::fromUtf8("🤒"), QString::fromUtf8("🤕"),
            QString::fromUtf8("🤢"), QString::fromUtf8("🤮"), QString::fromUtf8("🤧"), QString::fromUtf8("🥵"),
            QString::fromUtf8("🥶"), QString::fromUtf8("🥴"), QString::fromUtf8("😵"), QString::fromUtf8("🤯"),
            QString::fromUtf8("🤠"), QString::fromUtf8("🥳"), QString::fromUtf8("😎"), QString::fromUtf8("🤓"),
            QString::fromUtf8("🧐"), QString::fromUtf8("😕"), QString::fromUtf8("😟"), QString::fromUtf8("🙁"),
            QString::fromUtf8("☹️"), QString::fromUtf8("😮"), QString::fromUtf8("😯"), QString::fromUtf8("😲"),
            QString::fromUtf8("😳"), QString::fromUtf8("🥺"), QString::fromUtf8("😦"), QString::fromUtf8("😧"),
            QString::fromUtf8("😨"), QString::fromUtf8("😰"), QString::fromUtf8("😥"), QString::fromUtf8("😢"),
            QString::fromUtf8("😭"), QString::fromUtf8("😱"), QString::fromUtf8("😖"), QString::fromUtf8("😣"),
            QString::fromUtf8("😞"), QString::fromUtf8("😓"), QString::fromUtf8("😩"), QString::fromUtf8("😫"),
            QString::fromUtf8("🥱"), QString::fromUtf8("😤"), QString::fromUtf8("😡"), QString::fromUtf8("😠"),
            QString::fromUtf8("🤬"), QString::fromUtf8("😈"), QString::fromUtf8("👿"), QString::fromUtf8("💀"),
            QString::fromUtf8("☠️"), QString::fromUtf8("💩"), QString::fromUtf8("🤡"), QString::fromUtf8("👹"),
            QString::fromUtf8("👺"), QString::fromUtf8("👻"), QString::fromUtf8("👽"), QString::fromUtf8("👾"),
            QString::fromUtf8("🤖"), QString::fromUtf8("😺"), QString::fromUtf8("😸"), QString::fromUtf8("😹"),
            QString::fromUtf8("😻"), QString::fromUtf8("😼"), QString::fromUtf8("😽"), QString::fromUtf8("🙀"),
            QString::fromUtf8("😿"), QString::fromUtf8("😾"), QString::fromUtf8("🙈"), QString::fromUtf8("🙉"),
            QString::fromUtf8("🙊"), QString::fromUtf8("💋"), QString::fromUtf8("💌"), QString::fromUtf8("💘"),
            QString::fromUtf8("💝"), QString::fromUtf8("💖"), QString::fromUtf8("💗"), QString::fromUtf8("💓"),
            QString::fromUtf8("💞"), QString::fromUtf8("💕"), QString::fromUtf8("💟"), QString::fromUtf8("❣️"),
            QString::fromUtf8("💔"), QString::fromUtf8("❤️"), QString::fromUtf8("🧡"), QString::fromUtf8("💛"),
            QString::fromUtf8("💚"), QString::fromUtf8("💙"), QString::fromUtf8("💜"), QString::fromUtf8("🤎"),
            QString::fromUtf8("🖤"), QString::fromUtf8("🤍"), QString::fromUtf8("👍"), QString::fromUtf8("👎"),
            QString::fromUtf8("👊"), QString::fromUtf8("🤛"), QString::fromUtf8("🤜"), QString::fromUtf8("🤞"),
            QString::fromUtf8("✌️"), QString::fromUtf8("🤟"), QString::fromUtf8("🤘"), QString::fromUtf8("👌"),
            QString::fromUtf8("🤏"), QString::fromUtf8("👈"), QString::fromUtf8("👉"), QString::fromUtf8("👆"),
            QString::fromUtf8("👇"), QString::fromUtf8("☝️"), QString::fromUtf8("✋"), QString::fromUtf8("🤚"),
            QString::fromUtf8("🖐️"), QString::fromUtf8("🖖"), QString::fromUtf8("👋"), QString::fromUtf8("🤙"),
            QString::fromUtf8("💪"), QString::fromUtf8("🖕"), QString::fromUtf8("✍️"), QString::fromUtf8("🙏"),
            QString::fromUtf8("🔥"), QString::fromUtf8("✨"), QString::fromUtf8("🎉"), QString::fromUtf8("❤️"),
            QString::fromUtf8("✅"), QString::fromUtf8("❌"), QString::fromUtf8("💯"), QString::fromUtf8("🚀"),
            QString::fromUtf8("💬"), QString::fromUtf8("👀"), QString::fromUtf8("🎵"), QString::fromUtf8("🎶"),
            QString::fromUtf8("🍺"), QString::fromUtf8("🍻"), QString::fromUtf8("🏆"), QString::fromUtf8("🎮"),
            QString::fromUtf8("💻"), QString::fromUtf8("🖥️"), QString::fromUtf8("📱"), QString::fromUtf8("💡"),
            QString::fromUtf8("🔒"), QString::fromUtf8("🔓"), QString::fromUtf8("⭐"), QString::fromUtf8("🌟")
        };

        for (const QString &emoji : unicodeEmojis) {
            if (existingEntries.contains(emoji))
                continue;

            EmoticonLabel *lbl = new EmoticonLabel();
            lbl->setText(emoji);
            QFont font = lbl->font();
            font.setPointSize(22);
            lbl->setFont(font);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setToolTip(emoji);
            lbl->setFixedSize(QSize(34, 34));
            lbl->setContentsMargins(0, 0, 0, 0);
            m_pLayout->addWidget(lbl);
            existingEntries.insert(emoji);
        }
    }

    m_scrollArea->setWidget(m_scrollContent);

    // Keep the minimum height low enough that the popup can stay above the toolbar.
    setMinimumSize(QSize(360, 140));
    preparePopupGeometry(860, 1400, 620);

    // Create layout for the dialog
    QVBoxLayout *dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(0, 0, 0, 0);
    dialogLayout->addWidget(m_scrollArea);

    for (const auto &l : findChildren<EmoticonLabel*>())
        connect(l, &EmoticonLabel::clicked, this, &EmoticonDialog::smileClicked);
}

/** */
EmoticonDialog::~EmoticonDialog() {
    if (appFilterInstalled_ && qApp)
        qApp->removeEventFilter(this);

    if (m_scrollContent)
        m_scrollContent->deleteLater();
    if (m_scrollArea)
        m_scrollArea->deleteLater();
}

void EmoticonDialog::smileClicked(){
    EmoticonLabel *lbl = qobject_cast<EmoticonLabel* >(sender());

    if (!lbl)
        return;

    selectedSmile = lbl->toolTip();

    accept();
}

void EmoticonDialog::preparePopupGeometry(int preferredWidth, int maxWidth, int maxHeight)
{
    const int minWidth = 360;
    const int targetWidth = qBound(minWidth, preferredWidth, qMax(minWidth, maxWidth));
    const int targetMaxHeight = qMax(80, maxHeight);
    const int targetMinHeight = qMin(140, targetMaxHeight);

    int contentHeight = sizeHint().height();
    if (m_pLayout && m_pLayout->hasHeightForWidth()) {
        const QMargins margins = m_pLayout->contentsMargins();
        const int effectiveWidth = qMax(120, targetWidth - margins.left() - margins.right());
        contentHeight = m_pLayout->heightForWidth(effectiveWidth) + margins.top() + margins.bottom() + 12;
    }

    resize(targetWidth, qBound(targetMinHeight, contentHeight, targetMaxHeight));
}

void EmoticonDialog::showAnchoredAbove(QWidget *anchor, int preferredWidth, int maxWidth, int maxHeight, int verticalGap)
{
    showCenteredAbove(anchor, anchor, preferredWidth, maxWidth, maxHeight, verticalGap);
}

void EmoticonDialog::showCenteredAbove(QWidget *centerWidget, QWidget *anchor, int preferredWidth, int maxWidth, int maxHeight, int verticalGap)
{
    if (!anchor) {
        currentAnchor_.clear();
        preparePopupGeometry(preferredWidth, maxWidth, maxHeight);
        show();
        raise();
        activateWindow();
        return;
    }

    if (!appFilterInstalled_ && qApp) {
        qApp->installEventFilter(this);
        appFilterInstalled_ = true;
    }
    currentAnchor_ = anchor;
    placeCenteredAbove(centerWidget, anchor, preferredWidth, maxWidth, maxHeight, verticalGap);

    QPointer<QWidget> centerGuard(centerWidget);
    QPointer<QWidget> anchorGuard(anchor);
    QTimer::singleShot(0, this, [this, centerGuard, anchorGuard, preferredWidth, maxWidth, maxHeight, verticalGap]() {
        if (!isVisible() || !anchorGuard)
            return;

        placeCenteredAbove(centerGuard ? centerGuard.data() : anchorGuard.data(),
                           anchorGuard.data(), preferredWidth, maxWidth, maxHeight, verticalGap);
    });
}

void EmoticonDialog::placeCenteredAbove(QWidget *centerWidget, QWidget *anchor, int preferredWidth, int maxWidth, int maxHeight, int verticalGap)
{
    if (!anchor)
        return;

    const QPoint anchorTopLeft = anchor->mapToGlobal(QPoint(0, 0));
    const QRect anchorRect(anchorTopLeft, anchor->size());
    QWidget *centerSource = centerWidget ? centerWidget : anchor;
    const QRect centerRect(centerSource->mapToGlobal(QPoint(0, 0)), centerSource->size());

    QScreen *screen = QApplication::screenAt(anchorRect.center());
    if (!screen)
        screen = QApplication::primaryScreen();

    const QRect screenGeo = screen ? screen->availableGeometry() : QRect();
    constexpr int screenMargin = 8;
    constexpr int frameHeightAllowance = 36;

    int boundedMaxWidth = maxWidth;
    int boundedMaxHeight = maxHeight;

    if (screenGeo.isValid()) {
        boundedMaxWidth = qMin(maxWidth, qMax(360, screenGeo.width() - screenMargin * 2));

        const int availableAbove = anchorRect.top() - verticalGap - (screenGeo.top() + screenMargin) - frameHeightAllowance;
        if (availableAbove >= 180)
            boundedMaxHeight = qMin(maxHeight, availableAbove);
    }

    preparePopupGeometry(preferredWidth, boundedMaxWidth, boundedMaxHeight);

    // Show first so macOS reports the real frame including the title bar.
    move(centerRect.center().x() - width() / 2, anchorRect.top() - height() - verticalGap);
    show();

    QRect frameRect = frameGeometry();
    const int frameExtraHeight = qMax(frameHeightAllowance, frameRect.height() - height());
    if (screenGeo.isValid()) {
        const int availableContentHeight = anchorRect.top() - verticalGap - (screenGeo.top() + screenMargin) - frameExtraHeight;
        if (availableContentHeight >= 80 && height() > availableContentHeight) {
            resize(width(), qMax(80, availableContentHeight));
            frameRect = frameGeometry();
        }
    }

    const QPoint contentOffset = geometry().topLeft() - frameRect.topLeft();
    const int frameWidth = frameRect.width();
    const int frameHeight = frameRect.height();
    int frameX = centerRect.center().x() - frameWidth / 2;
    int frameY = anchorRect.top() - frameHeight - verticalGap;

    if (screenGeo.isValid()) {
        const int minLeft = screenGeo.left() + screenMargin;
        const int maxLeft = qMax(minLeft, screenGeo.left() + screenGeo.width() - screenMargin - frameWidth);
        frameX = qBound(minLeft, frameX, maxLeft);

        const int minTop = screenGeo.top() + screenMargin;
        frameY = qMax(minTop, frameY);
    }

    move(frameX + contentOffset.x(), frameY + contentOffset.y());
    raise();
    activateWindow();
}

bool EmoticonDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!isVisible())
        return QDialog::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();

        if (frameGeometry().contains(globalPos))
            return QDialog::eventFilter(watched, event);

        if (currentAnchor_) {
            const QRect anchorRect(currentAnchor_->mapToGlobal(QPoint(0, 0)), currentAnchor_->size());
            if (anchorRect.contains(globalPos))
                return QDialog::eventFilter(watched, event);
        }

        close();
    }

    return QDialog::eventFilter(watched, event);
}
