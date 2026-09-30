#include "RecheckJournal.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <filesystem>

namespace eiskalt::torrent {
namespace {
constexpr qint64 maxBytes=16*1024*1024;
constexpr int maxFiles=10000, maxPieces=524288;
class RecheckJournalText {
    Q_DECLARE_TR_FUNCTIONS(RecheckJournal)
};
bool fail(QString *error,const QString& message)
{
    if(error) *error=message;
    return false;
}
bool validString(const QString& value,int limit)
{
    return !value.isEmpty() && !value.contains(QChar(0)) && value.toUtf8().size()<=limit;
}
bool validIndices(const QList<int>& values,int maximum,bool allowEmpty)
{
    if(values.size()>maximum || (!allowEmpty && values.isEmpty())) return false;
    int previous=-1;
    for(const int value : values) {
        if(value<=previous || value>=maximum) return false;
        previous=value;
    }
    return true;
}
bool valid(const RecheckRecord& record)
{
    return validString(record.jobId,256) && validString(record.metadataDigest,256)
        && validString(record.storageIdentity,4096) && record.token && record.resumeGeneration
        && validIndices(record.selectedFiles,maxFiles,false) && validIndices(record.pieces,maxPieces,true);
}
QString recordPath(const QString& directory,const QString& id)
{
    const auto key=QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex();
    return QDir(directory).filePath(QString::fromLatin1(key)+".json");
}
bool validDirectory(const QString& directory,bool create,QString *error)
{
    if(directory.isEmpty())
        return fail(error,RecheckJournalText::tr("Invalid Torrent verification journal directory."));
    const QFileInfo parent(QFileInfo(directory).absolutePath()), info(directory);
    if(parent.isSymLink() || (parent.exists() && !parent.isDir()) || info.isSymLink()
        || (info.exists() && !info.isDir()))
        return fail(error,RecheckJournalText::tr("Torrent verification journal storage is redirected or invalid; recovery is blocked."));
    // QFileInfo::exists() alone conflates absence with denied ancestor traversal.
    for(const auto& path : {parent.absoluteFilePath(),directory}) {
#ifdef Q_OS_WIN
        const std::filesystem::path native(path.toStdWString());
#else
        const std::filesystem::path native(path.toUtf8().constData());
#endif
        std::error_code statusError;
        (void)std::filesystem::symlink_status(native,statusError);
        if(statusError && statusError!=std::errc::no_such_file_or_directory)
            return fail(error,RecheckJournalText::tr("Torrent verification journal storage is inaccessible; recovery is blocked."));
    }
    const auto accessible=[](const QFileInfo& entry) {
        if(!entry.exists()) return true;
        if(!entry.isReadable()) return false;
#ifdef Q_OS_UNIX
        if(!entry.isExecutable()) return false;
#endif
        return true;
    };
    if(!accessible(parent) || !accessible(info))
        return fail(error,RecheckJournalText::tr("Torrent verification journal storage is inaccessible; recovery is blocked."));
    if(create && (!QDir().mkpath(directory)
        || !QFile::setPermissions(directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)))
        return fail(error,RecheckJournalText::tr("Cannot create private Torrent verification journal storage."));
    return true;
}
QJsonArray indices(const QList<int>& values)
{
    QJsonArray array;
    for(const int value : values) array.append(value);
    return array;
}
QByteArray encode(const RecheckRecord& record)
{
    // JSON numbers cannot represent every 64-bit operation generation exactly.
    return QJsonDocument(QJsonObject{{"version",1},{"jobId",record.jobId},
        {"metadataDigest",record.metadataDigest},{"storageIdentity",record.storageIdentity},
        {"token",QString::number(record.token)},{"revision",QString::number(record.revision)},
        {"resumeGeneration",QString::number(record.resumeGeneration)},
        {"selectedFiles",indices(record.selectedFiles)},{"pieces",indices(record.pieces)},
        {"sourceReadOnly",record.sourceReadOnly}}).toJson(QJsonDocument::Compact);
}
bool number(const QJsonValue& value,quint64& result)
{
    if(!value.isString()) return false;
    const auto text=value.toString();
    if(text.size()>20) return false;
    bool ok=false;
    result=text.toULongLong(&ok);
    return ok && QString::number(result)==text;
}
bool decodeIndices(const QJsonValue& value,int maximum,QList<int>& result)
{
    if(!value.isArray()) return false;
    const auto array=value.toArray();
    if(array.size()>maximum) return false;
    for(const auto& entry : array) {
        const int index=entry.toInt(-1);
        if(!entry.isDouble() || entry.toDouble()!=index || index<0 || index>=maximum) return false;
        result.append(index);
    }
    return true;
}
std::optional<RecheckRecord> decode(const QByteArray& bytes)
{
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject()) return {};
    const auto object=document.object();
    if(!object["version"].isDouble() || object["version"].toDouble()!=1
        || !object["sourceReadOnly"].isBool()) return {};
    RecheckRecord record;
    for(const auto* field : {"jobId","metadataDigest","storageIdentity"})
        if(!object[field].isString()) return {};
    record.jobId=object["jobId"].toString();
    record.metadataDigest=object["metadataDigest"].toString();
    record.storageIdentity=object["storageIdentity"].toString();
    record.sourceReadOnly=object["sourceReadOnly"].toBool();
    if(!number(object["token"],record.token) || !number(object["revision"],record.revision)
        || !number(object["resumeGeneration"],record.resumeGeneration)
        || !decodeIndices(object["selectedFiles"],maxFiles,record.selectedFiles)
        || !decodeIndices(object["pieces"],maxPieces,record.pieces) || !valid(record)) return {};
    return record;
}
}

