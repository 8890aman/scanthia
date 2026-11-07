#pragma once

#include "Types.h"
#include "Volume.h"

#include <QVTKOpenGLNativeWidget.h>

#include <vtkSmartPointer.h>
#include <vtkActor2D.h>
#include <vtkBoxWidget.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPlane.h>
#include <vtkPolyData.h>
#include <vtkImageData.h>
#include <vtkRenderer.h>
#include <vtkVolume.h>
#include <vtkGenericOpenGLRenderWindow.h>

#include <array>
#include <vector>

namespace meda {

/// 3D GPU volume rendering view with CT-oriented transfer-function presets
/// and optional MPR cursor planes.
class VolumeWidget : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit VolumeWidget(QWidget* parent = nullptr);

    void setVolume(const VolumePtr& vol);
    /// preset: "CT-Bone", "CT-Soft", "CT-Lung", "MR-Default", "MIP"
    void setPreset(const QString& preset);
    void setCursor(const std::array<double,3>& ijk);
    void setShowPlanes(bool on);
    /// Weasis-style crosshair cuts: clip the volume on the given side
    /// of the MPR crosshair. Bits 0-5 remove the +X,-X,+Y,-Y,+Z,-Z
    /// halves respectively; planes track the cursor while scrolling.
    void setCutMask(unsigned mask);
    unsigned cutMask() const { return m_cutMask; }
    /// Freehand lasso sculpt (medecoro-style): while armed, LMB draws a
    /// lasso on the 3D view; on release every voxel projecting inside
    /// the loop is carved to air on a detached copy — MPR is untouched.
    void setFreeCut(bool on);
    bool freeCutArmed() const { return m_freeCut; }
    /// Discard all carving and show the pristine volume again.
    void restoreVolume();
    /// medecoro/OHIF crop: vtkBoxWidget around the volume — dragging
    /// its faces/corners clips the 3D render live (6 oriented planes).
    void setCropBox(bool on);
    bool cropArmed() const { return m_cropArmed; }
    /// Preset, cuts, lasso carve, crop and camera back to defaults.
    void resetView();
    /// Orientation cube + 1-click snaps: 'A','P','L','R','H'(ead),
    /// 'F'(eet) — sets the camera along that patient axis.
    void setOrientationCube(bool on);
    void snapView(char dir);

signals:
    /// Double-click on the 3D pane → host toggles it full-size.
    void doubleClicked();
    /// Lasso carve finished — free-cut auto-disarmed; host should
    /// uncheck its action.
    void freeCutDone();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void showEvent(QShowEvent* e) override {
        QVTKOpenGLNativeWidget::showEvent(e);
        setOrientationCube(m_cubeWanted);   // safe once realized
    }

private:
    void applyCtPreset(const QString& preset);
    void updatePlaneActors();
    void updateCutPlanes();
    void updateLassoActor();
    void carveLasso();
    void applyCropBox();
    void resetCameraAnterior();
    std::array<double,2> displayPos(const QPoint& p) const;

    VolumePtr      m_volume;
    bool           m_showPlanes = true;
    unsigned       m_cutMask = 0;
    bool           m_freeCut = false;
    bool           m_drawing  = false;
    std::array<double,3> m_cursor{0, 0, 0};
    /// Lasso points in VTK display coords (y up), render-window pixels.
    std::vector<std::array<double,2>> m_lasso;
    /// Detached copy the 3D mapper renders once carving begins —
    /// the source vtkImageData stays clean for the MPR views.
    vtkSmartPointer<vtkImageData> m_carveImage;
    vtkSmartPointer<vtkImageData> m_srcImage;
    vtkSmartPointer<vtkActor2D>   m_lassoActor;
    vtkSmartPointer<vtkActor2D>   m_lassoFill;
    vtkSmartPointer<vtkPolyData>  m_lassoPoly;
    bool                          m_cropArmed = false;
    vtkSmartPointer<vtkBoxWidget> m_boxWidget;
    vtkSmartPointer<vtkPlane>     m_cropPlanes[6];
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientWidget;
    bool                          m_cubeWanted = true;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer>    m_renderer;
    vtkSmartPointer<vtkVolume>      m_volumeProp;
    vtkSmartPointer<vtkActor>       m_planeActors[3];
    vtkSmartPointer<vtkPlane>       m_cutPlanes[6];
};

} // namespace meda
