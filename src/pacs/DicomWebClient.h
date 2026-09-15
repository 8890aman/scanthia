#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QJsonArray>
#include <QString>

class QNetworkAccessManager;

namespace meda {

/// Minimal DICOMweb client: QIDO-RS for study/series/instance queries,
/// WADO-RS for instance retrieval. Works against Orthanc, dcm4chee,
/// and any compliant DICOMweb endpoint.
class DicomWebClient {
public:
    DicomWebClient();
    ~DicomWebClient();

    void setBaseUrl(const QString& url);   // e.g. http://host:8042/dicom-web

    using Rows  = std::function<void(const QJsonArray&)>;
    using Error = std::function<void(const QString&)>;

    /// QIDO-RS: list studies, optionally filtered by patient name.
    void queryStudies(const QString& patientName, Rows done, Error fail);
    /// QIDO-RS: filtered lookup — accession / patientID / studyUID.
    /// Empty strings are not sent as filters.
    void queryStudiesFiltered(const QString& accession,
                              const QString& patientID,
                              const QString& studyUID,
                              Rows done, Error fail);
    /// QIDO-RS: series within a study.
    void querySeries(const QString& studyUID, Rows done, Error fail);

    /// WADO-RS: download every series of a study into destDir/<seriesUID>.
    /// progress(doneSeries, totalSeries) fires per finished series.
    void retrieveStudy(const QString& studyUID, const QString& destDir,
                       std::function<void(int, int)> progress,
                       std::function<void(const QString& dir)> done,
                       Error fail);

    /// WADO-RS: download every instance of a series into destDir.
    /// progress(done, total) is called per saved file.
    void retrieveSeries(const QString& studyUID, const QString& seriesUID,
                        const QString& destDir,
                        std::function<void(int, int)> progress,
                        std::function<void(const QString& dir)> done,
                        Error fail);

    /// Read a tag's first string-ish value from a QIDO JSON item.
    static QString tag(const QJsonObject& item, const char* tag);

private:
    void getJson(const QString& path, Rows done, Error fail);

    QNetworkAccessManager* m_nam;
    QString m_base;
};

} // namespace meda
