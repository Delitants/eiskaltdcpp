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

#include "ChatEdit.h"
#include "QtContextAware.h"
#include "QtContext.h"
#include "WulforUtil.h"

#include "dcpp/HashManager.h"
#include "dcpp/File.h"
#include "dcpp/MerkleTree.h"
#include "dcpp/ShareManager.h"
#include "dcpp/Util.h"
#include "dcpp/DCPlusPlus.h"

#include <QCompleter>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QRegularExpression>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QColorDialog>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QSet>
#include <QStandardPaths>

ChatEdit::ChatEdit(QWidget *parent) : QTextEdit(parent), cc(nullptr)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    document()->setDocumentMargin(10);

    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    applyContrastStyle();

    connect(this, &QTextEdit::textChanged, this, &ChatEdit::recalculateGeometry);
}

ChatEdit::~ChatEdit()
{}

void ChatEdit::applyContrastStyle()
{
    const QPalette pal = palette();
    const bool darkAppearance = (pal.color(QPalette::Window).lightness() + pal.color(QPalette::Base).lightness()) / 2 < 128;
    QColor borderColor = darkAppearance ? pal.color(QPalette::Window).lighter(165)
                                        : pal.color(QPalette::Window).darker(135);
    if (qAbs(borderColor.lightness() - pal.color(QPalette::Base).lightness()) < 22) {
        const QColor textColor = pal.color(QPalette::Text);
        borderColor = darkAppearance ? textColor.lighter(145) : textColor.darker(150);
    }
    QColor focusBorder = pal.color(QPalette::Highlight);
    if (darkAppearance && focusBorder.lightness() < 150)
        focusBorder = focusBorder.lighter(140);
    else if (!darkAppearance && focusBorder.lightness() > 205)
        focusBorder = focusBorder.darker(118);
    const QColor disabledBorder = darkAppearance ? borderColor.darker(118) : borderColor.lighter(112);

    setStyleSheet(QStringLiteral(
        "QTextEdit {"
        "    background: palette(base);"
        "    color: palette(text);"
        "    border: 1px solid %1;"
        "    border-radius: 7px;"
        "    padding: 12px;"
        "}"
        "QTextEdit:focus {"
        "    border: 1px solid %2;"
        "}"
        "QTextEdit:disabled {"
        "    border: 1px solid %3;"
        "    color: palette(mid);"
        "}"
    ).arg(borderColor.name(), focusBorder.name(), disabledBorder.name()));
}

void ChatEdit::changeEvent(QEvent *event)
{
    QTextEdit::changeEvent(event);

    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        applyContrastStyle();
    }
}

QString ChatEdit::defaultChatPictureDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("EiskaltDC++/chat-pictures"));
}

QString ChatEdit::configuredChatPictureDir()
{
    const QString configured = qtCtx()->settings()->getStr(WS_CHAT_PICTURE_DIR).trimmed();
    return configured.isEmpty() ? defaultChatPictureDir() : QDir::fromNativeSeparators(configured);
}

QString ChatEdit::ensureChatPictureDir()
{
    const QString dirPath = configuredChatPictureDir();
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
    return dir.absolutePath();
}

void ChatEdit::cleanupChatPictureStore(const QString &dir)
{
    if (!qtCtx()->settings()->getBool(WB_CHAT_PICTURE_AUTOCLEAN, true))
        return;

    const QString baseDir = dir.isEmpty() ? ensureChatPictureDir() : dir;
    const int maxAgeDays = qMax(1, qtCtx()->settings()->getInt(WI_CHAT_PICTURE_CLEAN_DAYS, 7));
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays);
    const QFileInfoList files = QDir(baseDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    for (const QFileInfo &info : files) {
        if (info.lastModified().isValid() && info.lastModified() < cutoff)
            QFile::remove(info.absoluteFilePath());
    }
}

bool ChatEdit::isSupportedChatImageFile(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().trimmed().toLower();
    if (suffix.isEmpty())
        return false;

    static QSet<QString> supported;
    if (supported.isEmpty()) {
        const auto formats = QImageReader::supportedImageFormats();
        for (const QByteArray &format : formats)
            supported.insert(QString::fromLatin1(format).toLower());
        supported.insert(QStringLiteral("heic"));
        supported.insert(QStringLiteral("heif"));
    }

    return supported.contains(suffix);
}

