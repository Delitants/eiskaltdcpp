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
#include <QScrollArea>
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

    // Start with a wide default so emoji selector opens in multi-row mode.
    setMinimumSize(QSize(360, 240));
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
    const int targetMaxHeight = qMax(240, maxHeight);

    int contentHeight = sizeHint().height();
    if (m_pLayout && m_pLayout->hasHeightForWidth()) {
        const QMargins margins = m_pLayout->contentsMargins();
        const int effectiveWidth = qMax(120, targetWidth - margins.left() - margins.right());
        contentHeight = m_pLayout->heightForWidth(effectiveWidth) + margins.top() + margins.bottom() + 12;
    }

    resize(targetWidth, qBound(240, contentHeight, targetMaxHeight));
}
