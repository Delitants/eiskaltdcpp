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

#include "PMWindow.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "WulforSettings.h"
#include "WulforUtil.h"
#include "HubManager.h"
#include "MainWindow.h"
#include "Notification.h"
#include "EmoticonFactory.h"
#include "EmoticonDialog.h"
#include "FlowLayout.h"
#include "ArenaWidgetManager.h"

#include "dcpp/stdinc.h"
#include "dcpp/ClientManager.h"
#include "dcpp/QueueManager.h"
#include "dcpp/User.h"
#include "dcpp/DCPlusPlus.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QCloseEvent>
#include <QMenu>
#include <QAction>
#include <QScrollBar>
#include <QFont>
#include <QApplication>
#include <QScreen>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

using namespace dcpp;

static inline void clearLayout(QLayout *l){
    if (!l)
        return;

    for (QLayoutItem *item = l->takeAt(0); item; item = l->takeAt(0)) {
        l->removeWidget(item->widget());
        item->widget()->deleteLater();
        delete item;
    }

    l->invalidate();
}

static QString themedChatTextColor(const QPalette &palette)
{
    const QColor base = palette.color(QPalette::Base);
    const QColor window = palette.color(QPalette::Window);
    const int baseLightness = qMin(base.lightness(), window.lightness());
    return baseLightness < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
}

static double channelToLinear(const int channel)
{
    const double normalized = channel / 255.0;
    if (normalized <= 0.04045)
        return normalized / 12.92;
    return std::pow((normalized + 0.055) / 1.055, 2.4);
}

static double relativeLuminance(const QColor &color)
{
    return 0.2126 * channelToLinear(color.red()) +
           0.7152 * channelToLinear(color.green()) +
           0.0722 * channelToLinear(color.blue());
}

static double contrastRatio(const QColor &a, const QColor &b)
{
    const double luminanceA = relativeLuminance(a);
    const double luminanceB = relativeLuminance(b);
    const double lighter = std::max(luminanceA, luminanceB);
    const double darker = std::min(luminanceA, luminanceB);
    return (lighter + 0.05) / (darker + 0.05);
}

static QString ensureReadableChatColor(const QString &candidate, const QPalette &palette)
{
    const QColor resolved = QColor::fromString(candidate.trimmed());
    if (!resolved.isValid())
        return themedChatTextColor(palette);

    const QColor base = palette.color(QPalette::Base);
    if (contrastRatio(resolved, base) < 3.0)
        return themedChatTextColor(palette);

    return resolved.name(QColor::HexRgb);
}

static QString resolveChatColorValue(const QString &settingKeyOrColor, const QPalette &palette)
{
    const QString keyOrColor = settingKeyOrColor.trimmed();
    if (QColor::fromString(keyOrColor).isValid())
        return ensureReadableChatColor(keyOrColor, palette);

    return ensureReadableChatColor(qtCtx()->settings()->getStr(keyOrColor), palette);
}

static bool parseInlineImageSpoilerUrl(const QString &urlText, QString &localPath, QString &displayName, int64_t &size)
{
    const QUrl url(urlText);
    if (!url.isValid() || url.scheme() != QLatin1String("eiskalt-chatimg") || url.host() != QLatin1String("open"))
        return false;

    const QUrlQuery query(url);
    localPath = query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded).trimmed();
    displayName = query.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded).trimmed();
    size = query.queryItemValue(QStringLiteral("size")).toLongLong();

    return !localPath.isEmpty();
}

static bool toggleInlineImageSpoiler(QTextEdit *editor,
                                     const QString &urlText,
                                     const QPoint &globalPos,
                                     QSet<QString> &expandedKeys,
                                     QHash<QString, QTextDocumentFragment> &collapsedBlocks)
{
    if (!editor)
        return false;

    QString localPath;
    QString displayName;
    int64_t size = 0;

    if (!parseInlineImageSpoilerUrl(urlText, localPath, displayName, size))
        return false;

    const QFileInfo localInfo(localPath);
    if (!localInfo.exists() || !localInfo.isFile())
        return true;

    QTextCursor cursor = editor->cursorForPosition(editor->mapFromGlobal(globalPos));
    cursor.select(QTextCursor::BlockUnderCursor);
    const QString expansionKey = QStringLiteral("%1#%2").arg(localInfo.absoluteFilePath()).arg(cursor.block().position());

    if (expandedKeys.contains(expansionKey)) {
        const auto it = collapsedBlocks.constFind(expansionKey);
        if (it != collapsedBlocks.constEnd())
            cursor.insertFragment(it.value());

        expandedKeys.remove(expansionKey);
        collapsedBlocks.remove(expansionKey);
        return true;
    }

    collapsedBlocks.insert(expansionKey, QTextDocumentFragment(cursor));

    cursor.clearSelection();
    cursor.movePosition(QTextCursor::EndOfBlock);

    const QString imageName = displayName.isEmpty() ? localInfo.fileName() : displayName;
    const int64_t imageSize = size > 0 ? size : localInfo.size();
    const QString localUrl = QUrl::fromLocalFile(localInfo.absoluteFilePath()).toString();
    const QString title = QObject::tr("%1 (%2)").arg(imageName, WulforUtil::formatBytes(imageSize));
    const QString imageHtml = QStringLiteral("<br/><a href=\"%1\" title=\"%2\" style=\"text-decoration:none\">"
                                             "<img src=\"%1\" alt=\"%3\" style=\"display:block; width:100%%; height:auto; margin-top:4px;\" />"
                                             "</a>")
        .arg(localUrl, title.toHtmlEscaped(), imageName.toHtmlEscaped());

    cursor.insertHtml(imageHtml);
    expandedKeys.insert(expansionKey);

    return true;
}

