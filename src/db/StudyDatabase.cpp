#include "StudyDatabase.h"

#include "DicomLoader.h"
#include "Thumbnailer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QThread>
#include <cstdio>
#include <mutex>

namespace meda {

namespace {

const char* kSchema[] = {
    R"(CREATE TABLE IF NOT EXISTS studies (
        study_uid   TEXT PRIMARY KEY,
        patient_name TEXT, patient_id TEXT,
        study_date  TEXT, description TEXT,
        accession   TEXT, modalities  TEXT
    ))",
    R"(CREATE TABLE IF NOT EXISTS series (
        series_uid  TEXT PRIMARY KEY,
        study_uid   TEXT REFERENCES studies(study_uid) ON DELETE CASCADE,
        modality    TEXT, description TEXT, body_part TEXT,
        rows        INT, cols INT, instances INT,
        ww REAL, wc REAL, has_wl INT,
        dir TEXT, files TEXT,
        thumb BLOB
    ))",
    "CREATE INDEX IF NOT EXISTS idx_series_study ON series(study_uid)",
};

/// Each thread gets its own QSqlDatabase connection to the same file.
/// Qt SQL connections are thread-bound — sharing one across threads
/// silently corrupts SQLite.  The mutex serializes writes so the
/// schema and thumbnails don't race.
QSqlDatabase threadConn(const QString& path, std::mutex& mtx)
{
    const auto tid = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    const auto name = "Scanthia_" + tid;
    if (QSqlDatabase::contains(name))
        return QSqlDatabase::database(name);
    std::lock_guard<std::mutex> g(mtx);
    auto db = QSqlDatabase::addDatabase("QSQLITE", name);
    db.setDatabaseName(path);
    db.open();
    QSqlQuery(db).exec("PRAGMA foreign_keys = ON");
    for (const char* stmt : kSchema)
        QSqlQuery(db).exec(stmt);
    return db;
}

QSqlQuery q(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    query.prepare(sql);
    return query;
}

} // namespace

struct StudyDatabase::Impl {
    QString path;
    std::mutex mtx;
};

StudyDatabase::StudyDatabase() : m_impl(std::make_unique<Impl>()) {}
StudyDatabase::~StudyDatabase()
{
    // Close and remove all thread connections we created.
    const auto tid = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    const auto name = "Scanthia_" + tid;
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase::database(name).close();
        QSqlDatabase::removeDatabase(name);
    }
}

bool StudyDatabase::open(const QString& path)
{
    m_impl->path = path;
    auto db = threadConn(path, m_impl->mtx);
    return db.isOpen();
}

bool StudyDatabase::isOpen() const
{
    const auto tid = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    const auto name = "Scanthia_" + tid;
    return QSqlDatabase::contains(name) && QSqlDatabase::database(name).isOpen();
}

