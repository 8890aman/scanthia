#include "StudyDatabase.h"

#include "DicomLoader.h"
#include "Thumbnailer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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

QSqlQuery q(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    query.prepare(sql);
    return query;
}

} // namespace

struct StudyDatabase::Impl {
    QSqlDatabase db;
};

StudyDatabase::StudyDatabase() : m_impl(std::make_unique<Impl>()) {}
StudyDatabase::~StudyDatabase()
{
    if (m_impl->db.isOpen()) {
        const auto name = m_impl->db.connectionName();
        m_impl->db.close();
        QSqlDatabase::removeDatabase(name);
    }
}

bool StudyDatabase::open(const QString& path)
{
    m_impl->db = QSqlDatabase::addDatabase("QSQLITE", "Scanthia");
    m_impl->db.setDatabaseName(path);
    if (!m_impl->db.open())
        return false;
    QSqlQuery(m_impl->db).exec("PRAGMA foreign_keys = ON");
    for (const char* stmt : kSchema) {
        if (!QSqlQuery(m_impl->db).exec(stmt))
            return false;
    }
    return true;
}

bool StudyDatabase::isOpen() const { return m_impl->db.isOpen(); }

int StudyDatabase::indexDirectory(const QString& dir)
{
    auto series = DicomLoader::scanDirectory(dir.toStdString());
    int count = 0;
    QStringList keep;
    for (auto& s : series) {
        // Upsert study.
        auto qs = q(m_impl->db,
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

        auto qe = q(m_impl->db,
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
        if (thumbnail(suid).isEmpty()) {
            QStringList fl;
            for (const auto& f : s.files)
                fl << QString::fromStdString(f);
            double ww = s.hasWindowing ? s.windowWidth : 2000.0;
            double wc = s.hasWindowing ? s.windowCenter : 0.0;
            auto png = Thumbnailer::renderPng(fl, ww, wc);
            if (!png.isEmpty())
                setThumbnail(suid, png);
        }
    }

    // Prune stale rows: anything indexed from this dir that no longer
    // qualifies (e.g. non-image series filtered by the loader now).
    QSqlQuery del(m_impl->db);
    del.exec("SELECT series_uid FROM series WHERE dir=" + QString("'%1'")
                 .arg(QString(dir).replace('\'', "''")));
    QStringList stale;
    while (del.next())
        stale << del.value(0).toString();
    for (const auto& suid : stale) {
        if (keep.contains(suid))
            continue;
        auto d = q(m_impl->db, "DELETE FROM series WHERE series_uid=?");
        d.addBindValue(suid);
        d.exec();
    }
    // Drop orphan studies.
    QSqlQuery(m_impl->db).exec(
        "DELETE FROM studies WHERE study_uid NOT IN "
        "(SELECT DISTINCT study_uid FROM series)");
    return count;
}

QList<StudyRecord> StudyDatabase::studies() const
{
    QList<StudyRecord> out;
    QSqlQuery query(m_impl->db);
    query.exec(
        "SELECT s.study_uid,s.patient_name,s.patient_id,s.study_date,"
        "s.description,s.accession,s.modalities,COUNT(r.series_uid) "
        "FROM studies s LEFT JOIN series r ON r.study_uid=s.study_uid "
        "GROUP BY s.study_uid ORDER BY s.study_date DESC, s.patient_name");
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
    auto query = q(m_impl->db,
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
    auto query = q(m_impl->db, "SELECT study_uid FROM series WHERE series_uid=?");
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
    auto query = q(m_impl->db, "SELECT thumb FROM series WHERE series_uid=?");
    query.addBindValue(seriesUID);
    query.exec();
    if (query.next())
        return query.value(0).toByteArray();
    return {};
}

void StudyDatabase::setThumbnail(const QString& seriesUID,
                                 const QByteArray& png)
{
    auto query = q(m_impl->db, "UPDATE series SET thumb=? WHERE series_uid=?");
    query.addBindValue(png);
    query.addBindValue(seriesUID);
    query.exec();
}

void StudyDatabase::removeStudy(const QString& studyUID)
{
    auto q1 = q(m_impl->db, "DELETE FROM series WHERE study_uid=?");
    q1.addBindValue(studyUID);
    q1.exec();
    auto q2 = q(m_impl->db, "DELETE FROM studies WHERE study_uid=?");
    q2.addBindValue(studyUID);
    q2.exec();
}

void StudyDatabase::removeSeries(const QString& seriesUID)
{
    auto q1 = q(m_impl->db, "DELETE FROM series WHERE series_uid=?");
    q1.addBindValue(seriesUID);
    q1.exec();
    QSqlQuery(m_impl->db).exec(
        "DELETE FROM studies WHERE study_uid NOT IN "
        "(SELECT DISTINCT study_uid FROM series)");
}

void StudyDatabase::clear()
{
    QSqlQuery(m_impl->db).exec("DELETE FROM series");
    QSqlQuery(m_impl->db).exec("DELETE FROM studies");
}

} // namespace meda
