#pragma once

#include "Types.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <memory>

namespace meda {

struct StudyRecord {
    QString studyUID;
    QString patientName;
    QString patientID;
    QString studyDate;
    QString description;
    QString accession;
    QString modalities;
    int     seriesCount = 0;
};

/// SQLite-backed index of all DICOM studies Scanthia has seen. Stores
/// series-level metadata, the full file list per series, and a thumbnail.
class StudyDatabase {
public:
    StudyDatabase();
    ~StudyDatabase();

    bool open(const QString& path);
    bool isOpen() const;

    /// Scan a directory and upsert every series found. Generates
    /// thumbnails for series that don't have one yet.
    /// Returns number of series indexed.
    int indexDirectory(const QString& dir);

    QList<StudyRecord> studies() const;
    QList<SeriesMeta>  seriesOf(const QString& studyUID) const;
    /// Series whose files were indexed from `dir` — used after a
    /// retrieve, where the files' StudyInstanceUID may not match the
    /// UID we requested (re-identified exports sometimes carry a
    /// duplicate/aliased UID tag).
    QList<SeriesMeta>  seriesInDir(const QString& dir) const;
    SeriesMeta         series(const QString& seriesUID) const;
    QByteArray         thumbnail(const QString& seriesUID) const;

    void setThumbnail(const QString& seriesUID, const QByteArray& png);
    void removeStudy(const QString& studyUID);
    void removeSeries(const QString& seriesUID);
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace meda