RecheckJournal::RecheckJournal(QString stateDirectory)
{
    if(!validString(stateDirectory,4096)) return;
    const QFileInfo root(QDir(stateDirectory).absolutePath());
    const auto canonical=root.canonicalFilePath();
    directory=QDir(canonical.isEmpty() ? root.absoluteFilePath() : canonical).filePath("rechecks");
}

std::optional<RecheckRecord> RecheckJournal::pending(const QString &jobId,QString *error) const
{
    if(error) error->clear();
    if(!validString(jobId,256)) {
        fail(error,RecheckJournalText::tr("Invalid Torrent verification journal identity."));
        return {};
    }
    if(!validDirectory(directory,false,error)) return {};
    const auto path=recordPath(directory,jobId);
    const QFileInfo info(path);
    if(info.isSymLink() || (info.exists() && !info.isFile())) {
        fail(error,RecheckJournalText::tr("Torrent verification record is redirected or invalid; recovery is blocked."));
        return {};
    }
    if(!info.exists()) return {};
    QFile file(path);
    if(info.size()>maxBytes || !file.open(QIODevice::ReadOnly)) {
        fail(error,RecheckJournalText::tr("Cannot read bounded Torrent verification record; recovery is blocked."));
        return {};
    }
    const auto bytes=file.read(maxBytes+1);
    const auto record=bytes.size()>maxBytes || file.error()!=QFile::NoError
        ? std::optional<RecheckRecord>{} : decode(bytes);
    if(!record || record->jobId!=jobId) {
        fail(error,RecheckJournalText::tr("Invalid Torrent verification record; recovery is blocked."));
        return {};
    }
    return record;
}

bool RecheckJournal::begin(const RecheckRecord &record,QString *error)
{
    if(error) error->clear();
    if(!valid(record)) return fail(error,RecheckJournalText::tr("Invalid Torrent verification recovery scope."));
    QString readError;
    const auto previous=pending(record.jobId,&readError);
    if(!readError.isEmpty()) return fail(error,readError);
    if(previous) return fail(error,RecheckJournalText::tr("An unfinished Torrent verification already requires recovery."));
    if(!validDirectory(directory,true,error)) return false;
    const auto bytes=encode(record);
    if(bytes.size()>maxBytes) return fail(error,RecheckJournalText::tr("Torrent verification record exceeds its size limit."));
    QSaveFile file(recordPath(directory,record.jobId));
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFile::ReadOwner|QFile::WriteOwner)
        || file.write(bytes)!=bytes.size() || !file.commit())
        return fail(error,RecheckJournalText::tr("Cannot atomically persist Torrent verification recovery record."));
    return true;
}

bool RecheckJournal::finish(const QString &jobId,quint64 token,quint64 durableResumeGeneration,QString *error)
{
    if(error) error->clear();
    QString readError;
    const auto record=pending(jobId,&readError);
    if(!readError.isEmpty()) return fail(error,readError);
    if(!record) return fail(error,RecheckJournalText::tr("Torrent verification recovery record is missing."));
    if(!token || token!=record->token || durableResumeGeneration!=record->resumeGeneration)
        return fail(error,RecheckJournalText::tr("Torrent verification result generation is not durably matched."));
    if(!QFile::remove(recordPath(directory,jobId)))
        return fail(error,RecheckJournalText::tr("Cannot remove Torrent verification recovery record; recovery remains required."));
    return true;
}
} // namespace eiskalt::torrent
