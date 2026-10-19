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
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QTextFragment>
#include <algorithm>

namespace {
// Only these semantic properties are serialized. Anchor decoration is visual only.
enum BBProperty {
    BBBold = QTextFormat::UserProperty + 0x4340,
    BBItalic, BBUnderline, BBStrike, BBCode, BBColor, BBReference, BBTarget
};

constexpr int maxBBDepth = 64;
constexpr int maxBBAttribute = 2048;
constexpr int maxBBInput = 1024 * 1024;
const QString bbCodeMimeType = QStringLiteral("application/x-eiskaltdcpp-bbcode");

bool literalCommand(const QString &text)
{
    if (!text.startsWith('/'))
        return false;
    int end = 0;
    while (end < text.size() && !text.at(end).isSpace())
        ++end;
    return text.left(end) != "/me";
}

QTextCharFormat bbCharacterFormat(QTextDocument *document, int position)
{
    // A paragraph separator carries the following block's insertion format.
    if (document->characterAt(position) == QChar::ParagraphSeparator)
        return document->findBlock(position + 1).charFormat();
    QTextCursor cursor(document);
    cursor.setPosition(position);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}

int styleProperty(const QString &tag)
{
    if (tag == "b") return BBBold;
    if (tag == "i") return BBItalic;
    if (tag == "u") return BBUnderline;
    if (tag == "s") return BBStrike;
    if (tag == "code") return BBCode;
    return 0;
}

bool readBBTag(const QString &token, QString &tag, QString &value, bool &closing)
{
    closing = token.startsWith('/');
    const QString body = closing ? token.mid(1) : token;
    const int equals = body.indexOf('=');
    tag = equals < 0 ? body : body.left(equals);
    value = equals < 0 ? QString() : body.mid(equals + 1);
    const bool simple = styleProperty(tag) || tag == "img";
    if (closing)
        return equals < 0 && (simple || tag == "color" || tag == "url" || tag == "magnet");
    if (simple)
        return equals < 0;
    if (tag == "url" && equals < 0)
        return true;
    if (equals < 0 || value.isEmpty() || value.size() > maxBBAttribute ||
        value.contains('[') || value.contains(']') || value.contains('\n') || value.contains('\r'))
        return false;
    if (tag == "color") {
        static const QRegularExpression colorPattern(QStringLiteral("\\A(?:[a-zA-Z]+|#[0-9a-fA-F]{6})\\z"));
        return colorPattern.match(value).hasMatch() && QColor(value).isValid();
    }
    if (tag != "url" && tag != "magnet")
        return false;
    if (value.startsWith('"') && value.endsWith('"') && value.size() > 2)
        value = value.mid(1, value.size() - 2);
    return !value.trimmed().isEmpty() && !value.contains('"');
}

QTextCharFormat bbFormat(const QString &tag, const QString &value,
                         const QPalette &palette, const QFont &baseFont, bool enabled = true)
{
    QTextCharFormat format;
    const int property = styleProperty(tag);
    if (property)
        format.setProperty(property, enabled);
    if (tag == "b") format.setFontWeight(enabled ? QFont::Bold : QFont::Normal);
    else if (tag == "i") format.setFontItalic(enabled);
    else if (tag == "u") format.setFontUnderline(enabled);
    else if (tag == "s") format.setFontStrikeOut(enabled);
    else if (tag == "code") {
        format.setFontFamilies(QStringList(enabled ? QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
                                                   : baseFont.family()));
        format.setFontFixedPitch(enabled);
    } else if (tag == "color") {
        format.setProperty(BBColor, value);
        format.setForeground(QColor(value));
    } else if (tag == "url" || tag == "img" || tag == "magnet") {
        format.setProperty(BBReference, tag);
        format.setProperty(BBTarget, value);
        format.setAnchor(true);
        format.setAnchorHref(value);
        format.setForeground(palette.brush(QPalette::Link));
        format.setFontUnderline(true);
    }
    return format;
}

struct BBPair {
    int begin;
    int openEnd;
    int closeBegin = -1;
    int end = -1;
    int parent;
    QString tag;
    QString value;
    bool invalid = false;
};

QVector<BBPair> bbPairs(const QString &text, const QVector<bool> &protectedCode)
{
    QVector<BBPair> pairs;
    QVector<int> stack;
    for (int position = 0; position < text.size();) {
        if (text.at(position) != '[' || protectedCode.at(position)) {
            ++position;
            continue;
        }
        const int last = text.indexOf(']', position + 1);
        if (last < 0)
            break;
        if (last - position > maxBBAttribute + 16) {
            position = last + 1;
            continue;
        }
        bool protectedToken = false;
        for (int i = position; i <= last; ++i)
            protectedToken = protectedToken || protectedCode.at(i);
        QString tag, value;
        bool closing = false;
        if (protectedToken || !readBBTag(text.mid(position + 1, last - position - 1), tag, value, closing)) {
            position = last + 1;
            continue;
        }
        if (closing) {
            if (!stack.isEmpty() && pairs.at(stack.last()).tag == tag) {
                BBPair &pair = pairs[stack.takeLast()];
                pair.closeBegin = position;
                pair.end = last + 1;
            } else {
                // Crossed pairs cannot partly consume malformed user input.
                for (int index : stack)
                    pairs[index].invalid = true;
                stack.clear();
            }
            position = last + 1;
            continue;
        }
        if (stack.size() >= maxBBDepth)
            return {};
        BBPair pair;
        pair.begin = position;
        pair.openEnd = last + 1;
        pair.parent = stack.isEmpty() ? -1 : stack.last();
        pair.tag = tag;
        pair.value = value;
        if (tag == "code" || tag == "img") {
            const QString closingTag = "[/" + tag + "]";
            int close = text.indexOf(closingTag, pair.openEnd);
            while (close >= 0 && protectedCode.at(close))
                close = text.indexOf(closingTag, close + closingTag.size());
            if (close < 0)
                break;
            pair.closeBegin = close;
            pair.end = close + closingTag.size();
            pairs.append(pair);
            position = pair.end;
        } else {
            stack.append(pairs.size());
            pairs.append(pair);
            position = last + 1;
        }
    }
    // Parents precede children, so invalid/incomplete ancestors propagate in one pass.
    for (int i = 0; i < pairs.size(); ++i) {
        BBPair &pair = pairs[i];
        pair.invalid = pair.invalid || pair.end < 0 ||
            (pair.parent >= 0 && pairs.at(pair.parent).invalid);
    }
    return pairs;
}

struct BBMarkup {
    QString open;
    QString close;
    bool operator==(const BBMarkup &other) const { return open == other.open && close == other.close; }
};

QVector<BBMarkup> bbMarkup(const QTextCharFormat &format)
{
    QVector<BBMarkup> tags;
    const auto append = [&tags](const QString &tag, const QString &argument = QString()) {
        tags.append({"[" + tag + argument + "]", "[/" + tag + "]"});
    };
    if (format.boolProperty(BBBold)) append("b");
    if (format.boolProperty(BBItalic)) append("i");
    if (format.boolProperty(BBUnderline)) append("u");
    if (format.boolProperty(BBStrike)) append("s");
    if (format.hasProperty(BBColor)) append("color", "=" + format.stringProperty(BBColor));
    const QString reference = format.stringProperty(BBReference);
    const QString target = format.stringProperty(BBTarget);
    if (reference == "magnet") append(reference, "=\"" + target + "\"");
    else if (reference == "url") append(reference, target.isEmpty() ? QString() : "=" + target);
    else if (reference == "img") append(reference);
    if (format.boolProperty(BBCode)) append("code");
    return tags;
}
}