QString ChatEdit::sanitizeChatPictureBaseName(const QString &name)
{
    QString base = QFileInfo(name).completeBaseName().trimmed();
    if (base.isEmpty())
        base = QStringLiteral("image");

    base.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}._ -]+")), QStringLiteral("_"));
    base = base.simplified().replace(' ', '_');
    return base.isEmpty() ? QStringLiteral("image") : base;
}

QString ChatEdit::normalizedTTH(QString tth)
{
    static const QString prefix = QStringLiteral("urn:tree:tiger:");
    tth = tth.trimmed();
    if (tth.startsWith(prefix, Qt::CaseInsensitive))
        tth.remove(0, prefix.length());
    return tth;
}

QString ChatEdit::picturePathForMagnet(const QString &displayName, const QString &tth)
{
    const QString dir = ensureChatPictureDir();
    const QString cleanTth = normalizedTTH(tth);
    const QString suffix = QFileInfo(displayName).suffix().trimmed();
    QString fileName = sanitizeChatPictureBaseName(displayName);
    if (!cleanTth.isEmpty())
        fileName += QStringLiteral("-") + cleanTth.left(12).toLower();
    if (!suffix.isEmpty())
        fileName += QStringLiteral(".") + suffix;
    return QDir(dir).filePath(fileName);
}

QString ChatEdit::uniquePathInDirectory(const QString &dir, const QString &fileName)
{
    const QFileInfo info(fileName);
    const QString base = sanitizeChatPictureBaseName(info.fileName());
    const QString suffix = info.completeSuffix();
    QString candidate = QDir(dir).filePath(base + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));

    for (int index = 2; QFileInfo::exists(candidate); ++index) {
        const QString numbered = QStringLiteral("%1-%2").arg(base).arg(index);
        candidate = QDir(dir).filePath(numbered + (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix));
    }

    return candidate;
}

QString ChatEdit::computeFileTTH(const QString &path, int64_t *sizeOut)
{
    static const quint64 minBlockSize = 64 * 1024;
    static const size_t bufferSize = 64 * 1024;

    if (sizeOut)
        *sizeOut = 0;

    QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return QString();

    const int64_t size = info.size();
    if (sizeOut)
        *sizeOut = size;

    std::unique_ptr<char[]> buf(new char[bufferSize]);

    try {
        dcpp::File file(_tq(path), dcpp::File::READ, dcpp::File::OPEN);
        TigerTree tree(std::max(TigerTree::calcBlockSize(file.getSize(), 10), static_cast<int64_t>(minBlockSize)));

        if (file.getSize() > 0) {
            size_t readBytes = bufferSize;
            while ((readBytes = file.read(buf.get(), readBytes)) > 0) {
                tree.update(buf.get(), readBytes);
                readBytes = bufferSize;
            }
        } else {
            tree.update("", 0);
        }

        tree.finalize();
        qtCtx()->dcCtx().getHashManager()->addTree(_tq(path), static_cast<uint32_t>(info.lastModified().toSecsSinceEpoch()), tree);
        return _q(tree.getRoot().toBase32());
    } catch (...) {
        return QString();
    }
}

void ChatEdit::ensureChatPictureShared(const QString &dir)
{
    auto *shareManager = qtCtx()->dcCtx().getShareManager();
    const QString cleanDir = QDir(dir).canonicalPath().isEmpty() ? QDir(dir).absolutePath() : QDir(dir).canonicalPath();
    const auto directories = shareManager->getDirectories();

    for (const auto &pair : directories) {
        const QString sharedPath = QDir(QString::fromStdString(pair.first)).canonicalPath().isEmpty()
            ? QDir(QString::fromStdString(pair.first)).absolutePath()
            : QDir(QString::fromStdString(pair.first)).canonicalPath();
        if (sharedPath == cleanDir)
            return;
    }

    std::string alias = "ChatPictures";
    int suffix = 2;
    while (shareManager->hasVirtual(alias))
        alias = "ChatPictures" + dcpp::Util::toString(suffix++);

    try {
        shareManager->addDirectory(_tq(cleanDir), alias);
    } catch (...) {
    }
}