PMWindow::PMWindow(const QString &cid_, const QString &hubUrl_):
        hasMessages(false),
        hasHighlightMessages(false),
        cid(cid_),
        hubUrl(hubUrl_),
        arena_menu(nullptr),
        emojiDialog_(nullptr)
{
    setupUi(this);


    frame_SMILES->setLayout(new FlowLayout(frame_SMILES));
    frame_SMILES->setVisible(false);

    setAttribute(Qt::WA_DeleteOnClose);

    lineEdit_FIND->installEventFilter(this);

    plainTextEdit_INPUT->setWordWrapMode(QTextOption::NoWrap);
    plainTextEdit_INPUT->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    plainTextEdit_INPUT->setContextMenuPolicy(Qt::CustomContextMenu);
    plainTextEdit_INPUT->installEventFilter(this);
    plainTextEdit_INPUT->setAcceptRichText(false);
    plainTextEdit_INPUT->setMinimumHeight(72);
    plainTextEdit_INPUT->setMaximumHeight(200);
    plainTextEdit_INPUT->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    const QPalette inputPalette = frame->palette();
    const bool darkInput = (inputPalette.color(QPalette::Window).lightness() + inputPalette.color(QPalette::Base).lightness()) / 2 < 128;
    QColor inputBorder = darkInput ? inputPalette.color(QPalette::Window).lighter(170)
                                   : inputPalette.color(QPalette::Window).darker(140);
    if (qAbs(inputBorder.lightness() - inputPalette.color(QPalette::Window).lightness()) < 26) {
        const QColor textColor = inputPalette.color(QPalette::Text);
        inputBorder = darkInput ? textColor.lighter(145) : textColor.darker(150);
    }
    const QColor inputBackground = darkInput ? inputPalette.color(QPalette::Window).lighter(106)
                                             : inputPalette.color(QPalette::Window);
    frame->setStyleSheet(QStringLiteral(
        "QFrame#frame {"
        " border: 1px solid %1;"
        " border-radius: 8px;"
        " background: %2;"
        "}"
    ).arg(inputBorder.name(), inputBackground.name()));

    if (gridLayout) {
        gridLayout->setContentsMargins(6, 6, 6, 6);
        gridLayout->setHorizontalSpacing(4);
        gridLayout->setVerticalSpacing(4);
    }

    textEdit_CHAT->viewport()->installEventFilter(this);
    textEdit_CHAT->viewport()->setMouseTracking(true);
    textEdit_CHAT->document()->setMaximumBlockCount(qtCtx()->settings()->getInt(WI_CHAT_MAXPARAGRAPHS));
    textEdit_CHAT->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    textEdit_CHAT->setTabStopDistance(40);

    frame_2->setVisible(false);

    updateStyles();

    toolButton_SMILE->setVisible(true);
    toolButton_SMILE->setIcon(QIcon());
    toolButton_SMILE->setText(QString::fromUtf8("😊"));
    toolButton_SMILE->setToolTip(tr("Emoji"));
    toolButton_SMILE->setContextMenuPolicy(Qt::CustomContextMenu);
    toolButton_SMILE->setAutoRaise(true);
    toolButton_SMILE->setIconSize(QSize(18, 18));
    toolButton_SMILE->setFixedSize(QSize(28, 28));
    toolButton_SMILE->setStyleSheet(QStringLiteral("QToolButton { font-size: 18px; }"));
    auto *toolButton_IMAGE = new QToolButton(this);
    toolButton_IMAGE->setAutoRaise(true);
    toolButton_IMAGE->setMinimumHeight(24);
    toolButton_IMAGE->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFILETYPE_PICTURE));
    toolButton_IMAGE->setToolTip(tr("Image"));
    horizontalLayout_BBCODE->setSpacing(3);
    horizontalLayout_BBCODE->setContentsMargins(0, 0, 0, 0);
    const int smileButtonIndex = horizontalLayout_BBCODE->indexOf(toolButton_SMILE);
    if (smileButtonIndex >= 0)
        horizontalLayout_BBCODE->insertWidget(smileButtonIndex, toolButton_IMAGE);
    else
        horizontalLayout_BBCODE->insertWidget(horizontalLayout_BBCODE->count() - 1, toolButton_IMAGE);
    const QList<QToolButton*> formatButtons = {
        toolButton_BOLD, toolButton_ITALIC, toolButton_UNDERLINE, toolButton_STRIKE,
        toolButton_COLOR, toolButton_LINK, toolButton_CODE, toolButton_IMAGE
    };
    for (auto *button : formatButtons) {
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setAutoRaise(true);
        button->setMinimumHeight(24);
        button->setMaximumHeight(28);
    }
    QFont boldFont = toolButton_BOLD->font();
    boldFont.setBold(true);
    toolButton_BOLD->setFont(boldFont);
    QFont italicFont = toolButton_ITALIC->font();
    italicFont.setItalic(true);
    toolButton_ITALIC->setFont(italicFont);
    QFont underlineFont = toolButton_UNDERLINE->font();
    underlineFont.setUnderline(true);
    toolButton_UNDERLINE->setFont(underlineFont);
    QFont strikeFont = toolButton_STRIKE->font();
    strikeFont.setStrikeOut(true);
    toolButton_STRIKE->setFont(strikeFont);

    for (int i = 0; i < horizontalLayout_BBCODE->count(); ++i)
        horizontalLayout_BBCODE->setStretch(i, 0);
    horizontalLayout_BBCODE->addStretch(1);
    horizontalLayout_BBCODE->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    connect(toolButton_BOLD, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->wrapWithTag("b"); });
    connect(toolButton_ITALIC, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->wrapWithTag("i"); });
    connect(toolButton_UNDERLINE, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->wrapWithTag("u"); });
    connect(toolButton_STRIKE, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->wrapWithTag("s"); });
    connect(toolButton_COLOR, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->insertColorTag(); });
    connect(toolButton_LINK, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->insertUrlTag(); });
    connect(toolButton_CODE, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->wrapWithTag("code"); });
    connect(toolButton_IMAGE, &QToolButton::clicked, this, [this]() { plainTextEdit_INPUT->insertImageMagnet(); });

    toolButton_ALL->setCheckable(true);

    toolButton_HIDE->setIcon(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiEDITDELETE));

    arena_menu = new QMenu(tr("Private message"));
    QAction *close_wnd = new QAction(qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiFILECLOSE), tr("Close"), arena_menu);
    arena_menu->addAction(close_wnd);

    reloadSomeSettings();

    connect(close_wnd, &QAction::triggered, this, &PMWindow::slotClose);
    connect(pushButton_HUB, &QPushButton::clicked, this, &PMWindow::slotHub);
    connect(pushButton_SHARE, &QPushButton::clicked, this, &PMWindow::slotShare);
    connect(toolButton_SMILE, &QToolButton::clicked, this, &PMWindow::slotSmile);
    connect(toolButton_SMILE, &QToolButton::customContextMenuRequested, this, &PMWindow::slotSmileContextMenu);
    connect(plainTextEdit_INPUT, &QTextEdit::textChanged, this, &PMWindow::inputTextChanged);
    connect(plainTextEdit_INPUT, &QWidget::customContextMenuRequested, this, &PMWindow::inputTextMenu);
    connect(qtCtx()->settings(), &WulforSettings::strValueChanged, this, &PMWindow::slotSettingChanged);
    connect(lineEdit_FIND, &QLineEdit::textChanged, this, &PMWindow::slotFindTextEdited);
    connect(toolButton_HIDE, &QToolButton::clicked, this, &PMWindow::slotHideSearchBar);
    connect(toolButton_BACK, &QToolButton::clicked, this, &PMWindow::slotFindBackward);
    connect(toolButton_FORWARD, &QToolButton::clicked, this, &PMWindow::slotFindForward);
    connect(toolButton_ALL, &QToolButton::clicked, this, &PMWindow::slotFindAll);

    out_messages_index = 0;
    out_messages_unsent = false;
    
    setState(state() & ~ArenaWidget::RaiseOnStart);// Do not allow PMWindow to be automatically showed by default
}