ChatEdit::ChatEdit(QWidget *parent) : QTextEdit(parent), cc_model(nullptr), cc(nullptr)
{
    setAcceptRichText(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    document()->setDocumentMargin(10);

    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    applyContrastStyle();

    connect(this, &QTextEdit::textChanged, this, &ChatEdit::recalculateGeometry);
    connect(this, &QTextEdit::textChanged, this, [this]() {
        if (!changingBBCode && document()->isEmpty())
            setCurrentCharFormat(QTextCharFormat());
    });
}

ChatEdit::~ChatEdit()
{}

void ChatEdit::applyContrastStyle()
{
    if (contrastStyleInProgress)
        return;

    contrastStyleInProgress = true;

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
    const int padding = property("compactInputStyle").toBool() ? 2 : 12;

    const QString contrastStyle = QStringLiteral(
        "QTextEdit {"
        "    background: palette(base);"
        "    color: palette(text);"
        "    border: 1px solid %1;"
        "    border-radius: 7px;"
        "    padding: %4px;"
        "}"
        "QTextEdit:focus {"
        "    border: 1px solid %2;"
        "}"
        "QTextEdit:disabled {"
        "    border: 1px solid %3;"
        "    color: palette(mid);"
        "}"
    ).arg(borderColor.name(), focusBorder.name(), disabledBorder.name()).arg(padding);

    if (contrastStyle != lastContrastStyle) {
        lastContrastStyle = contrastStyle;
        setStyleSheet(contrastStyle);
    }

    contrastStyleInProgress = false;
}

void ChatEdit::setCompactInputStyle(bool compact)
{
    setProperty("compactInputStyle", compact);
    document()->setDocumentMargin(compact ? 1 : 10);
    applyContrastStyle();
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

QString ChatEdit::bbCodeText() const
{
    return bbCodeRange(0, document()->characterCount() - 1);
}

QString ChatEdit::bbCodeRange(int start, int end) const
{
    const QString plain = toPlainText();
    const bool wholeDocument = start == 0 && end == plain.size();
    if (literalCommand(plain))
        return plain.mid(start, end - start);
    int commandEnd = 0;
    if (plain.startsWith('/')) {
        while (commandEnd < plain.size() && !plain.at(commandEnd).isSpace())
            ++commandEnd;
        while (commandEnd < plain.size() && plain.at(commandEnd).isSpace())
            ++commandEnd;
    }
    QString result = plain.mid(start, qMax(0, qMin(commandEnd, end) - start));
    QVector<BBMarkup> active;
    for (int position = qMax(start, commandEnd); position < end; ++position) {
        QTextCharFormat format = bbCharacterFormat(document(), position);
        if (!wholeDocument && format.stringProperty(BBReference) == "url" &&
            format.stringProperty(BBTarget).isEmpty()) {
            const QString href = format.anchorHref();
            QString tag, target;
            bool closing = false;
            if (href.size() <= maxBBAttribute && readBBTag("url=" + href, tag, target, closing) &&
                !closing && tag == "url" && target == href) {
                format.setProperty(BBTarget, href);
            } else {
                // An unrepresentable target must not become a link to the shortened label.
                format.clearProperty(BBReference);
                format.clearProperty(BBTarget);
            }
        }
        const auto desired = bbMarkup(format);
        int common = 0;
        while (common < active.size() && common < desired.size() && active.at(common) == desired.at(common))
            ++common;
        for (int i = active.size() - 1; i >= common; --i)
            result += active.at(i).close;
        for (int i = common; i < desired.size(); ++i)
            result += desired.at(i).open;
        active = desired;
        result += plain.at(position);
    }
    for (int i = active.size() - 1; i >= 0; --i)
        result += active.at(i).close;
    return result;
}

void ChatEdit::setBBCodeText(const QString &text)
{
    changingBBCode = true;
    const bool undoEnabled = document()->isUndoRedoEnabled();
    document()->setUndoRedoEnabled(false);
    setCurrentCharFormat(QTextCharFormat());
    QTextEdit::setPlainText(text);
    normalizeBBCode(false);
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    setCurrentCharFormat(QTextCharFormat());
    document()->setUndoRedoEnabled(undoEnabled);
    changingBBCode = false;
}

void ChatEdit::normalizeBBCode(bool joinPreviousEdit)
{
    const QString text = toPlainText();
    if (text.isEmpty() || text.size() > maxBBInput || literalCommand(text) ||
        !text.contains('[') || !text.contains(']'))
        return;
    QVector<bool> protectedCode(text.size(), false);
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().boolProperty(BBCode)) {
                const int last = qMin(int(text.size()), fragment.position() + fragment.length());
                for (int i = fragment.position(); i < last; ++i)
                    protectedCode[i] = true;
            }
        }
    }
    const auto pairs = bbPairs(text, protectedCode);
    QVector<QPair<int, int>> removed;
    for (const auto &pair : pairs) {
        if (!pair.invalid) {
            removed.append({pair.begin, pair.openEnd});
            removed.append({pair.closeBegin, pair.end});
        }
    }
    if (removed.isEmpty())
        return;
    std::sort(removed.begin(), removed.end());
    QString proposedText;
    proposedText.reserve(text.size());
    int retainedStart = 0;
    for (const auto &range : removed) {
        proposedText += text.mid(retainedStart, range.first - retainedStart);
        retainedStart = range.second;
    }
    proposedText += text.mid(retainedStart);
    // Formatting must never promote literal chat input into an executable command.
    if ((proposedText.startsWith('/') && !text.startsWith('/')) || literalCommand(proposedText))
        return;
    const auto mappedPosition = [&removed](int position) {
        int deleted = 0;
        for (const auto &range : removed) {
            if (position <= range.first)
                break;
            deleted += qMin(position, range.second) - range.first;
        }
        return position - deleted;
    };
    const int originalAnchor = textCursor().anchor();
    const int originalPosition = textCursor().position();
    const QTextCharFormat typingFormat = currentCharFormat();
    const bool wasChanging = changingBBCode;
    changingBBCode = true;
    QTextCursor edit(document());
    if (joinPreviousEdit)
        edit.joinPreviousEditBlock();
    else
        edit.beginEditBlock();
    for (const auto &pair : pairs) {
        if (pair.invalid || pair.openEnd == pair.closeBegin)
            continue;
        auto format = bbFormat(pair.tag, pair.value, palette(), font());
        if (pair.tag == "img" || (pair.tag == "url" && pair.value.isEmpty()))
            format.setAnchorHref(text.mid(pair.openEnd, pair.closeBegin - pair.openEnd));
        edit.setPosition(pair.openEnd);
        edit.setPosition(pair.closeBegin, QTextCursor::KeepAnchor);
        edit.mergeCharFormat(format);
    }
    for (int i = removed.size() - 1; i >= 0; --i) {
        edit.setPosition(removed.at(i).first);
        edit.setPosition(removed.at(i).second, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
    }
    edit.endEditBlock();
    QTextCursor restored(document());
    restored.setPosition(mappedPosition(originalAnchor));
    restored.setPosition(mappedPosition(originalPosition), QTextCursor::KeepAnchor);
    setTextCursor(restored);
    if (!restored.hasSelection())
        setCurrentCharFormat(document()->isEmpty() ? QTextCharFormat() : typingFormat);
    changingBBCode = wasChanging;
}