QString ChatEdit::buildImageNameFilter()
{
    QStringList patterns;
    const auto formats = QImageReader::supportedImageFormats();
    for (const QByteArray &format : formats)
        patterns << QStringLiteral("*.") + QString::fromLatin1(format).toLower();
    patterns << QStringLiteral("*.heic") << QStringLiteral("*.heif");
    patterns.removeDuplicates();
    patterns.sort();

    return QObject::tr("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}

QString ChatEdit::buildManagedImageMagnet(const QString &sourcePath)
{
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile() || !isSupportedChatImageFile(sourceInfo.fileName()))
        return QString();

    const QString dir = ensureChatPictureDir();
    const QString originalName = sourceInfo.fileName();
    cleanupChatPictureStore(dir);
    ensureChatPictureShared(dir);

    QString targetPath = sourcePath;
    const QString cleanDir = QDir(dir).canonicalPath().isEmpty() ? QDir(dir).absolutePath() : QDir(dir).canonicalPath();
    const QString sourceDir = sourceInfo.dir().canonicalPath().isEmpty() ? sourceInfo.dir().absolutePath() : sourceInfo.dir().canonicalPath();

    if (sourceDir != cleanDir) {
        targetPath = uniquePathInDirectory(dir, sourceInfo.fileName());
        QFile::remove(targetPath);
        if (!QFile::copy(sourcePath, targetPath))
            return QString();
        QFile(targetPath).setPermissions(sourceInfo.permissions());
    }

    int64_t size = 0;
    const QString tth = computeFileTTH(targetPath, &size);
    if (tth.isEmpty())
        return QString();

    const QString finalPath = picturePathForMagnet(originalName, tth);
    if (QFileInfo(targetPath).absoluteFilePath() != QFileInfo(finalPath).absoluteFilePath()) {
        if (QFileInfo::exists(finalPath))
            QFile::remove(finalPath);

        if (!QFile::rename(targetPath, finalPath)) {
            QFile::remove(finalPath);
            if (!QFile::copy(targetPath, finalPath))
                return QString();
            QFile::remove(targetPath);
        }

        targetPath = finalPath;
    }

    qtCtx()->dcCtx().getShareManager()->setDirty();
    qtCtx()->dcCtx().getShareManager()->refresh(true, true, true);
    return qtCtx()->wulforUtil()->makeMagnet(originalName, size, tth);
}

void ChatEdit::wrapSelection(const QString &prefix, const QString &suffix, const QString &placeholder)
{
    QTextCursor cursor = textCursor();
    QString selected = cursor.selectedText();
    selected.replace(QChar::ParagraphSeparator, '\n');

    const bool hadSelection = cursor.hasSelection();
    const QString content = hadSelection ? selected : placeholder;
    cursor.insertText(prefix + content + suffix);

    if (!hadSelection) {
        const int endPos = cursor.position() - suffix.length();
        cursor.setPosition(endPos - content.length());
        if (!content.isEmpty())
            cursor.setPosition(endPos, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
    } else {
        setTextCursor(cursor);
    }
}

void ChatEdit::wrapWithTag(const QString &tag, const QString &placeholder)
{
    wrapSelection("[" + tag + "]", "[/" + tag + "]", placeholder);
}

void ChatEdit::insertUrlTag()
{
    QTextCursor cursor = textCursor();
    QString selected = cursor.selectedText();
    selected.replace(QChar::ParagraphSeparator, '\n');

    const QString initialUrl = QUrl(selected).isValid() ? selected : QString();
    const QString url = QInputDialog::getText(this, tr("Link"), tr("Address"), QLineEdit::Normal, initialUrl);
    if (url.trimmed().isEmpty())
        return;

    if (selected.isEmpty())
        wrapSelection("[url=" + url.trimmed() + "]", "[/url]", url.trimmed());
    else
        wrapSelection("[url=" + url.trimmed() + "]", "[/url]", selected);
}

void ChatEdit::insertColorTag(const QColor &initialColor)
{
    const QColor color = QColorDialog::getColor(initialColor.isValid() ? initialColor : QColor(), this);
    if (!color.isValid())
        return;

    wrapSelection("[color=" + color.name() + "]", "[/color]");
}

void ChatEdit::insertImageMagnet()
{
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Select image"),
                                                          ensureChatPictureDir(),
                                                          buildImageNameFilter());
    if (filePath.isEmpty())
        return;

    const QString magnet = buildManagedImageMagnet(filePath);
    if (magnet.isEmpty())
        return;

    const QString displayName = QFileInfo(filePath).fileName();
    textCursor().insertText(QStringLiteral("[magnet=\"%1\"]%2[/magnet]").arg(magnet, displayName));
}

void ChatEdit::insertEmoji(const QString &emoji)
{
    if (emoji.isEmpty())
        return;

    textCursor().insertText(emoji + QStringLiteral(" "));
    setFocus();
}

void ChatEdit::populateEmojiMenu(QMenu *menu)
{
    if (!menu)
        return;

    static const QStringList emojis = {
        QString::fromUtf8("😀"), QString::fromUtf8("😄"), QString::fromUtf8("😉"), QString::fromUtf8("😍"),
        QString::fromUtf8("😎"), QString::fromUtf8("🤔"), QString::fromUtf8("😅"), QString::fromUtf8("😂"),
        QString::fromUtf8("😢"), QString::fromUtf8("😭"), QString::fromUtf8("😡"), QString::fromUtf8("😮"),
        QString::fromUtf8("👍"), QString::fromUtf8("👎"), QString::fromUtf8("🙏"), QString::fromUtf8("🔥"),
        QString::fromUtf8("🎉"), QString::fromUtf8("❤️"), QString::fromUtf8("✅"), QString::fromUtf8("🔒")
    };

    constexpr int kColumns = 5;
    for (int index = 0; index < emojis.size(); ++index) {
        QAction *action = menu->addAction(emojis.at(index));
        action->setData(emojis.at(index));
        connect(action, &QAction::triggered, this, [this, action]() {
            insertEmoji(action->data().toString());
        });

        if ((index + 1) % kColumns == 0 && index + 1 < emojis.size())
            menu->addSeparator();
    }
}

void ChatEdit::populateBBCodeMenu(QMenu *menu)
{
    if (!menu)
        return;

    QAction *bold = menu->addAction(QStringLiteral("B"));
    QFont boldFont = bold->font();
    boldFont.setBold(true);
    bold->setFont(boldFont);
    bold->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(bold, &QAction::triggered, this, [this]() { wrapWithTag("b"); });

    QAction *italic = menu->addAction(QStringLiteral("I"));
    QFont italicFont = italic->font();
    italicFont.setItalic(true);
    italic->setFont(italicFont);
    italic->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(italic, &QAction::triggered, this, [this]() { wrapWithTag("i"); });

    QAction *underline = menu->addAction(QStringLiteral("U"));
    QFont underlineFont = underline->font();
    underlineFont.setUnderline(true);
    underline->setFont(underlineFont);
    underline->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    connect(underline, &QAction::triggered, this, [this]() { wrapWithTag("u"); });

    QAction *strike = menu->addAction(QStringLiteral("S"));
    QFont strikeFont = strike->font();
    strikeFont.setStrikeOut(true);
    strike->setFont(strikeFont);
    connect(strike, &QAction::triggered, this, [this]() { wrapWithTag("s"); });

    menu->addSeparator();

    QAction *color = menu->addAction(tr("Color"));
    connect(color, &QAction::triggered, this, [this]() { insertColorTag(); });

    QAction *link = menu->addAction(tr("Link"));
    connect(link, &QAction::triggered, this, [this]() { insertUrlTag(); });

    QAction *code = menu->addAction(tr("Code"));
    connect(code, &QAction::triggered, this, [this]() { wrapWithTag("code"); });

    QAction *image = menu->addAction(tr("Image"));
    connect(image, &QAction::triggered, this, [this]() { insertImageMagnet(); });
}

void ChatEdit::setCompleter(QCompleter *completer, UserListModel *model)
{
    if (cc)
        QObject::disconnect(cc, nullptr, this, nullptr);

    cc = completer;

    if (!cc || !model)
        return;

    cc->setWidget(this);
    cc->setWrapAround(false);
    cc->setCaseSensitivity(Qt::CaseInsensitive);
    cc->setCompletionMode(QCompleter::PopupCompletion);

    cc_model = model;

    QObject::connect(cc, qOverload<const QModelIndex &>(&QCompleter::activated),
                     this, &ChatEdit::insertCompletion);
}

QSize ChatEdit::minimumSizeHint() const{
    QSize sh = QTextEdit::minimumSizeHint();
    const int frame = frameWidth() * 2;
    const int docMargin = int(document()->documentMargin() * 2);
    sh.setHeight(fontMetrics().height() + docMargin + frame + 2);
    return sh;
}

QSize ChatEdit::sizeHint() const{
    QSize sh = QTextEdit::sizeHint();
    const int frame = frameWidth() * 2;
    const int docMargin = int(document()->documentMargin() * 2);
    const int docHeight = int(document()->documentLayout()->documentSize().height());
    sh.setHeight(qMax(fontMetrics().height() + docMargin, docHeight + docMargin) + frame + 2);
    return sh;
}

void ChatEdit::insertCompletion(const QModelIndex & index)
{
    if (cc->widget() != this || !index.isValid())
        return;

    QString nick = cc->completionModel()->index(index.row(), index.column()).data().toString();
    int begin = textCursor().position() - cc->completionPrefix().length();

    insertToPos(nick, begin);
}

void ChatEdit::insertToPos(const QString & completeText, int begin)
{
    if (completeText.isEmpty())
        return;

    if (begin < 0)
        begin = 0;

    QTextCursor cursor = textCursor();
    int end = cursor.position();
    cursor.setPosition(begin);
    cursor.setPosition(end, QTextCursor::KeepAnchor);

    if (!begin)
        cursor.insertText(completeText + ": ");
    else
        cursor.insertText(completeText + " ");

    setTextCursor(cursor);
}

QString ChatEdit::textUnderCursor() const
{
    QTextCursor cursor = textCursor();

    int curpos = cursor.position();
    QString text = cursor.block().text().left(curpos);

    QStringList wordList = text.split(QRegularExpression("\\s"));

    if (wordList.isEmpty())
        return QString();

    return wordList.last();
}

void ChatEdit::focusInEvent(QFocusEvent *e)
{
    if (cc)
        cc->setWidget(this);

    QTextEdit::focusInEvent(e);
}

void ChatEdit::keyPressEvent(QKeyEvent *e)
{
    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    bool hasModifier = (e->modifiers() != Qt::NoModifier) &&
                       (e->modifiers() != Qt::KeypadModifier) &&
                       !ctrlOrShift;

    if (e->key() == Qt::Key_Tab) {
        if (!toPlainText().isEmpty()) {
            if (cc && cc->popup()->isVisible()) {
                int row = cc->popup()->currentIndex().row() + 1;
                if (cc->completionModel()->rowCount() == row)
                    row = 0;
                cc->popup()->setCurrentIndex(cc->completionModel()->index(row, 0));
            }
            e->accept();
        } else {
            e->ignore();
        }
        return;
    }

    if (e->modifiers() == Qt::ControlModifier) {
        if (e->key() == Qt::Key_B) {
            wrapWithTag("b");
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_I) {
            wrapWithTag("i");
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_U) {
            wrapWithTag("u");
            e->accept();
            return;
        }
    }

    if (cc && cc->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        default:
            break;
        }
    }

    if (!cc || !cc->popup()->isVisible() || !hasModifier)
        QTextEdit::keyPressEvent(e);

    if (ctrlOrShift && e->text().isEmpty())
        return;

    if (cc->popup()->isVisible() && (hasModifier || e->text().isEmpty())) {
        cc->popup()->hide();
        return;
    }

    if (cc->popup()->isVisible())
        complete();
}