void PMWindow::setCompleter(QCompleter *completer, UserListModel *model) {
    plainTextEdit_INPUT->setCompleter(completer, model);
}

PMWindow::~PMWindow(){
    arena_menu->deleteLater();
}

void PMWindow::slotClose() {
    qtCtx()->arenaWidgetManager()->rem(this);
}

bool PMWindow::eventFilter(QObject *obj, QEvent *e){
    if (e->type() == QEvent::KeyRelease){
        QKeyEvent *k_e = reinterpret_cast<QKeyEvent*>(e);

        if ((static_cast<QTextEdit*>(obj) == plainTextEdit_INPUT) &&
            (k_e->key() == Qt::Key_Enter || k_e->key() == Qt::Key_Return) &&
            (k_e->modifiers() == Qt::NoModifier))
        {
            return true;
        }
        else if (static_cast<LineEdit*>(obj) == lineEdit_FIND && k_e->key() == Qt::Key_Escape){
            lineEdit_FIND->clear();
            slotHideSearchBar();
            return true;
        }
    }
    else if (e->type() == QEvent::KeyPress){
        QKeyEvent *k_e = reinterpret_cast<QKeyEvent*>(e);

        const bool controlModifier = (k_e->modifiers() == Qt::ControlModifier);

        if (static_cast<QTextEdit*>(obj) == plainTextEdit_INPUT)
        {
            const bool useCtrlEnter = qtCtx()->settings()->getBool(WB_USE_CTRL_ENTER);
            const bool keyEnter = (k_e->key() == Qt::Key_Enter || k_e->key() == Qt::Key_Return);
            const bool shiftModifier = (k_e->modifiers() == Qt::ShiftModifier);

            if ((useCtrlEnter && keyEnter && controlModifier) ||
                (!useCtrlEnter && keyEnter && !controlModifier && !shiftModifier))
            {
                const QString msg = plainTextEdit_INPUT->toPlainText();

                HubFrame *fr = qobject_cast<HubFrame*>(qtCtx()->hubManager()->getHub(hubUrl));

                if (fr) {
                    if (!fr->parseForCmd(msg, this))
                        sendMessage(msg, false, false);
                }
                else {
                    sendMessage(msg, false, false);
                }

                plainTextEdit_INPUT->setPlainText("");

                return true;
            }
        }

        if (controlModifier){
            if (k_e->key() == Qt::Key_Equal || k_e->key() == Qt::Key_Plus){
                textEdit_CHAT->zoomIn();

                return true;
            }
            else if (k_e->key() == Qt::Key_Minus){
                textEdit_CHAT->zoomOut();

                return true;
            }
        }
    }
    else if (e->type() == QEvent::MouseButtonRelease){
        QMouseEvent *m_e = reinterpret_cast<QMouseEvent*>(e);

        if ((static_cast<QWidget*>(obj) == textEdit_CHAT->viewport()) && (m_e->button() == Qt::LeftButton)){
            const QString pressedParagraph = textEdit_CHAT->anchorAt(textEdit_CHAT->mapFromGlobal(QCursor::pos()));

            if (!pressedParagraph.isEmpty() &&
                toggleInlineImageSpoiler(textEdit_CHAT,
                                         pressedParagraph,
                                         QCursor::pos(),
                                         expandedInlineImageKeys_,
                                         collapsedInlineImageBlocks_))
            {
                return true;
            }

            qtCtx()->wulforUtil()->openUrl(pressedParagraph);
        }
    }
    else if (e->type() == QEvent::MouseMove && (static_cast<QWidget*>(obj) == textEdit_CHAT->viewport())){
        QString str = textEdit_CHAT->anchorAt(textEdit_CHAT->mapFromGlobal(QCursor::pos()));

        if (!str.isEmpty())
            textEdit_CHAT->viewport()->setCursor(Qt::PointingHandCursor);
        else
            textEdit_CHAT->viewport()->setCursor(Qt::IBeamCursor);
    }
    else if (e->type() == QEvent::MouseButtonDblClick){
        HubFrame *fr = qobject_cast<HubFrame*>(qtCtx()->hubManager()->getHub(hubUrl));
        bool cursoratnick = false;
        QString nick = "",nickstatus="",nickmessage="";
        QString cid = "";
        QTextCursor cursor = textEdit_CHAT->textCursor();

        QString pressedParagraph = cursor.block().text();
        int positionCursor = cursor.columnNumber();
        int l = pressedParagraph.indexOf(" <");
        int r = pressedParagraph.indexOf("> ");
        if (l < r)
            nickmessage = pressedParagraph.mid(l+2, r-l-2);
        else {
            int l1 = pressedParagraph.indexOf(" * ");
            //qDebug() << positionCursor << " " << l1 << " " << l << " " << r;
            if (l1 > -1 ) {
                QString pressedParagraphstatus = pressedParagraph.remove(0,l1+3).simplified();
                //qDebug() << pressedParagraphstatus;
                int r1 = pressedParagraphstatus.indexOf(" ");
                //qDebug() << r1;
                nickstatus = pressedParagraphstatus.mid(0, r1);
                //qDebug() << nickstatus;
            }
        }
        if ((!nickmessage.isEmpty() || !nickstatus.isEmpty())&& fr){
            //qDebug() << nickstatus;
            //qDebug() << nickmessage;
            nick = nickmessage + nickstatus;
            //qDebug() << nick;
            cid = fr->getCIDforNick(nick);
        }
        if ((positionCursor < r) && (positionCursor > l))
            cursoratnick = true;

        if (!cid.isEmpty()){
            if (qtCtx()->settings()->getInt(WI_CHAT_DBLCLICK_ACT) == 1 && fr && cursoratnick)
                    fr->browseUserFiles(cid, false);
            else if (qtCtx()->settings()->getInt(WI_CHAT_DBLCLICK_ACT) == 2 && fr && cursoratnick)
                    fr->addPM(cid, "");
            else if (textEdit_CHAT->anchorAt(textEdit_CHAT->mapFromGlobal(QCursor::pos())).startsWith("user://")){
                if(!plainTextEdit_INPUT->textCursor().position())
                    plainTextEdit_INPUT->textCursor().insertText(nick + qtCtx()->settings()->getStr(WS_CHAT_SEPARATOR) + " ");
                else
                    plainTextEdit_INPUT->textCursor().insertText(nick + " ");

                plainTextEdit_INPUT->setFocus();
            }
        }
    }

    return QWidget::eventFilter(obj, e);
}