int StudyDatabase::indexDirectory(const QString& dir)
{
    std::lock_guard<std::mutex> g(m_impl->mtx);
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto series = DicomLoader::scanDirectory(dir.toStdString());
    int count = 0;
    QStringList keep;
    for (auto& s : series) {
        // Upsert study.
        auto qs = q(db,
            "INSERT INTO studies(study_uid,patient_name,patient_id,study_date,"
            "description,accession,modalities) VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(study_uid) DO UPDATE SET "
            "patient_name=excluded.patient_name,"
            "study_date=excluded.study_date");
        qs.addBindValue(QString::fromStdString(s.studyInstanceUID));
        qs.addBindValue(QString::fromStdString(s.patientName));
        qs.addBindValue(QString::fromStdString(s.patientID));
        qs.addBindValue(QString::fromStdString(s.studyDate));
        qs.addBindValue(QString::fromStdString(s.studyDescription));
        qs.addBindValue(QString::fromStdString(s.sopClassUID));
        qs.addBindValue(QString::fromStdString(s.modality));
        qs.exec();

        QJsonArray files;
        for (const auto& f : s.files)
            files.append(QString::fromStdString(f));

        auto qe = q(db,
            "INSERT INTO series(series_uid,study_uid,modality,description,"
            "body_part,rows,cols,instances,ww,wc,has_wl,dir,files) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(series_uid) DO UPDATE SET "
            "dir=excluded.dir,files=excluded.files,instances=excluded.instances");
        qe.addBindValue(QString::fromStdString(s.seriesInstanceUID));
        qe.addBindValue(QString::fromStdString(s.studyInstanceUID));
        qe.addBindValue(QString::fromStdString(s.modality));
        qe.addBindValue(QString::fromStdString(s.seriesDescription));
        qe.addBindValue(QString::fromStdString(s.bodyPart));
        qe.addBindValue(s.rows);
        qe.addBindValue(s.columns);
        qe.addBindValue(s.instanceCount);
        qe.addBindValue(s.windowWidth);
        qe.addBindValue(s.windowCenter);
        qe.addBindValue(s.hasWindowing ? 1 : 0);
        qe.addBindValue(dir);
        qe.addBindValue(QString::fromUtf8(
            QJsonDocument(files).toJson(QJsonDocument::Compact)));
        qe.exec();
        ++count;
        keep << QString::fromStdString(s.seriesInstanceUID);

        // Generate thumbnail if missing.
        const auto suid = QString::fromStdString(s.seriesInstanceUID);
        {
            auto tq = q(db, "SELECT thumb FROM series WHERE series_uid=?");
            tq.addBindValue(suid);
            tq.exec();
            bool has = tq.next() && !tq.value(0).toByteArray().isEmpty();
            if (!has) {
                QStringList fl;
                for (const auto& f : s.files)
                    fl << QString::fromStdString(f);
                double ww = s.hasWindowing ? s.windowWidth : 2000.0;
                double wc = s.hasWindowing ? s.windowCenter : 0.0;
                auto png = Thumbnailer::renderPng(fl, ww, wc);
                if (!png.isEmpty()) {
                    auto uq = q(db, "UPDATE series SET thumb=? WHERE series_uid=?");
                    uq.addBindValue(png);
                    uq.addBindValue(suid);
                    uq.exec();
                }
            }
        }
    }

    // Prune stale rows: anything indexed from this dir that no longer
    // qualifies (e.g. non-image series filtered by the loader now).
    QSqlQuery del(db);
    del.exec("SELECT series_uid FROM series WHERE dir=" + QString("'%1'")
                 .arg(QString(dir).replace('\'', "''")));
    QStringList stale;
    while (del.next())
        stale << del.value(0).toString();
    for (const auto& suid : stale) {
        if (keep.contains(suid))
            continue;
        auto d = q(db, "DELETE FROM series WHERE series_uid=?");
        d.addBindValue(suid);
        d.exec();
    }
    // Drop orphan studies.
    QSqlQuery(db).exec(
        "DELETE FROM studies WHERE study_uid NOT IN "
        "(SELECT DISTINCT study_uid FROM series)");
    return count;
}

QList<StudyRecord> StudyDatabase::studies() const
{
    QList<StudyRecord> out;
    auto db = threadConn(m_impl->path, m_impl->mtx);
    QSqlQuery query(db);
    const QString sql =
        "SELECT s.study_uid,s.patient_name,s.patient_id,s.study_date,"
        "s.description,s.accession,s.modalities,COUNT(r.series_uid) "
        "FROM studies s LEFT JOIN series r ON r.study_uid=s.study_uid "
        "GROUP BY s.study_uid ORDER BY s.study_date DESC, s.patient_name";
    if (!query.exec(sql)) {
        std::fprintf(stderr, "studies() query failed: %s\n",
                     query.lastError().text().toUtf8().constData());
        return out;
    }
    while (query.next()) {
        StudyRecord r;
        r.studyUID    = query.value(0).toString();
        r.patientName = query.value(1).toString();
        r.patientID   = query.value(2).toString();
        r.studyDate   = query.value(3).toString();
        r.description = query.value(4).toString();
        r.accession   = query.value(5).toString();
        r.modalities  = query.value(6).toString();
        r.seriesCount = query.value(7).toInt();
        out.append(r);
    }
    return out;
}

