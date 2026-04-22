/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#pragma once

#include <QTextEdit>
#include <QAbstractTextDocumentLayout>
#include <QStringListModel>
#include <QMenu>
#include <QColor>

#include <QDebug>

#include "UserListModel.h"

class QCompleter;

class NickCompletionModel: public QStringListModel
{
    Q_OBJECT

public:
    NickCompletionModel(QObject *parent = nullptr) : QStringListModel(parent)
    {}
    NickCompletionModel(const QStringList &strings, QObject *parent = nullptr) : QStringListModel(strings, parent)
    {}
    virtual ~NickCompletionModel()
    { }

    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const
    {
        QVariant result = QStringListModel::data(index, role);

        if (result.isValid() && role == Qt::EditRole) {
            int end = -1;
            QString nick = result.toString();
            if (nick.startsWith("[") && (end = nick.lastIndexOf("]")) > 0 && nick.length() > ++end) {
                nick.remove(0, end);
                result = nick;
            }

        }
        return result;
    }
};

class ChatEdit : public QTextEdit
{
    Q_OBJECT

public:
    ChatEdit(QWidget *parent = nullptr);
    virtual ~ChatEdit();

    void setCompleter(QCompleter *, UserListModel *);
    void wrapSelection(const QString &prefix, const QString &suffix, const QString &placeholder = QString());
    void wrapWithTag(const QString &tag, const QString &placeholder = QString());
    void insertUrlTag();
    void insertColorTag(const QColor &color = QColor());
    void insertImageMagnet();
    void insertEmoji(const QString &emoji);
    void populateEmojiMenu(QMenu *menu);
    void populateBBCodeMenu(QMenu *menu);

    static QString defaultChatPictureDir();
    static QString configuredChatPictureDir();
    static QString ensureChatPictureDir();
    static void cleanupChatPictureStore(const QString &dir = QString());
    static bool isSupportedChatImageFile(const QString &fileName);
    static QString picturePathForMagnet(const QString &displayName, const QString &tth);

    QSize minimumSizeHint() const;
    QSize sizeHint() const;

protected:
    void keyPressEvent(QKeyEvent *);
    void keyReleaseEvent(QKeyEvent *);
    void changeEvent(QEvent *) override;
    void focusInEvent(QFocusEvent *);
    void dropEvent(QDropEvent *);
    void dragEnterEvent(QDragEnterEvent *e);
    void dragMoveEvent(QDragMoveEvent *event); // Required to accept drops on win32

private Q_SLOTS:
    void insertCompletion(const QModelIndex &);
    void recalculateGeometry() { updateGeometry(); updateScrollBar(); }
    void updateScrollBar();

private:
    void applyContrastStyle();
    QString textUnderCursor() const;
    void insertToPos(const QString &, int);
    void complete();
    static QString buildManagedImageMagnet(const QString &sourcePath);
    static QString buildImageNameFilter();
    static QString sanitizeChatPictureBaseName(const QString &name);
    static QString normalizedTTH(QString tth);
    static QString uniquePathInDirectory(const QString &dir, const QString &fileName);
    static QString computeFileTTH(const QString &path, int64_t *sizeOut = nullptr);
    static void ensureChatPictureShared(const QString &dir);

    UserListModel *cc_model;
    QCompleter *cc;
};
