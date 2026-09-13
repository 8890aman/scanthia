#include "AutoPullRunner.h"
#include "PacsClient.h"
#include "StudyDatabase.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QString>

#include <cstdio>

namespace meda {

namespace {

void log(const QString& s)
{
    std::fprintf(stdout, "%s\n", s.toUtf8().constData());
    std::fflush(stdout);
}

} // namespace

QString AutoPullRunner::downloadDir(const QString& studyUID)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/downloads/" + studyUID;
}

int AutoPullRunner::run(const QString& ruleId)
{
    AutoPullRules store;
    if (!store.load()) {
        log("AutoPullRunner: no rules file.");
        return 1;
    }
    const AutoPullRule rule = store.get(ruleId);
    if (rule.id.isEmpty()) {
        log("AutoPullRunner: rule not found: " + ruleId);
        return 1;
    }
    if (!rule.enabled) {
        log("AutoPullRunner: rule disabled, skipping: " + rule.name);
        return 0;
    }

    log(QString("AutoPull: running rule \"%1\" against %2:%3")
            .arg(rule.name).arg(QString::fromStdString(rule.node.host))
            .arg(rule.node.port));

    // Mark as running.
    AutoPullRule r = rule;
    r.lastRun = QDateTime::currentDateTime();
    r.lastStatus = "Running";
    store.upsert(r);
    store.save();

    // 1. Query matching studies.
    std::vector<StudyQueryResult> results;
    std::string err;
    if (!PacsClient().queryStudies(rule.node, rule.query, results, &err)) {
        const QString msg = QString("Query failed: %1").arg(err.c_str());
        log(msg);
        r.lastStatus = msg;
        store.upsert(r);
        store.save();
        store.appendHistory(ruleId, msg);
        return 2;
    }
    log(QString("  matched %1 studies").arg(results.size()));

    // 2. Apply limits.
    if (results.size() > static_cast<size_t>(rule.maxStudies))
        results.resize(rule.maxStudies);

    // 3. Skip duplicates already in the local library.
    StudyDatabase db;
    const QString dbPath = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation) + "/scanthia.db";
    db.open(dbPath);
    QSet<QString> known;
    if (rule.skipDuplicates) {
        for (const auto& s : db.studies())
            known.insert(s.studyUID);
    }

    int pulled = 0;
    int skipped = 0;
    for (const auto& s : results) {
        const QString uid = QString::fromStdString(s.studyInstanceUID);
        if (rule.skipDuplicates && known.contains(uid)) {
            ++skipped;
            log(QString("  skip duplicate %1").arg(uid));
            continue;
        }
        log(QString("  retrieving %1 (%2)...")
                .arg(uid).arg(QString::fromStdString(s.patientName)));
        std::string e2;
        const bool ok =
            rule.retrieveMethod == "C-MOVE"
                ? PacsClient().retrieveStudyMove(
                      rule.node, s.studyInstanceUID,
                      rule.moveDestAET.toStdString(), &e2)
                : PacsClient().retrieveStudyGet(
                      rule.node, s.studyInstanceUID,
                      downloadDir(uid).toStdString(), &e2);
        if (!ok) {
            const QString msg = QString("Retrieve failed for %1: %2")
                                    .arg(uid).arg(e2.c_str());
            log(msg);
            store.appendHistory(ruleId, msg);
            continue;
        }
        // Index the downloaded study into the local library.
        const QString dir = downloadDir(uid);
        if (QDir(dir).exists())
            db.indexDirectory(dir);
        ++pulled;
    }

    // For C-MOVE the SCP (in the GUI process) writes to /incoming; we
    // can't index from here without the SCP path. The GUI process's
    // incoming timer will pick it up. We only index C-GET downloads.

    const QString summary = QString("OK: pulled %1, skipped %2, matched %3")
                                 .arg(pulled).arg(skipped)
                                 .arg(results.size());
    log("  " + summary);
    r.lastStatus = summary;
    r.lastCount = pulled;
    store.upsert(r);
    store.save();
    store.appendHistory(ruleId, summary);
    return 0;
}

} // namespace meda