void ChatEdit::keyReleaseEvent(QKeyEvent *e)
{
    bool hasModifier = (e->modifiers() != Qt::NoModifier);

    switch (e->key()) {
    case Qt::Key_Tab:
        if (cc && !hasModifier && !cc->popup()->isVisible())
            complete();

    case Qt::Key_Enter:
    case Qt::Key_Return:
        e->ignore();
        return;
    default:
        break;
    }
}

void ChatEdit::complete()
{
    QString completionPrefix = textUnderCursor();

    if (completionPrefix.isEmpty()) {
        if (cc->popup()->isVisible())
            cc->popup()->hide();

        return;
    }

    if (!cc->popup()->isVisible() || completionPrefix.length() < cc->completionPrefix().length()) {
        QString pattern = QString("(\\[.*\\])?%1.*").arg( QRegularExpression::escape(completionPrefix) );
        QStringList nicks = cc_model->findItems(pattern, Qt::MatchRegularExpression, 0);

        if (nicks.isEmpty())
            return;

        if (nicks.count() == 1) {
            insertToPos(nicks.last(), textCursor().position() - completionPrefix.length());
            return;
        }

        NickCompletionModel *tmpModel = new NickCompletionModel(nicks, cc);
        cc->setModel(tmpModel);
    }

    if (completionPrefix != cc->completionPrefix()) {
        cc->setCompletionPrefix(completionPrefix);
        cc->popup()->setCurrentIndex(cc->completionModel()->index(0, 0));
    }

    QRect cr = cursorRect();
    cr.setWidth(cc->popup()->sizeHintForColumn(0)
                + cc->popup()->verticalScrollBar()->sizeHint().width());

    cc->complete(cr);
}