void PMWindow::closeEvent(QCloseEvent *c_e){
    if (emojiDialog_ && emojiDialog_->isVisible())
        emojiDialog_->close();

    emit privateMessageClosed(cid);

    hasMessages = false;
    hasHighlightMessages = false;

    c_e->accept();
}

void PMWindow::showEvent(QShowEvent *e){
    e->accept();

    if (isVisible()){
        hasMessages = false;
        hasHighlightMessages = false;
        qtCtx()->mainWindow()->redrawToolPanel();
    }
}

void PMWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);

    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        reloadSomeSettings();
    }
}

void PMWindow::slotActivate(){
    plainTextEdit_INPUT->setFocus();
}

QString PMWindow::getArenaTitle(){
    QString nick = (cid.length() > 24)? qtCtx()->wulforUtil()->getNickViaOnlineUser(cid, hubUrl) : cid;

    nick_ = nick.isEmpty()? nick_ : nick;

    return (tr("%1 on hub %2").arg(nick_).arg(hubUrl));
}

QString PMWindow::getArenaShortTitle(){
    QString nick = (cid.length() > 24)? qtCtx()->wulforUtil()->getNickViaOnlineUser(cid, hubUrl) : cid;

    nick_ = nick.isEmpty()? nick_ : nick;

    return nick_;
}