void ChatEdit::applyBBCodeFormat(const QString &tag, const QString &value, const QString &placeholder)
{
    QTextCursor cursor = textCursor();
    const bool hadSelection = cursor.hasSelection();
    const int property = styleProperty(tag);
    bool enabled = true;
    if (property) {
        bool allEnabled = currentCharFormat().boolProperty(property);
        if (hadSelection) {
            allEnabled = true;
            for (int i = cursor.selectionStart(); i < cursor.selectionEnd() && allEnabled; ++i) {
                allEnabled = bbCharacterFormat(document(), i).boolProperty(property);
            }
        }
        enabled = !allEnabled;
    }
    const auto format = bbFormat(tag, value, palette(), font(), enabled);
    if (!hadSelection && placeholder.isEmpty()) {
        QTextCharFormat typing = currentCharFormat();
        typing.merge(format);
        setCurrentCharFormat(typing);
        return;
    }
    cursor.beginEditBlock();
    if (hadSelection) {
        cursor.mergeCharFormat(format);
    } else {
        const int start = cursor.position();
        QTextCharFormat typing = currentCharFormat();
        typing.merge(format);
        cursor.insertText(placeholder, typing);
        cursor.setPosition(start, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void ChatEdit::wrapSelection(const QString &prefix, const QString &suffix, const QString &placeholder)
{
    QString tag, value;
    bool closing = false;
    if (prefix.startsWith('[') && prefix.endsWith(']') &&
        readBBTag(prefix.mid(1, prefix.size() - 2), tag, value, closing) && !closing &&
        suffix == "[/" + tag + "]") {
        applyBBCodeFormat(tag, value, placeholder);
        return;
    }

    QTextCursor cursor = textCursor();
    const bool hadSelection = cursor.hasSelection();
    QString content = hadSelection ? cursor.selectedText() : placeholder;
    content.replace(QChar::ParagraphSeparator, '\n');
    cursor.insertText(prefix + content + suffix);
    if (!hadSelection) {
        const int endPos = cursor.position() - suffix.length();
        cursor.setPosition(endPos - content.length());
        if (!content.isEmpty())
            cursor.setPosition(endPos, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
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
    const QTextCharFormat typingFormat = currentCharFormat();
    QTextCharFormat imageFormat = typingFormat;
    imageFormat.merge(bbFormat("magnet", magnet, palette(), font()));
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.insertText(displayName, imageFormat);
    cursor.endEditBlock();
    setTextCursor(cursor);
    setCurrentCharFormat(typingFormat);
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
    cc_model = model;

    if (!cc || !model)
        return;

    cc->setWidget(this);
    cc->setWrapAround(false);
    cc->setCaseSensitivity(Qt::CaseInsensitive);
    cc->setCompletionMode(QCompleter::PopupCompletion);

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
    if (!cc || cc->widget() != this || !index.isValid())
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

    if (!cc || !cc->popup()->isVisible() || !hasModifier) {
        // Qt's native typing command is not always joinable after it closes.
        // Own potential tag-closing/deletion edits; ordinary typing keeps native undo coalescence.
        const bool undoRedo = e->matches(QKeySequence::Undo) || e->matches(QKeySequence::Redo);
        const bool deletionEdit = e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace;
        const QString before = toPlainText();
        const bool conversionEdit = !undoRedo && (e->text().contains(']') || deletionEdit) &&
            before.contains('[') && !literalCommand(before) && !currentCharFormat().boolProperty(BBCode);
        const int revision = document()->revision();
        QTextCursor transaction(document());
        if (conversionEdit)
            transaction.beginEditBlock();
        QTextEdit::keyPressEvent(e);
        if (conversionEdit)
            transaction.endEditBlock();
        // Inspect the committed revision, then join the explicit block if normalization edits it.
        if (!undoRedo && document()->revision() != revision)
            normalizeBBCode();
    }

    if (!cc || (ctrlOrShift && e->text().isEmpty()))
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
    if (!cc || !cc_model)
        return;

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

QMimeData *ChatEdit::createMimeDataFromSelection() const
{
    QMimeData *mime = QTextEdit::createMimeDataFromSelection();
    const QTextCursor selection = textCursor();
    if (mime && selection.hasSelection()) {
        // Materialize Qt's lazy selection MIME before adding another advertised format.
        (void)mime->text();
        mime->setData(bbCodeMimeType, bbCodeRange(selection.selectionStart(), selection.selectionEnd()).toUtf8());
    }
    return mime;
}

void ChatEdit::insertFromMimeData(const QMimeData *source)
{
    if (!source || (!source->hasText() && !source->hasFormat(bbCodeMimeType)))
        return;
    const QString content = source->hasFormat(bbCodeMimeType)
        ? QString::fromUtf8(source->data(bbCodeMimeType)) : source->text();
    const bool wasChanging = changingBBCode;
    changingBBCode = true;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.insertText(content, currentCharFormat());
    setTextCursor(cursor);
    normalizeBBCode(false);
    cursor.endEditBlock();
    changingBBCode = wasChanging;
    if (document()->isEmpty())
        setCurrentCharFormat(QTextCharFormat());
}

void ChatEdit::inputMethodEvent(QInputMethodEvent *event)
{
    QTextEdit::inputMethodEvent(event);
    if (!event->commitString().isEmpty())
        normalizeBBCode();
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
