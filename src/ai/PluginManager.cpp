#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>

namespace meda {

PluginManager::PluginManager(QObject* parent) : QObject(parent) {}

void PluginManager::discover(const QString& dir)
{
    const QString pluginDir =
        dir.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/plugins"
            : dir;
    for (const QString& file :
         QDir(pluginDir).entryList(QDir::Files)) {
#ifdef _WIN32
        if (!file.endsWith(".dll"))
            continue;
#else
        if (!file.endsWith(".so") && !file.endsWith(".dylib"))
            continue;
        if (file.endsWith("debug.dylib"))
            continue;
#endif
        QPluginLoader loader(pluginDir + "/" + file);
        if (auto* obj = loader.instance()) {
            if (qobject_cast<IAiPlugin*>(obj))
                m_instances.push_back(obj);
        }
    }
}

QList<IAiPlugin*> PluginManager::plugins() const
{
    QList<IAiPlugin*> out;
    for (auto* o : m_instances)
        out.append(qobject_cast<IAiPlugin*>(o));
    return out;
}

IAiPlugin* PluginManager::pluginNamed(const QString& name) const
{
    for (auto* p : plugins())
        if (p->name() == name)
            return p;
    return nullptr;
}

} // namespace meda
