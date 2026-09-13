#pragma once

#include "PacsClient.h"

#include <QDateTime>
#include <QString>
#include <QList>

namespace meda {

/// One scheduled auto-pull rule. Persisted as JSON in AppData.
struct AutoPullRule {
    QString id;                 // uuid for Task Scheduler naming
    QString name;               // human label
    bool    enabled = true;

    // Source node
    PacsNode node;
    QString moveDestAET;        // for C-MOVE

    // Query filters (mirrors StudyQuery + retrieve method)
    StudyQuery query;
    QString retrieveMethod;     // "C-GET" or "C-MOVE"

    // Limits
    int maxStudies = 10;        // hard cap on retrieved studies
    bool  skipDuplicates = true;

    // Schedule
    enum class Schedule { Once, Daily, Weekdays, Weekly } schedule = Schedule::Daily;
    QDateTime startTime;        // first run / daily time / weekly time
    int      weeklyDays = 0x7F; // bitmask: Mon=1..Sun=64

    // Last run state (written by the worker, read by the manager)
    QDateTime lastRun;
    QString   lastStatus;       // "OK 5 studies", "Failed: ...", "Running"
    int       lastCount = -1;
};

/// JSON-backed store for auto-pull rules. File lives in AppData.
class AutoPullRules {
public:
    AutoPullRules();
    ~AutoPullRules();

    bool load();
    bool save() const;

    QList<AutoPullRule> rules() const { return m_rules; }
    void setRules(const QList<AutoPullRule>& r) { m_rules = r; }

    /// Upsert by id; generates a new id if empty.
    void upsert(const AutoPullRule& r);
    void remove(const QString& id);
    AutoPullRule get(const QString& id) const;

    /// Append a run-history line for a rule (kept small, last 200).
    void appendHistory(const QString& ruleId, const QString& line);
    QList<QString> history(const QString& ruleId) const;

private:
    QString m_path;
    QString m_histPath;
    QList<AutoPullRule> m_rules;
};

} // namespace meda
