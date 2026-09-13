#pragma once

#include "AutoPullRules.h"

#include <QString>

namespace meda {

/// Windows Task Scheduler bridge. Creates, updates, and removes a
/// scheduled task per rule using `schtasks.exe`. The task launches the
/// Scanthia executable with `--autopull <ruleId>`.
class TaskScheduler {
public:
    /// Register (or update) a scheduled task for the given rule.
    /// Returns true on success.
    static bool registerRule(const AutoPullRule& r, QString* err = nullptr);

    /// Remove the scheduled task for the given rule id.
    static bool unregisterRule(const QString& id, QString* err = nullptr);

    /// Re-register all rules (call after bulk edits).
    static void syncAll();

private:
    static QString taskName(const QString& id);
    static QString scheduleString(const AutoPullRule& r);
};

} // namespace meda