QWidget *PMWindow::getWidget(){
    return this;
}

QMenu *PMWindow::getMenu(){
    return arena_menu;
}

const QPixmap &PMWindow::getPixmap(){
    if (hasHighlightMessages)
        return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiMESSAGE);
    else if (hasMessages)
        return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiPMMSG);
    else
        return qtCtx()->wulforUtil()->getPixmap(WulforUtil::eiUSERS);
}

ArenaWidget::Role PMWindow::role() const {
    return ArenaWidget::PrivateMessage;
}

void PMWindow::requestClear(){
    clearChat();
}

void PMWindow::requestFilter() {
    slotShowSearchBar();
}

void PMWindow::requestFocus() {
    plainTextEdit_INPUT->setFocus();
}

void PMWindow::clearChat(){
    textEdit_CHAT->setHtml("");
    expandedInlineImageKeys_.clear();
    collapsedInlineImageBlocks_.clear();
    addStatus(tr("Chat cleared."));

    updateStyles();
}

void PMWindow::reloadSomeSettings()
{
    QPalette chatPalette = QApplication::palette(textEdit_CHAT);
    const bool useCustomChatBg = qtCtx()->settings()->getBool("hubframe/change-chat-background-color", false);
    if (useCustomChatBg) {
        const QColor customBg = QColor::fromString(qtCtx()->settings()->getStr("hubframe/chat-background-color"));
        if (customBg.isValid())
            chatPalette.setColor(QPalette::Base, customBg);
    }
    const QColor foreground(themedChatTextColor(chatPalette));
    chatPalette.setColor(QPalette::Text, foreground);
    chatPalette.setColor(QPalette::WindowText, foreground);
    textEdit_CHAT->setPalette(chatPalette);

    const QPalette inputPalette = QApplication::palette(frame);
    const bool darkInput = (inputPalette.color(QPalette::Window).lightness() + inputPalette.color(QPalette::Base).lightness()) / 2 < 128;
    QColor inputBorder = darkInput ? inputPalette.color(QPalette::Window).lighter(170)
                                   : inputPalette.color(QPalette::Window).darker(140);
    if (qAbs(inputBorder.lightness() - inputPalette.color(QPalette::Window).lightness()) < 26) {
        const QColor textColor = inputPalette.color(QPalette::Text);
        inputBorder = darkInput ? textColor.lighter(145) : textColor.darker(150);
    }
    const QColor inputBackground = darkInput ? inputPalette.color(QPalette::Window).lighter(106)
                                             : inputPalette.color(QPalette::Window);
    frame->setStyleSheet(QStringLiteral(
        "QFrame#frame {"
        " border: 1px solid %1;"
        " border-radius: 8px;"
        " background: %2;"
        "}"
    ).arg(inputBorder.name(), inputBackground.name()));

    updateStyles();
}

