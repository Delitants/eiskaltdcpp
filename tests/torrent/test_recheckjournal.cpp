#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "torrent/RecheckJournal.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <limits>
#include <cstdlib>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#include <unistd.h>
#include <csignal>
#endif

namespace {
using namespace eiskalt::torrent;
struct Temp final : QTemporaryDir { Temp() { REQUIRE(isValid()); } };
RecheckRecord record()
{
    return {QStringLiteral("job-1"),QString(64,'a'),QStringLiteral("storage-identity"),
        7,11,9,{0,2},{0,1,3},true};
}
QString path(const QString& root,const QString& id=QStringLiteral("job-1"))
{
    const auto key=QCryptographicHash::hash(id.toUtf8(),QCryptographicHash::Sha256).toHex();
    return QDir(root).filePath("rechecks/"+QString::fromLatin1(key)+".json");
}
QByteArray read(const QString& file)
{
    QFile f(file); REQUIRE(f.open(QIODevice::ReadOnly)); return f.readAll();
}
void write(const QString& file,const QByteArray& data)
{
    REQUIRE(QDir().mkpath(QFileInfo(file).absolutePath()));
    QFile f(file); REQUIRE(f.open(QIODevice::WriteOnly)); REQUIRE(f.write(data)==data.size());
}
void ensureApp()
{
    if(QCoreApplication::instance()) return;
    static int argc=1;
    static char name[]="journal-tests";
    static char* argv[]={name,nullptr};
    static QCoreApplication app(argc,argv);
}
void child(const QString& root,const QString& mode,int expected)
{
    ensureApp();
    QProcess p;
    auto env=QProcessEnvironment::systemEnvironment();
    env.insert("EISKALT_RECHECK_JOURNAL_CHILD",root);
    env.insert("EISKALT_RECHECK_JOURNAL_MODE",mode);
    p.setProcessEnvironment(env);
    p.start(QCoreApplication::applicationFilePath(),{"[.selected-recheck-journal-child]"});
    REQUIRE(p.waitForFinished(15000));
    INFO(p.readAllStandardError().toStdString());
    INFO(p.readAllStandardOutput().toStdString());
    REQUIRE(p.exitCode()==expected);
}
}

TEST_CASE("Pending checks roundtrip exact 64-bit generations through opaque filenames", "[selected-recheck-journal]")
{
    Temp temp; REQUIRE(temp.isValid());
    RecheckJournal journal(temp.path());
    auto r=record();
    r.jobId="../../escaped";
    r.token=std::numeric_limits<quint64>::max();
    r.revision=(quint64(1)<<53)+1;
    r.resumeGeneration=r.token-1;
    QString error="stale";
    REQUIRE(journal.begin(r,&error));
    REQUIRE(error.isEmpty());
    REQUIRE(QFile::exists(path(temp.path(),r.jobId)));
    const auto restored=journal.pending(r.jobId,&error);
    REQUIRE(restored.has_value());
    REQUIRE(*restored==r);
    REQUIRE(error.isEmpty());
    REQUIRE(journal.finish(r.jobId,r.token,r.resumeGeneration,&error));
    REQUIRE_FALSE(journal.pending(r.jobId,&error).has_value());
    REQUIRE(error.isEmpty());
}

