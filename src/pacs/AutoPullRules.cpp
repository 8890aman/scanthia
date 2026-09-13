#include "AutoPullRules.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>

namespace meda {

namespace {

QString appDataDir()
{
    const QString d = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d;
}

PacsNode nodeFromJson(const QJsonObject& o)
{
    PacsNode n;
    n.host = o.value("host").toString().toStdString();
    n.port = static_cast<uint16_t>(o.value("port").toInt(104));
    n.calledAET = o.value("calledAET").toString().toStdString();
    n.callingAET = o.value("callingAET").toString().toStdString();
    return n;
}

QJsonObject nodeToJson(const PacsNode& n)
{
    QJsonObject o;
    o["host"] = QString::fromStdString(n.host);
    o["port"] = n.port;
    o["calledAET"] = QString::fromStdString(n.calledAET);
    o["callingAET"] = QString::fromStdString(n.callingAET);
    return o;
}

StudyQuery queryFromJson(const QJsonObject& o)
{
    StudyQuery q;
    q.patientName = o.value("patientName").toString().toStdString();
    q.patientID = o.value("patientID").toString().toStdString();
    q.studyDate = o.value("studyDate").toString().toStdString();
    q.modality = o.value("modality").toString().toStdString();
    q.accession = o.value("accession").toString().toStdString();
    q.studyDescription = o.value("studyDescription").toString().toStdString();
    q.referringPhysician = o.value("referringPhysician").toString().toStdString();
    return q;
}

QJsonObject queryToJson(const StudyQuery& q)
{
    QJsonObject o;
    o["patientName"] = QString::fromStdString(q.patientName);
    o["patientID"] = QString::fromStdString(q.patientID);
    o["studyDate"] = QString::fromStdString(q.studyDate);
    o["modality"] = QString::fromStdString(q.modality);
    o["accession"] = QString::fromStdString(q.accession);
    o["studyDescription"] = QString::fromStdString(q.studyDescription);
    o["referringPhysician"] = QString::fromStdString(q.referringPhysician);
    return o;
}

AutoPullRule ruleFromJson(const QJsonObject& o)
{
    AutoPullRule r;
    r.id = o.value("id").toString();
    r.name = o.value("name").toString();
    r.enabled = o.value("enabled").toBool(true);
    r.node = nodeFromJson(o.value("node").toObject());
    r.moveDestAET = o.value("moveDestAET").toString();
    r.query = queryFromJson(o.value("query").toObject());
    r.retrieveMethod = o.value("retrieveMethod").toString("C-GET");
    r.maxStudies = o.value("maxStudies").toInt(10);
    r.skipDuplicates = o.value("skipDuplicates").toBool(true);
    r.schedule = static_cast<AutoPullRule::Schedule>(
        o.value("schedule").toInt(static_cast<int>(AutoPullRule::Schedule::Daily)));
    r.startTime = QDateTime::fromString(
        o.value("startTime").toString(), Qt::ISODate);
    r.weeklyDays = o.value("weeklyDays").toInt(0x7F);
    r.lastRun = QDateTime::fromString(
        o.value("lastRun").toString(), Qt::ISODate);
    r.lastStatus = o.value("lastStatus").toString();
    r.lastCount = o.value("lastCount").toInt(-1);
    return r;
}

QJsonObject ruleToJson(const AutoPullRule& r)
{
    QJsonObject o;
    o["id"] = r.id;
    o["name"] = r.name;
    o["enabled"] = r.enabled;
    o["node"] = nodeToJson(r.node);
    o["moveDestAET"] = r.moveDestAET;
    o["query"] = queryToJson(r.query);
    o["retrieveMethod"] = r.retrieveMethod;
    o["maxStudies"] = r.maxStudies;
    o["skipDuplicates"] = r.skipDuplicates;
    o["schedule"] = static_cast<int>(r.schedule);
    o["startTime"] = r.startTime.toString(Qt::ISODate);
    o["weeklyDays"] = r.weeklyDays;
    o["lastRun"] = r.lastRun.toString(Qt::ISODate);
    o["lastStatus"] = r.lastStatus;
    o["lastCount"] = r.lastCount;
    return o;
}

} // namespace

AutoPullRules::AutoPullRules()
{
    m_path = appDataDir() + "/pullrules.json";
    m_histPath = appDataDir() + "/pullhistory.log";
    load();
}

AutoPullRules::~AutoPullRules()
{
    // Save is explicit; destructor is a no-op to avoid writes on read-only
    // copies held by dialogs.
}

bool AutoPullRules::load()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    m_rules.clear();
    const auto arr = doc.object().value("rules").toArray();
    for (const auto& v : arr)
        m_rules.append(ruleFromJson(v.toObject()));
    return true;
}

bool AutoPullRules::save() const
{
    QJsonObject root;
    QJsonArray arr;
    for (const auto& r : m_rules)
        arr.append(ruleToJson(r));
    root["rules"] = arr;
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void AutoPullRules::upsert(const AutoPullRule& r)
{
    AutoPullRule rule = r;
    if (rule.id.isEmpty())
        rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (int i = 0; i < m_rules.size(); ++i)
        if (m_rules[i].id == rule.id) {
            m_rules[i] = rule;
            return;
        }
    m_rules.append(rule);
}

void AutoPullRules::remove(const QString& id)
{
    m_rules.erase(std::remove_if(m_rules.begin(), m_rules.end(),
                                 [&](const AutoPullRule& r) { return r.id == id; }),
                  m_rules.end());
}

AutoPullRule AutoPullRules::get(const QString& id) const
{
    for (const auto& r : m_rules)
        if (r.id == id)
            return r;
    return AutoPullRule{};
}

void AutoPullRules::appendHistory(const QString& ruleId, const QString& line)
{
    QFile f(m_histPath);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    const QString stamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QTextStream(&f) << stamp << " [" << ruleId << "] " << line << "\n";
    // Keep the file bounded: trim to last 200 lines on each write.
    f.close();
    QFile rf(m_histPath);
    if (!rf.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const auto lines = QString::fromUtf8(rf.readAll()).split('\n');
    rf.close();
    if (lines.size() > 250) {
        QFile wf(m_histPath);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream s(&wf);
            for (int i = lines.size() - 201; i < lines.size(); ++i)
                if (i >= 0 && !lines[i].isEmpty())
                    s << lines[i] << "\n";
        }
    }
}

QList<QString> AutoPullRules::history(const QString& ruleId) const
{
    QFile f(m_histPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QList<QString> out;
    const auto lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const auto& l : lines)
        if (l.contains("[" + ruleId + "]"))
            out.append(l);
    return out;
}

} // namespace meda
