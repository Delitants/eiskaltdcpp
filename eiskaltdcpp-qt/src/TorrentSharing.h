#pragma once
#include "torrent/TorrentTypes.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextEdit>
#include <QUrl>

namespace torrent_sharing {
struct FileMagnet {
    QString jobId, path, magnet;
    quint64 revision = 0;
    int index = -1;
    qint64 size = 0;
};

template<class Lookup> QList<FileMagnet> dcFiles(const eiskalt::torrent::Job &job, Lookup lookup)
{
    QList<FileMagnet> result;
    if (!job.complete || job.privateTorrent || job.dcShareExcluded || !job.error.isEmpty()) return result;
    static const QRegularExpression validTth(QStringLiteral("^[A-Z2-7]{39}$"));
    for (const auto &file : job.files) {
        if (!file.wanted || file.size < 0) continue;
        const QString tth = lookup(file);
        if (!validTth.match(tth).hasMatch()) continue;
        const auto name = QFileInfo(file.path).fileName();
        if (name.isEmpty()) continue;
        const auto magnet = QStringLiteral("magnet:?xt=urn:tree:tiger:%1&xl=%2&dn=%3")
            .arg(tth).arg(file.size).arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
        result.append({job.id, file.path, magnet, job.revision, file.index, file.size});
    }
    return result;
}

inline bool appendDraft(QTextEdit *editor, const QStringList &magnets)
{
    if (!editor || magnets.isEmpty()) return false;
    auto cursor = editor->textCursor();
    cursor.clearSelection();
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    const auto previous = editor->toPlainText();
    if (!previous.isEmpty() && !previous.endsWith('\n')) cursor.insertText("\n");
    cursor.insertText(magnets.join('\n'));
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
    return true;
}
}