QList<SeriesMeta> StudyDatabase::seriesOf(const QString& studyUID) const
{
    QList<SeriesMeta> out;
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto query = q(db,
        "SELECT * FROM series WHERE study_uid=? ORDER BY description");
    query.addBindValue(studyUID);
    query.exec();
    while (query.next()) {
        SeriesMeta s;
        s.seriesInstanceUID = query.value("series_uid").toString().toStdString();
        s.studyInstanceUID  = query.value("study_uid").toString().toStdString();
        s.modality          = query.value("modality").toString().toStdString();
        s.seriesDescription = query.value("description").toString().toStdString();
        s.bodyPart          = query.value("body_part").toString().toStdString();
        s.rows              = query.value("rows").toInt();
        s.columns           = query.value("cols").toInt();
        s.instanceCount     = query.value("instances").toInt();
        s.windowWidth       = query.value("ww").toDouble();
        s.windowCenter      = query.value("wc").toDouble();
        s.hasWindowing      = query.value("has_wl").toInt() != 0;
        const auto filesJson = QJsonDocument::fromJson(
            query.value("files").toString().toUtf8());
        for (const auto& f : filesJson.array())
            s.files.push_back(f.toString().toStdString());
        out.append(s);
    }
    return out;
}

SeriesMeta StudyDatabase::series(const QString& seriesUID) const
{
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto query = q(db, "SELECT study_uid FROM series WHERE series_uid=?");
    query.addBindValue(seriesUID);
    query.exec();
    if (!query.next())
        return {};
    for (const auto& s : seriesOf(query.value(0).toString()))
        if (s.seriesInstanceUID == seriesUID.toStdString())
            return s;
    return {};
}

QByteArray StudyDatabase::thumbnail(const QString& seriesUID) const
{
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto query = q(db, "SELECT thumb FROM series WHERE series_uid=?");
    query.addBindValue(seriesUID);
    query.exec();
    if (query.next())
        return query.value(0).toByteArray();
    return {};
}

void StudyDatabase::setThumbnail(const QString& seriesUID,
                                 const QByteArray& png)
{
    std::lock_guard<std::mutex> g(m_impl->mtx);
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto query = q(db, "UPDATE series SET thumb=? WHERE series_uid=?");
    query.addBindValue(png);
    query.addBindValue(seriesUID);
    query.exec();
}

void StudyDatabase::removeStudy(const QString& studyUID)
{
    std::lock_guard<std::mutex> g(m_impl->mtx);
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto q1 = q(db, "DELETE FROM series WHERE study_uid=?");
    q1.addBindValue(studyUID);
    q1.exec();
    auto q2 = q(db, "DELETE FROM studies WHERE study_uid=?");
    q2.addBindValue(studyUID);
    q2.exec();
}

void StudyDatabase::removeSeries(const QString& seriesUID)
{
    std::lock_guard<std::mutex> g(m_impl->mtx);
    auto db = threadConn(m_impl->path, m_impl->mtx);
    auto q1 = q(db, "DELETE FROM series WHERE series_uid=?");
    q1.addBindValue(seriesUID);
    q1.exec();
    QSqlQuery(db).exec(
        "DELETE FROM studies WHERE study_uid NOT IN "
        "(SELECT DISTINCT study_uid FROM series)");
}

void StudyDatabase::clear()
{
    std::lock_guard<std::mutex> g(m_impl->mtx);
    auto db = threadConn(m_impl->path, m_impl->mtx);
    QSqlQuery(db).exec("DELETE FROM series");
    QSqlQuery(db).exec("DELETE FROM studies");
}

} // namespace meda
