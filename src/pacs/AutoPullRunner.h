#pragma once

#include "AutoPullRules.h"

#include <QString>

namespace meda {

/// Headless auto-pull executor. Called from main.cpp when launched with
/// `--autopull <ruleId>`. Queries the PACS, applies limits (max studies,
/// skip duplicates), retrieves each study, and logs the run.
class AutoPullRunner {
public:
    /// Execute one rule. Returns 0 on success, non-zero on error.
    static int run(const QString& ruleId);

private:
    static QString downloadDir(const QString& studyUID);
};

} // namespace meda
