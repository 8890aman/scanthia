#pragma once

#include "AiPlugin.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

namespace meda {

/// Discovers IAiPlugin implementations in the plugins/ directory next to
/// the executable.
class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QObject* parent = nullptr);

    /// Load all plugins found under `dir` (defaults to <appdir>/plugins).
    void discover(const QString& dir = {});

    QList<IAiPlugin*> plugins() const;
    IAiPlugin* pluginNamed(const QString& name) const;

private:
    std::vector<QObject*> m_instances;
};

} // namespace meda