void PMWindow::updateStyles(){
    QString custom_font_desc = qtCtx()->settings()->getStr(WS_CHAT_PM_FONT);
    QFont custom_font;
    const QString chatTextColor = themedChatTextColor(textEdit_CHAT->palette());

    if (!custom_font_desc.isEmpty() && custom_font.fromString(custom_font_desc)){
        textEdit_CHAT->document()->setDefaultStyleSheet(
                                                        QString("pre { margin:0px; white-space:pre-wrap; color:%3; font-family:'%1'; font-size: %2pt; }")
                                                        .arg(custom_font.family()).arg(custom_font.pointSize()).arg(chatTextColor)
                                                       );
    }
    else {
        textEdit_CHAT->document()->setDefaultStyleSheet(
                                                        QString("pre { margin:0px; white-space:pre-wrap; color:%2; font-family:'%1' }")
                                                        .arg(QApplication::font().family()).arg(chatTextColor)
                                                       );
    }
}

void PMWindow::addStatusMessage(const QString &msg){
    QString status = " * ";

    QString nick = "";
    QString time = "";

    if (!qtCtx()->settings()->getStr(WS_CHAT_TIMESTAMP).isEmpty())
        time = "[" + QDateTime::currentDateTime().toString(qtCtx()->settings()->getStr(WS_CHAT_TIMESTAMP)) + "]";

    status = time + status;
    const QPalette chatPalette = textEdit_CHAT->palette();
    const QString statusColor = resolveChatColorValue(WS_CHAT_STAT_COLOR, chatPalette);
    status += "<font color=\"" + statusColor + "\"><b>" + nick + "</b> </font>: ";
    status += msg;

    addOutput(status);
}

void PMWindow::addStatus(QString msg){
    QString status = "";
    QString nick    = " * ";
    const QPalette chatPalette = textEdit_CHAT->palette();
    const QString statusColor = resolveChatColorValue(WS_CHAT_STAT_COLOR, chatPalette);
    const QString timeColor = resolveChatColorValue(WS_CHAT_TIME_COLOR, chatPalette);

    qtCtx()->wulforUtil()->textToHtml(msg, true);
    qtCtx()->wulforUtil()->textToHtml(nick, true);

    msg             = "<font color=\"" + themedChatTextColor(chatPalette) + "\">" + msg + "</font>";
    QString time    = "";

    if (!qtCtx()->settings()->getStr(WS_CHAT_TIMESTAMP).isEmpty())
        time = "<font color=\"" + timeColor + "\">[" + QDateTime::currentDateTime().toString(qtCtx()->settings()->getStr(WS_CHAT_TIMESTAMP)) + "]</font>";

    status = time + "<font color=\"" + statusColor + "\"><b>" + nick + "</b> </font>";
    status += msg;

    qtCtx()->wulforUtil()->textToHtml(status, false);

    addOutput(status);
}

void PMWindow::addOutput(QString msg){
    msg.replace("\r", "");
    msg = "<pre>" + msg + "</pre>";
    textEdit_CHAT->append(msg);

    if (!isVisible()) {
        hasMessages = true;
        qtCtx()->mainWindow()->redrawToolPanel();
    }
}

void PMWindow::addUserData(const QString &nick){
    QTextDocument *chatDoc = textEdit_CHAT->document();
    for (QTextBlock itu = chatDoc->lastBlock(); itu.isValid(); itu = itu.previous()){
        if (!itu.userData())
            itu.setUserData(new UserListUserData(nick));
        else
            break;
    }
}

void PMWindow::sendMessage(QString msg, const bool thirdPerson, const bool stripNewLines){
    UserPtr user = qtCtx()->dcCtx().getClientManager()->findUser(CID(cid.toStdString()));

    if (user && user->isOnline()){

        if (stripNewLines)
            msg.replace("\n", "");

        if (msg.isEmpty() || msg == "\n")
            return;

        qtCtx()->dcCtx().getClientManager()->privateMessage(HintedUser(user, _tq(hubUrl)), _tq(msg), thirdPerson);
    }
    else {
        addStatusMessage(tr("User went offline"));
    }

    if (out_messages_unsent){
        out_messages.removeLast();
        out_messages_unsent = false;
    }

    out_messages << msg;

    if (out_messages.size() > qtCtx()->settings()->getInt(WI_OUT_IN_HIST))
        out_messages.removeFirst();

    out_messages_index = out_messages.size()-1;
}

QWidget *PMWindow::inputWidget() const {
    return plainTextEdit_INPUT;
}

void PMWindow::setHasHighlightMessages(bool h) {
    hasHighlightMessages = (h && !isVisible());
}

bool PMWindow::hasNewMessages() {
    return (hasMessages || hasHighlightMessages);
}