void ChatEdit::dragMoveEvent(QDragMoveEvent *event) {
    event->accept();
}

void ChatEdit::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls() || e->mimeData()->hasText()) {
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}

void ChatEdit::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasUrls()) {

        e->setDropAction(Qt::IgnoreAction);

        QStringList fileNames;
        for (const auto url : e->mimeData()->urls()) {
            QString urlStr = url.toString();
            if (url.scheme().toLower() == "file") {
                QFileInfo fi( url.toLocalFile() );
                QString str = QDir::toNativeSeparators( fi.absoluteFilePath() );

                if ( fi.exists() && fi.isFile() && !str.isEmpty() ) {
                    if (isSupportedChatImageFile(fi.fileName())) {
                        const QString magnet = buildManagedImageMagnet(str);
                        if (!magnet.isEmpty())
                            urlStr = QStringLiteral("[magnet=\"%1\"]%2[/magnet]").arg(magnet, fi.fileName());
                    } else {
                        const TTHValue *tth = qtCtx()->dcCtx().getHashManager()->getFileTTHif(str.toStdString());
                        if ( !tth ) {
                            str = QDir::toNativeSeparators( fi.canonicalFilePath() ); // try to follow symlinks
                            tth = qtCtx()->dcCtx().getHashManager()->getFileTTHif(str.toStdString());
                        }
                        if (tth)
                            urlStr = qtCtx()->wulforUtil()->makeMagnet(fi.fileName(), fi.size(), _q(tth->toBase32()));
                    }
                }
            };

            if (!urlStr.isEmpty())
                fileNames << urlStr;
        }

        if (!fileNames.isEmpty()) {

            QString dropText = (fileNames.count() == 1) ? fileNames.last() : "\n" + fileNames.join("\n");

            QMimeData mime;
            mime.setText(dropText);
            QDropEvent drop(e->position().toPoint(), Qt::CopyAction, &mime, e->buttons(),
                            e->modifiers(), e->type());

            QTextEdit::dropEvent(&drop);
            return;
        }
    }
    QTextEdit::dropEvent(e);
}

void ChatEdit::updateScrollBar(){
    setVerticalScrollBarPolicy(sizeHint().height() > height() ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
    ensureCursorVisible();
}