TEST_CASE("A matching durable generation is mandatory before removing a recovery barrier", "[selected-recheck-journal]")
{
    Temp temp;
    RecheckJournal journal(temp.path());
    const auto r=record(); QString error;
    REQUIRE(journal.begin(r,&error));
    const auto before=read(path(temp.path()));
    REQUIRE_FALSE(journal.finish(r.jobId,r.token+1,r.resumeGeneration,&error));
    REQUIRE_FALSE(error.isEmpty());
    for(const auto generation : {quint64(0),quint64(8),quint64(10)}) {
        REQUIRE_FALSE(journal.finish(r.jobId,r.token,generation,&error));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE(read(path(temp.path()))==before);
    }
    auto replacement=r; replacement.token++;
    REQUIRE_FALSE(journal.begin(replacement,&error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(read(path(temp.path()))==before);
    REQUIRE(journal.finish(r.jobId,r.token,r.resumeGeneration,&error));
    REQUIRE(error.isEmpty());
}

TEST_CASE("Truncated oversized and malformed records block recovery rather than disappear", "[selected-recheck-journal]")
{
    const auto kind=GENERATE(0,1,2,3,4,5,6,7);
    Temp temp; RecheckJournal journal(temp.path());
    QString error; REQUIRE(journal.begin(record(),&error));
    auto bytes=read(path(temp.path()));
    auto object=QJsonDocument::fromJson(bytes).object();
    switch(kind) {
    case 0: bytes="{"; break;
    case 1: bytes=QByteArray(16*1024*1024+1,'x'); break;
    case 2: object.insert("token",7); break;
    case 3: object.insert("selectedFiles",QJsonArray{0,0}); break;
    case 4: object.insert("pieces",QJsonArray{524288}); break;
    case 5: object.insert("sourceReadOnly",1); break;
    case 6: object.insert("jobId","different-job"); break;
    case 7: object.insert("metadataDigest",QString(257,'x')); break;
    }
    if(kind>1) bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    write(path(temp.path()),bytes);
    REQUIRE_FALSE(journal.pending("job-1",&error).has_value());
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(journal.finish("job-1",7,9,&error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(read(path(temp.path()))==bytes);
}

TEST_CASE("Invalid scopes and generations cannot create a pending record", "[selected-recheck-journal]")
{
    const auto kind=GENERATE(0,1,2,3,4,5,6,7);
    Temp temp; RecheckJournal journal(temp.path());
    auto r=record(); QString error;
    switch(kind) {
    case 0: r.token=0; break;
    case 1: r.resumeGeneration=0; break;
    case 2: r.selectedFiles={}; break;
    case 3: r.selectedFiles={10000}; break;
    case 4: r.pieces={-1}; break;
    case 5: r.pieces={0,0}; break;
    case 6: r.storageIdentity=QString(4097,'x'); break;
    case 7: r.metadataDigest=QString("bad").append(QChar(0)); break;
    }
    REQUIRE_FALSE(journal.begin(r,&error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(QFile::exists(path(temp.path())));
}

TEST_CASE("Empty-piece checks still retain a real selected-file recovery record", "[selected-recheck-journal]")
{
    Temp temp; RecheckJournal journal(temp.path());
    auto r=record(); r.pieces={}; r.revision=0; r.sourceReadOnly=false;
    QString error; REQUIRE(journal.begin(r,&error));
    REQUIRE(journal.pending(r.jobId,&error)==std::optional<RecheckRecord>{r});
}

TEST_CASE("Unwritable or redirected journal storage never removes the barrier", "[selected-recheck-journal]")
{
    Temp temp; QString error;
    SECTION("A file in place of the journal directory") {
        write(QDir(temp.path()).filePath("rechecks"),"not a directory");
        RecheckJournal journal(temp.path());
        REQUIRE_FALSE(journal.begin(record(),&error));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE_FALSE(journal.pending("job-1",&error).has_value());
        REQUIRE_FALSE(error.isEmpty());
    }
#ifdef Q_OS_UNIX
    SECTION("Symlinked journal directory") {
        Temp outside;
        const auto dir=QDir(temp.path()).filePath("rechecks");
        REQUIRE(::symlink(outside.path().toUtf8().constData(),dir.toUtf8().constData())==0);
        RecheckJournal journal(temp.path());
        REQUIRE_FALSE(journal.begin(record(),&error));
        REQUIRE_FALSE(journal.pending("job-1",&error).has_value());
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE(QDir(outside.path()).entryList(QDir::Files).isEmpty());
    }
    SECTION("Failure to unlink retains the record") {
        RecheckJournal journal(temp.path());
        REQUIRE(journal.begin(record(),&error));
        const auto dir=QDir(temp.path()).filePath("rechecks");
        const auto permissions=QFileInfo(dir).permissions();
        auto restore=qScopeGuard([&]{QFile::setPermissions(dir,permissions);});
        REQUIRE(QFile::setPermissions(dir,QFile::ReadOwner|QFile::ExeOwner));
        REQUIRE_FALSE(journal.finish("job-1",7,9,&error));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE(journal.pending("job-1",&error).has_value());
    }
    SECTION("An inaccessible directory is not an absent pending check") {
        RecheckJournal journal(temp.path());
        REQUIRE(journal.begin(record(),&error));
        const auto dir=QDir(temp.path()).filePath("rechecks");
        const auto permissions=QFileInfo(dir).permissions();
        auto restore=qScopeGuard([&]{QFile::setPermissions(dir,permissions);});
        REQUIRE(QFile::setPermissions(dir,QFile::ReadOwner));
        REQUIRE_FALSE(journal.pending("job-1",&error).has_value());
        REQUIRE_FALSE(error.isEmpty());
    }
    SECTION("An inaccessible ancestor is not an absent pending check") {
        const auto state=QDir(temp.path()).filePath("state");
        REQUIRE(QDir().mkpath(state));
        RecheckJournal journal(state);
        REQUIRE(journal.begin(record(),&error));
        const auto permissions=QFileInfo(temp.path()).permissions();
        auto restore=qScopeGuard([&]{QFile::setPermissions(temp.path(),permissions);});
        REQUIRE(QFile::setPermissions(temp.path(),QFile::ReadOwner));
        REQUIRE_FALSE(journal.pending("job-1",&error).has_value());
        REQUIRE_FALSE(error.isEmpty());
    }
#endif
}

TEST_CASE("Process exits on persistence boundaries cannot drop unfinished checks", "[selected-recheck-journal]")
{
    const auto mode=GENERATE(QString("begin-before"),QString("begin-after"),
        QString("result-before"),QString("result-after"),QString("finish-before"),QString("finish-after"));
    Temp temp; RecheckJournal journal(temp.path()); QString error;
    child(temp.path(),mode,73);
    const auto pending=journal.pending("job-1",&error);
    REQUIRE(error.isEmpty());
    REQUIRE(pending.has_value()==(mode!="begin-before" && mode!="finish-after"));
    if(pending) REQUIRE(*pending==record());
}

#ifdef Q_OS_UNIX
TEST_CASE("An interrupted failed atomic write leaves no trusted pending record", "[selected-recheck-journal]")
{
    Temp temp; RecheckJournal journal(temp.path()); QString error;
    child(temp.path(),"write-failure",74);
    REQUIRE_FALSE(journal.pending("job-1",&error).has_value());
    REQUIRE(error.isEmpty());
}
#endif

TEST_CASE("Journal subprocess crash fixture", "[.selected-recheck-journal-child]")
{
    const auto root=qEnvironmentVariable("EISKALT_RECHECK_JOURNAL_CHILD");
    REQUIRE_FALSE(root.isEmpty());
    const auto mode=qEnvironmentVariable("EISKALT_RECHECK_JOURNAL_MODE");
    RecheckJournal journal(root); QString error;
    if(mode=="begin-before") {
        REQUIRE(QDir().mkpath(QFileInfo(path(root)).absolutePath()));
        QSaveFile f(path(root)); REQUIRE(f.open(QIODevice::WriteOnly)); REQUIRE(f.write("{")==1);
        std::_Exit(73);
    }
#ifdef Q_OS_UNIX
    if(mode=="write-failure") {
        std::signal(SIGXFSZ,SIG_IGN);
        const rlimit limit{64,64}; REQUIRE(setrlimit(RLIMIT_FSIZE,&limit)==0);
        REQUIRE_FALSE(journal.begin(record(),&error)); REQUIRE_FALSE(error.isEmpty());
        std::_Exit(74);
    }
#endif
    REQUIRE(journal.begin(record(),&error));
    if(mode=="result-before" || mode=="result-after") {
        QSaveFile f(QDir(root).filePath("resume-fixture.json"));
        REQUIRE(f.open(QIODevice::WriteOnly)); REQUIRE(f.write("result-generation-9")==19);
        if(mode=="result-after") REQUIRE(f.commit());
    }
    if(mode=="finish-after") REQUIRE(journal.finish("job-1",7,9,&error));
    std::_Exit(73);
}