void PMWindow::nextMsg(){
    if (!plainTextEdit_INPUT->hasFocus())
        return;

    if (out_messages_index < 0 ||
        out_messages_index+1 > out_messages.size()-1 ||
        out_messages.isEmpty())
        return;

    if (out_messages.at(out_messages_index) != plainTextEdit_INPUT->toPlainText())
        out_messages[out_messages_index] = plainTextEdit_INPUT->toPlainText();

    if (out_messages_index+1 <= out_messages.size()-1)
        out_messages_index++;

    plainTextEdit_INPUT->setPlainText(out_messages.at(out_messages_index));

    if (out_messages_unsent && out_messages_index == out_messages.size()-1){
        out_messages.removeLast();
        out_messages_unsent = false;
        out_messages_index = out_messages.size()-1;
    }
}

void PMWindow::prevMsg(){
    if (!plainTextEdit_INPUT->hasFocus())
        return;

    if (out_messages_index < 1 ||
        out_messages_index-1 > out_messages.size()-1 ||
        out_messages.isEmpty())
        return;

    if (!out_messages_unsent && out_messages_index == out_messages.size()-1){
        out_messages << plainTextEdit_INPUT->toPlainText();
        out_messages_unsent = true;
        out_messages_index++;
    }

    if (out_messages.at(out_messages_index) != plainTextEdit_INPUT->toPlainText())
        out_messages[out_messages_index] = plainTextEdit_INPUT->toPlainText();

    if (out_messages_index >= 1)
        out_messages_index--;

    plainTextEdit_INPUT->setPlainText(out_messages.at(out_messages_index));
}

void PMWindow::slotHub(){
    HubFrame *fr = qobject_cast<HubFrame*>(qtCtx()->hubManager()->getHub(hubUrl));

    if (fr)
        qtCtx()->arenaWidgetManager()->activate(fr);
}

void PMWindow::slotShare(){
    string cid = this->cid.toStdString();

    if (!cid.empty()){
        try{
            UserPtr user = qtCtx()->dcCtx().getClientManager()->findUser(CID(cid));

            if (user){
                if (user == qtCtx()->dcCtx().getClientManager()->getMe())
                    qtCtx()->mainWindow()->browseOwnFiles();
                else
                    qtCtx()->dcCtx().getQueueManager()->addList(HintedUser(user, _tq(hubUrl)), QueueItem::FLAG_CLIENT_VIEW, "");
            }
        }
        catch (const Exception &e){}
    }
}

void PMWindow::slotSmile(){
    if (emojiDialog_ && emojiDialog_->isVisible()) {
        emojiDialog_->close();
        return;
    }

    if (!emojiDialog_) {
        emojiDialog_ = new EmoticonDialog(this, Qt::Tool);
        emojiDialog_->setWindowModality(Qt::NonModal);
        emojiDialog_->setAttribute(Qt::WA_DeleteOnClose, false);

        connect(emojiDialog_, &QDialog::accepted, this, [this]() {
            if (emojiDialog_ && !emojiDialog_->getEmoticonText().isEmpty())
                plainTextEdit_INPUT->insertEmoji(emojiDialog_->getEmoticonText());
        });
        connect(emojiDialog_, &QObject::destroyed, this, [this]() {
            emojiDialog_ = nullptr;
        });
    }

    const QPoint smileButtonPos = toolButton_SMILE->mapToGlobal(QPoint(0, 0));
    QScreen *screen = QApplication::screenAt(smileButtonPos);
    if (!screen)
        screen = QApplication::primaryScreen();

    const QRect screenGeo = screen ? screen->availableGeometry() : QRect();

    const int verticalGap = 6;
    const int chatWidth = textEdit_CHAT && textEdit_CHAT->viewport() ? textEdit_CHAT->viewport()->width() : width();
    const int preferredWidth = qMax(420, chatWidth - 2);
    const int maxDialogWidth = screenGeo.isValid() ? qMax(520, screenGeo.width() - 24) : 1400;
    const int maxDialogHeight = screenGeo.isValid() ? qMax(260, screenGeo.height() - 24) : 620;
    emojiDialog_->preparePopupGeometry(preferredWidth, maxDialogWidth, maxDialogHeight);

    int dialogWidth = emojiDialog_->width();
    int dialogHeight = emojiDialog_->height();
    if (dialogWidth > preferredWidth) {
        emojiDialog_->resize(preferredWidth, dialogHeight);
        dialogWidth = emojiDialog_->width();
        dialogHeight = emojiDialog_->height();
    }

    if (screenGeo.isValid()) {
        const int bbcodeTopY = toolButton_BOLD->mapToGlobal(QPoint(0, 0)).y();
        const int availableAbove = bbcodeTopY - verticalGap - screenGeo.top();
        if (availableAbove > 120 && dialogHeight > availableAbove) {
            emojiDialog_->resize(dialogWidth, availableAbove);
            dialogHeight = emojiDialog_->height();
        }
    }

    const QPoint chatTopLeft = (textEdit_CHAT && textEdit_CHAT->viewport())
        ? textEdit_CHAT->viewport()->mapToGlobal(QPoint(0, 0))
        : mapToGlobal(QPoint(0, 0));
    int dialogX = chatTopLeft.x();
    const int bbcodeTopY = toolButton_BOLD->mapToGlobal(QPoint(0, 0)).y();
    int dialogY = bbcodeTopY - dialogHeight - verticalGap;

    if (screenGeo.isValid()) {
        if (dialogX < screenGeo.left())
            dialogX = screenGeo.left();
        if (dialogX + dialogWidth > screenGeo.right())
            dialogX = screenGeo.right() - dialogWidth;

        if (dialogY < screenGeo.top())
            dialogY = screenGeo.top();

        // Keep popup strictly above the BBCode panel.
        const int maxBottom = bbcodeTopY - verticalGap;
        if (dialogY + dialogHeight > maxBottom)
            dialogY = qMax(screenGeo.top(), maxBottom - dialogHeight);
    }

    emojiDialog_->move(dialogX, dialogY);
    emojiDialog_->show();
    emojiDialog_->raise();
    emojiDialog_->activateWindow();
}

