#include "DicomNodes.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace meda {

namespace {

QString appDataDir()
{
    const QString d = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d;
}

QJsonObject toJson(const DicomNode& n)
{
    QJsonObject o;
    o["name"] = n.name;
    o["host"] = QString::fromStdString(n.node.host);
    o["port"] = n.node.port;
    o["calledAET"] = QString::fromStdString(n.node.calledAET);
    o["callingAET"] = QString::fromStdString(n.node.callingAET);
    o["moveDestAET"] = n.moveDestAET;
    o["retrieveMethod"] = n.retrieveMethod;
    o["isDefault"] = n.isDefault;
    return o;
}

DicomNode fromJson(const QJsonObject& o)
{
    DicomNode n;
    n.name = o.value("name").toString();
    n.node.host = o.value("host").toString().toStdString();
    n.node.port = static_cast<uint16_t>(o.value("port").toInt(104));
    n.node.calledAET = o.value("calledAET").toString().toStdString();
    n.node.callingAET = o.value("callingAET").toString().toStdString();
    n.moveDestAET = o.value("moveDestAET").toString();
    n.retrieveMethod = o.value("retrieveMethod").toString("C-GET");
    n.isDefault = o.value("isDefault").toBool(false);
    return n;
}

} // namespace

DicomNodes::DicomNodes()
{
    m_path = appDataDir() + "/nodes.json";
    load();
}

bool DicomNodes::load()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    m_nodes.clear();
    for (const auto& v : doc.object().value("nodes").toArray())
        m_nodes.append(fromJson(v.toObject()));
    return true;
}

bool DicomNodes::save() const
{
    QJsonObject root;
    QJsonArray arr;
    for (const auto& n : m_nodes)
        arr.append(toJson(n));
    root["nodes"] = arr;
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void DicomNodes::upsert(const DicomNode& n)
{
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].name == n.name) {
            m_nodes[i] = n;
            return;
        }
    m_nodes.append(n);
}

void DicomNodes::remove(const QString& name)
{
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(),
                                 [&](const DicomNode& n) {
                                     return n.name == name;
                                 }),
                  m_nodes.end());
}

DicomNode DicomNodes::get(const QString& name) const
{
    for (const auto& n : m_nodes)
        if (n.name == name)
            return n;
    return DicomNode{};
}

DicomNode DicomNodes::defaultNode() const
{
    for (const auto& n : m_nodes)
        if (n.isDefault)
            return n;
    return m_nodes.isEmpty() ? DicomNode{} : m_nodes.first();
}

} // namespace meda
