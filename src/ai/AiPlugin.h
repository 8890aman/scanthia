#pragma once

#include "Segmentation.h"
#include "Volume.h"

#include <QtPlugin>
#include <QString>

namespace meda {

class InferenceEngine;

/// Interface for AI plugins. A plugin packages a model + preprocessing +
/// postprocessing into a single "run" call that returns a Segmentation.
///
/// Implementations are Qt plugins: mark the class with Q_PLUGIN_METADATA
/// and Q_INTERFACES(meda::IAiPlugin).
class IAiPlugin {
public:
    virtual ~IAiPlugin() = default;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const { return "0.1"; }

    /// Modalities this plugin accepts, e.g. {"CT"}; empty = any.
    virtual QStringList supportedModalities() const { return {}; }

    /// Run inference. `engine` is pre-configured; plugins may load their
    /// own model via engine.loadModel(). May throw std::exception.
    virtual Segmentation run(const VolumePtr& volume,
                             InferenceEngine& engine) = 0;
};

} // namespace meda

#define IAiPlugin_iid "org.Scanthia.IAiPlugin/1.0"
Q_DECLARE_INTERFACE(meda::IAiPlugin, IAiPlugin_iid)
