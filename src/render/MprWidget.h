#pragma once

#include "Types.h"
#include "Volume.h"

#include <QWidget>
#include <QFrame>
#include <QJsonObject>
#include <QMainWindow>

#include <vtkSmartPointer.h>
#include <vtkImageData.h>
#include <vtkLookupTable.h>

#include <array>

namespace meda {

class SliceViewer;
class VolumeWidget;

/// Classic 2x2 MPR layout: axial, sagittal, coronal and a 3D volume view,
/// synchronized through a shared cursor in IJK space.
class MprWidget : public QWidget {
    Q_OBJECT
public:
    explicit MprWidget(QWidget* parent = nullptr);

    void setVolume(const VolumePtr& vol);

    SliceViewer*  viewer(Orientation o);
    VolumeWidget* volumeView() const { return m_volumeView; }

    void setActiveTool(Tool t);
    /// Thick-slab MPR for all views: 0=off 1=Mean 2=MIP 3=MinIP, mm thick.
    void setSlab(int type, double mm);
    /// Denoised display toggle for all views.
    void setSmoothing(bool on);
    /// Color map for all views: 0=gray 1=inverted 2=hot iron 3=PET 4=bone.
    void setColorMap(int which);
    /// Hide all drawn annotations in all views.
    void clearAnnotations();
    /// Remove the most recent annotation in each view.
    void undoLastAnnotation();
    /// Crosshair line visibility in all views.
    void setCrosshairVisible(bool on);
    void setOverlay(vtkImageData* labelmap, vtkLookupTable* lut,
                    double opacity);
    /// Fused modality (PET on CT) shown through a hot LUT + alpha ramp.
    void setFusion(vtkImageData* img, vtkLookupTable* lut, double opacity);
    void setFusionOpacity(double o);
    void setFusionWindow(double lo, double hi);
    void setFusionColormap(int which);
    void setFusionBarTitle(const QString& text);
    /// "FUSION: <series>" badge on all slice views; empty hides it.
    void setFusionBadge(const QString& name);
    /// Oblique MPR: rotate the active view's plane. (0,0) resets.
    void setObliqueAngles(double pitchDeg, double yawDeg);
    /// Reset all views to orthogonal MPR.
    void resetOblique();
    /// Cine playback in all slice views.
    void setCinePlaying(bool on);
    /// Playback rate for all views.
    void setCineFps(int fps);
    /// Linked zoom/pan across views (default on).
    void setViewsLinked(bool on) { m_linkEnabled = on; }
    /// Window/level applied to all slice views.
    void setWindowLevel(double w, double c);
    /// Show/hide the mm edge ruler on all slice views.
    void setScaleVisible(bool on);
    /// Segmentation editing parameters on all slice views.
    void setEditLabel(int label);
    void setBrushRadiusMm(double mm);
    /// Top-left info text on all slice views.
    void setInfoText(const QString& text);
    void setStudyText(const QString& text);
    void setSeriesText(const QString& text);
    /// Detach/reattach a pane (0-2 = axial/sagittal/coronal, 3 = 3D)
    /// into a floating top-level window for a second monitor.
    void toggleDetach(int idx);
    /// Called by the camera observer when a view's camera changes.
    void onCameraMoved(int src);
    /// Repaint all views — used while a volume streams in.
    void refresh();
    /// Camera observer payload — public so the free callback can use it.
    struct CamLink;
    /// Serialize all views' annotations: {axial:{}, sagittal:{}, coronal:{}}.
    QJsonObject annotationsToJson() const;
    void annotationsFromJson(const QJsonObject& o);

    std::array<double,3> cursor() const { return m_cursor; }
    void setCursor(const std::array<double,3>& ijk);

signals:
    void voxelHovered(std::array<double,3> ijk, double value);
    void annotationsChanged();

private:
    void onViewSliceChanged(Orientation o, int slice);
    void onCrosshairMoved(std::array<double,3> ijk);
    void setActiveView(SliceViewer* v);
    void toggleMaximize(SliceViewer* v);
    void linkCameras();
    bool eventFilter(QObject* o, QEvent* e) override;

    VolumePtr                  m_volume;
    std::array<double,3>       m_cursor{0, 0, 0};
    SliceViewer*               m_views[3]{};
    QFrame*                    m_frames[3]{};
    QFrame*                    m_volumeFrame = nullptr;
    VolumeWidget*              m_volumeView = nullptr;
    SliceViewer*               m_maximized = nullptr;
    SliceViewer*               m_activeView = nullptr;
    QMainWindow*               m_detached[4]{};   // floating pane windows
    vtkImageData*              m_overlay = nullptr;
    vtkSmartPointer<vtkLookupTable> m_overlayLut;
    double                     m_overlayOpacity = 0.4;
    bool                       m_linkEnabled = true;
    bool                       m_camLinked = false;
    bool                       m_linkGuard = false;
    double                     m_lastFocal[3][3]{};
    CamLink*                   m_camLink[3]{};
};

} // namespace meda
