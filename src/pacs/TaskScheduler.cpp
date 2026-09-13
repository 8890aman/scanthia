#include "TaskScheduler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QProcess>
#include <QStringList>

namespace meda {

QString TaskScheduler::taskName(const QString& id)
{
    return "Scanthia_AutoPull_" + id;
}

QString TaskScheduler::scheduleString(const AutoPullRule& r)
{
    // schtasks /SC accepts: ONCE, DAILY, WEEKLY, ONSTART, ONLOGON.
    // We map our enum to these and emit /ST (time), /SD (start date),
    // /D (days for WEEKLY).
    switch (r.schedule) {
    case AutoPullRule::Schedule::Once:
        return "/SC ONCE /SD " + r.startTime.toString("yyyy/MM/dd") +
               " /ST " + r.startTime.toString("HH:mm:ss");
    case AutoPullRule::Schedule::Daily:
        return "/SC DAILY /ST " + r.startTime.toString("HH:mm:ss");
    case AutoPullRule::Schedule::Weekdays:
        // schtasks has no "weekdays" token; emulate with WEEKLY Mon-Fri.
        return "/SC WEEKLY /D MON,TUE,WED,THU,FRI /ST " +
               r.startTime.toString("HH:mm:ss");
    case AutoPullRule::Schedule::Weekly: {
        // weeklyDays bitmask: Mon=1..Sun=64
        static const char* days[] = {"MON","TUE","WED","THU","FRI","SAT","SUN"};
        QStringList sel;
        for (int i = 0; i < 7; ++i)
            if (r.weeklyDays & (1 << i))
                sel.append(days[i]);
        if (sel.isEmpty()) sel.append("MON");
        return "/SC WEEKLY /D " + sel.join(',') + " /ST " +
               r.startTime.toString("HH:mm:ss");
    }
    }
    return "/SC DAILY /ST " + r.startTime.toString("HH:mm:ss");
}

bool TaskScheduler::registerRule(const AutoPullRule& r, QString* err)
{
    // Delete any existing task with the same name first (schtasks /Create
    // fails if the task exists; /Change requires the task to exist).
    QProcess::execute("schtasks",
        {"/Delete", "/TN", taskName(r.id), "/F"});

    const QString exe = QCoreApplication::applicationFilePath();
    const QString cmd = "\"" + exe + "\" --autopull " + r.id;
    QStringList args;
    args << "/Create"
         << "/TN" << taskName(r.id)
         << "/TR" << cmd
         << scheduleString(r)
         << "/F";   // force overwrite
    const int rc = QProcess::execute("schtasks", args);
    if (rc != 0) {
        if (err) *err = QString("schtasks /Create rc=%1").arg(rc);
        return false;
    }
    return true;
}

bool TaskScheduler::unregisterRule(const QString& id, QString* err)
{
    const int rc = QProcess::execute("schtasks",
        {"/Delete", "/TN", taskName(id), "/F"});
    if (rc != 0) {
        if (err) *err = QString("schtasks /Delete rc=%1").arg(rc);
        return false;
    }
    return true;
}

void TaskScheduler::syncAll()
{
    AutoPullRules store;
    if (!store.load()) return;
    for (const auto& r : store.rules()) {
        if (r.enabled)
            registerRule(r);
        else
            unregisterRule(r.id);
    }
}

} // namespace meda
