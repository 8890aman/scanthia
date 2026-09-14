#pragma once

#include "Types.h"
#include "Volume.h"

#include <QVTKOpenGLNativeWidget.h>

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkVolume.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkSmartVolumeMapper.h>

#include <array>
#include <QTimer>

namespace meda {

/// 3D GPU volume rendering view with CT-oriented transfer-function presets
/// and optional MPR cursor planes.
class VolumeWidget : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit VolumeWidget(QWidget* parent = nullptr);

    void setVolume(const VolumePtr& vol);
    /// preset: "CT-Bone", "CT-Soft", "CT-Lung", "CT-Angio",
    ///         "MR-T1", "MR-T2", "MIP"
    void setPreset(const QString& preset);
    void setCursor(const std::array<double,3>& ijk);
    void setShowPlanes(bool on);

    /// Interactive LOD: drop sample distance while rotating/zooming,
    /// restore full quality after the user stops.
    void beginInteraction();
    void endInteraction();

private:
    void applyCtPreset(const QString& preset);
    void updatePlaneActors();
    void setLodQuality(bool interactive);

    VolumePtr      m_volume;
    bool           m_showPlanes = true;
    std::array<double,3> m_cursor{0, 0, 0};

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer>    m_renderer;
    vtkSmartPointer<vtkVolume>      m_volumeProp;
    vtkSmartPointer<vtkSmartVolumeMapper> m_mapper;
    vtkSmartPointer<vtkActor>       m_planeActors[3];

    QTimer         m_idleTimer;
    bool           m_interacting = false;
};

} // namespace meda