void PMWindow::slotSmileClicked(){
    slotSmile();
}


void PMWindow::slotSmileContextMenu(){
    slotSmile();
}

void PMWindow::slotSettingChanged(const QString &key, const QString &value){
    Q_UNUSED(value);

    if (key == WS_CHAT_PM_FONT)
        updateStyles();
    else if (key == "hubframe/chat-background-color" || key == "hubframe/change-chat-background-color")
        reloadSomeSettings();
    else if (key == WS_TRANSLATION_FILE)
        retranslateUi(this);
}

void PMWindow::slotShowSearchBar(){
    frame_2->setVisible(true);
    QString stext = textEdit_CHAT->textCursor().selectedText();
    if (!stext.isEmpty())
        lineEdit_FIND->setText(stext);
    lineEdit_FIND->selectAll();
    lineEdit_FIND->setFocus();
}

void PMWindow::slotHideSearchBar(){
    frame_2->setVisible(false);
    QTextCursor c = textEdit_CHAT->textCursor();
    c.movePosition(QTextCursor::StartOfLine,QTextCursor::MoveAnchor,1);
    textEdit_CHAT->setExtraSelections(QList<QTextEdit::ExtraSelection>());
    textEdit_CHAT->setTextCursor(c);
}

void PMWindow::slotFindTextEdited(const QString & text){
    if (text.isEmpty()){
        textEdit_CHAT->verticalScrollBar()->setValue(textEdit_CHAT->verticalScrollBar()->maximum());
        textEdit_CHAT->textCursor().movePosition(QTextCursor::End, QTextCursor::MoveAnchor, 1);

        return;
    }

    QTextCursor c = textEdit_CHAT->textCursor();

    c.movePosition(QTextCursor::StartOfLine,QTextCursor::MoveAnchor,1);
    c = textEdit_CHAT->document()->find(lineEdit_FIND->text(), c, QTextDocument::FindFlags());
    if (!c.isNull()) {
        textEdit_CHAT->setExtraSelections(QList<QTextEdit::ExtraSelection>());
        textEdit_CHAT->setTextCursor(c);
        slotFindAll();
    }
}

void PMWindow::slotFindAll(){
    if (!toolButton_ALL->isChecked()){
        textEdit_CHAT->setExtraSelections(QList<QTextEdit::ExtraSelection>());

        return;
    }

    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!lineEdit_FIND->text().isEmpty()) {
        QTextEdit::ExtraSelection selection;

        QColor color;
        color = QColor::fromString(qtCtx()->settings()->getStr(WS_CHAT_FIND_COLOR));
        color.setAlpha(qtCtx()->settings()->getInt(WI_CHAT_FIND_COLOR_ALPHA));

        selection.format.setBackground(color);

        QTextCursor c = textEdit_CHAT->document()->find(lineEdit_FIND->text(), 0, QTextDocument::FindFlags());

        while (!c.isNull()){
            selection.cursor = c;
            extraSelections.append(selection);

            c = textEdit_CHAT->document()->find(lineEdit_FIND->text(), c, QTextDocument::FindFlags());
        }
    }

    textEdit_CHAT->setExtraSelections(extraSelections);
}

void PMWindow::findText(QTextDocument::FindFlags flag){
    textEdit_CHAT->setExtraSelections(QList<QTextEdit::ExtraSelection>());

    if (lineEdit_FIND->text().isEmpty())
        return;

    QTextCursor c = textEdit_CHAT->textCursor();

    bool ok = textEdit_CHAT->find(lineEdit_FIND->text(), flag);

    if (flag == QTextDocument::FindBackward && !ok)
        c.movePosition(QTextCursor::End,QTextCursor::MoveAnchor,1);
    else if (!flag && !ok)
        c.movePosition(QTextCursor::Start,QTextCursor::MoveAnchor,1);

    c = textEdit_CHAT->document()->find(lineEdit_FIND->text(), c, flag);

    if (!c.isNull()) {
        textEdit_CHAT->setTextCursor(c);
        slotFindAll();
    }
}
