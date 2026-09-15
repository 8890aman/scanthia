#include "DicomWebClient.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace meda {

DicomWebClient::DicomWebClient() : m_nam(new QNetworkAccessManager) {}
DicomWebClient::~DicomWebClient() { delete m_nam; }

void DicomWebClient::setBaseUrl(const QString& url)
{
    m_base = url;
    while (m_base.endsWith('/'))
        m_base.chop(1);
}

QString DicomWebClient::tag(const QJsonObject& item, const char* t)
{
    const auto el = item.value(QLatin1String(t)).toObject();
    const auto vals = el.value("Value").toArray();
    if (vals.isEmpty())
        return {};
    const auto v = vals.first();
    if (v.isObject())   // PN etc: {"Alphabetic": "..."}
        return v.toObject().value("Alphabetic").toString();
    return v.toVariant().toString();
}

void DicomWebClient::getJson(const QString& path, Rows done, Error fail)
{
    QNetworkRequest req(QUrl(m_base + path));
    req.setRawHeader("Accept", "application/dicom+json");
    auto* reply = m_nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("HTTP error: %1")
                     .arg(reply->errorString()));
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        done(doc.isArray() ? doc.array() : QJsonArray{});
    });
}

void DicomWebClient::queryStudies(const QString& patientName, Rows done,
                                  Error fail)
{
    QString path = "/qido/studies?includefield=00081030";
    if (!patientName.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("PatientName", "*" + patientName + "*");
        path += "&" + q.toString();
    }
    getJson(path, done, fail);
}

void DicomWebClient::querySeries(const QString& studyUID, Rows done,
                                 Error fail)
{
    getJson("/qido/studies/" + studyUID + "/series?includefield=00201209",
            done, fail);
}

void DicomWebClient::queryStudiesFiltered(const QString& accession,
                                          const QString& patientID,
                                          const QString& studyUID,
                                          Rows done, Error fail)
{
    QString path = "/qido/studies?includefield=00081030";
    QUrlQuery q;
    if (!accession.isEmpty())
        q.addQueryItem("AccessionNumber", accession);
    if (!patientID.isEmpty())
        q.addQueryItem("PatientID", patientID);
    if (!studyUID.isEmpty())
        q.addQueryItem("StudyInstanceUID", studyUID);
    const QString qs = q.toString();
    if (!qs.isEmpty())
        path += "&" + qs;
    getJson(path, done, fail);
}

void DicomWebClient::retrieveStudy(
    const QString& studyUID, const QString& destDir,
    std::function<void(int, int)> progress,
    std::function<void(const QString& dir)> done, Error fail)
{
    QDir().mkpath(destDir);
    // 1) List the study's series via QIDO, then pull each in turn.
    querySeries(studyUID, [=](const QJsonArray& seriesList) {
        QStringList uids;
        for (const auto& v : seriesList) {
            const QString suid = tag(v.toObject(), "0020000E");
            if (!suid.isEmpty())
                uids << suid;
        }
        if (uids.isEmpty()) {
            fail(QStringLiteral("Study has no series"));
            return;
        }
        auto remaining = std::make_shared<QStringList>(uids);
        auto finished = std::make_shared<int>(0);
        auto failed   = std::make_shared<bool>(false);
        auto next = std::make_shared<std::function<void()>>();
        *next = [=] {
            if (*failed)
                return;
            if (remaining->isEmpty()) {
                done(destDir);
                return;
            }
            const QString suid = remaining->takeFirst();
            retrieveSeries(studyUID, suid, destDir + "/" + suid,
                {},   // per-file progress — we report per-series
                [=](const QString&) {
                    ++(*finished);
                    if (progress)
                        progress(*finished, uids.size());
                    (*next)();
                },
                [=](const QString& e) {
                    *failed = true;
                    fail(e);
                });
        };
        (*next)();
    }, fail);
}

void DicomWebClient::retrieveSeries(
    const QString& studyUID, const QString& seriesUID, const QString& destDir,
    std::function<void(int, int)> progress,
    std::function<void(const QString& dir)> done, Error fail)
{
    QDir().mkpath(destDir);
    // 1) QIDO instance list for the series.
    getJson("/qido/studies/" + studyUID + "/series/" + seriesUID +
                "/instances",
            [=](const QJsonArray& insts) {
        QStringList sopUids;
        for (const auto& v : insts) {
            const QString sop = tag(v.toObject(), "00080018");
            if (!sop.isEmpty())
                sopUids << sop;
        }
        if (sopUids.isEmpty()) {
            fail(QStringLiteral("Series has no instances"));
            return;
        }
        // 2) Download each instance via WADO-RS, sequentially.
        auto remaining = std::make_shared<QStringList>(sopUids);
        auto count = std::make_shared<int>(0);
        auto next = std::make_shared<std::function<void()>>();
        *next = [=] {
            if (remaining->isEmpty()) {
                done(destDir);
                return;
            }
            const QString sop = remaining->takeFirst();
            const QString url = m_base + "/wado/studies/" + studyUID +
                                "/series/" + seriesUID + "/instances/" + sop;
            QNetworkRequest req{QUrl(url)};
            req.setRawHeader("Accept", "application/dicom");
            auto* reply = m_nam->get(req);
            QObject::connect(reply, &QNetworkReply::finished, reply,
                             [=] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    fail(QStringLiteral("WADO fetch failed: %1")
                             .arg(reply->errorString()));
                    return;
                }
                QFile out(destDir + "/" + sop + ".dcm");
                if (out.open(QIODevice::WriteOnly)) {
                    out.write(reply->readAll());
                }
                ++(*count);
                if (progress)
                    progress(*count, sopUids.size());
                (*next)();
            });
        };
        (*next)();
    }, fail);
}

} // namespace meda
