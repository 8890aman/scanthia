#pragma once

#include "Types.h"
#include "Volume.h"

#include <QVTKOpenGLNativeWidget.h>
#include <QJsonObject>
#include <QTimer>

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkImageSlice.h>
#include <vtkImageResliceMapper.h>
#include <vtkPlane.h>
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkTextActor.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkGenericOpenGLRenderWindow.h>

#include <array>
#include <utility>
#include <vector>

class vtkDistanceWidget;

namespace meda {

class SliceInteractionStyle;

/// A single 2D slice view. Handles window/level, scroll, zoom, pan,
/// distance measurement, MPR crosshairs and a labelmap overlay.
class SliceViewer : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit SliceViewer(QWidget* parent = nullptr);

    void setVolume(const VolumePtr& vol, Orientation o);
    void setOrientation(Orientation o);
    /// Rotate the slice plane off-axis. `pitchDeg`/`yawDeg` rotate the
    /// current orientation's normal around the in-plane horizontal and
    /// vertical axes respectively. (0,0) restores orthogonal MPR.
    void setObliqueAngles(double pitchDeg, double yawDeg);
    void obliqueAngles(double& pitchDeg, double& yawDeg) const;
    VolumePtr   volume() const { return m_volume; }
    Orientation orientation() const { return m_orientation; }

    void setSlice(int s);
    int  slice() const { return m_slice; }
    int  sliceCount() const;

    void setWindowLevel(double width, double center);
    void windowLevel(double& width, double& center) const;
    void autoWindowLevel();

    void setActiveTool(Tool t);
    Tool activeTool() const;

    void setCinePlaying(bool on);
    void setCineFps(int fps);

    /// MPR: show crosshair for the other two planes at cursor position ijk.
    void setShowCrosshair(bool on);
    void setCrosshairIjk(const std::array<double,3>& ijk);

    /// Segmentation labelmap overlay (nullptr to clear).
    void setOverlay(vtkImageData* labelmap, vtkLookupTable* lut,
                    double opacity = 0.4);

    /// Fused second modality (e.g. PET on CT) — resampled onto this
    /// volume's grid, shown through a continuous color LUT with an
    /// alpha ramp. nullptr clears it.
    void setFusion(vtkImageData* img, vtkLookupTable* lut,
                   double opacity = 0.5);
    void setFusionOpacity(double o);
    /// Adjust the overlay LUT's value window.
    void setFusionWindow(double lo, double hi);
    /// Apply one of the standard colormaps to the overlay LUT.
    void setFusionColormap(int which);
    /// Units label on the fusion colorbar (e.g. "SUV", "" hides).
    void setFusionBarTitle(const QString& text);

    /// Hide all drawn annotations (measure lines, ROI, angle, text).
    void clearAnnotations();

    /// Overlay text for the top-left corner (patient / series info).
    void setInfoText(const QString& text);
    /// Study-level text, upper-right corner above the slice line.
    void setStudyText(const QString& text);
    /// Series-level text, lower-right corner (modality/desc/dims).
    void setSeriesText(const QString& text);
    /// Show/hide the amber "FLIPPED" badge at bottom-left.
    void setFlipBadge(bool on);
    /// Show/hide the amber "DOWNSAMPLED" badge at bottom-left.
    void setDownsampleBadge(bool on);
    /// Fusion indicator: orange "FUSION: <series>" badge, bottom-right.
    /// Empty string hides it.
    void setFusionBadge(const QString& name);
    /// Show/hide the edge ruler (mm tick marks on left+bottom axes).
    /// Always hidden for unscaled volumes regardless of `on`.
    void setScaleVisible(bool on);

    /// World-space coordinate of the slice plane along the view normal.
    double slicePlaneOffset() const;

    /// Thick-slab MPR: type 0=off (thin), 1=Mean, 2=MIP, 3=MinIP.
    /// Thickness in mm.
    void setSlab(int type, double thicknessMm);
    int    slabType() const { return m_slabType; }
    double slabThickness() const { return m_slabMm; }

    /// Toggle denoised display (Gaussian-smoothed copy; raw data intact).
    void setSmoothing(bool on);

    /// Color lookup table applied after window/level.
    /// 0=gray, 1=inverted gray, 2=hot iron, 3=PET/rainbow, 4=bone.
    void setColorMap(int which);

    /// Serialize the drawn annotations (measure/roi/angle) to JSON.
    QJsonObject annotationsToJson() const;
    /// Restore previously serialized annotations.
    void annotationsFromJson(const QJsonObject& obj);

    /// Segmentation editing: label index for the brush (eraser ignores).
    void setEditLabel(int label) { m_editLabel = label; }
    /// Brush radius in millimeters.
    void setBrushRadiusMm(double mm) { m_brushMm = mm; }
    /// Paint/erase a disk at a world-space point on the current slice.
    void paintAt(const std::array<double,3>& world, bool erase);
    /// Overlay labelmap being edited (may be null).
    vtkImageData* labelmap() const { return m_labelmap; }
    /// Remove the most recently drawn annotation.
    void undoAnnotation();

    vtkRenderer* renderer() const { return m_renderer; }
    /// Force a repaint — used while a volume streams in.
    void refresh() { m_renderWindow->Render(); }
    /// Z is the zoom modifier (hold + wheel). Public so the app-level
    /// event filter can set it even when another widget has focus.
    void setZoomKeyHeld(bool on) { m_zoomKeyHeld = on; }

    /// Interactive LOD: called on scroll/drag start → fast rendering.
    /// endInteraction() is called ~150ms after the last interaction to
    /// trigger a full-quality re-render.
    void beginInteraction();
    void endInteraction();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void zoomBy(double factor);   // scale camera by factor
    bool event(QEvent* e) override;  // touch gestures
    void resizeEvent(QResizeEvent* e) override;
    /// Qt-level double click — more reliable than VTK's internal
    /// generic-interactor double-click timing, which can miss clicks
    /// when the custom interactor style is mid-drag (W/L, pan, etc).
    void mouseDoubleClickEvent(QMouseEvent* e) override;

signals:
    void sliceChanged(int slice);
    void windowLevelChanged(double width, double center);
    void crosshairMoved(std::array<double,3> ijk);
    void voxelHovered(std::array<double,3> ijk, double value);
    void activated();
    void measured(double millimeters);
    void doubleClicked();
    /// Emitted when a measure/ROI/angle annotation is updated.
    void annotationsChanged();
    void overlayEdited();
    /// Emitted when editing created a fresh labelmap — owner should
    /// propagate it to sibling views.
    void overlayCreated(vtkImageData* map, vtkLookupTable* lut);
    /// Right-click while a tool is active → host should reset to W/L.
    void toolDeselectRequested();

private:
    void updatePlaneOrigin();
    void updateOrientationLabels();
    std::array<double,3> pickPointOnPlane(int displayX, int displayY) const;
    void setupCamera();
    void updateCrosshairActors();
    void updateCornerText();
    /// Ruler: fixed "nice" length bars (1-2-5 cm/mm)
    /// centred on the bottom and left edges, in display pixels.
    void updateScaleRuler();
    /// Start a new annotation (kind 0=distance 1=roi 2=angle).
    void beginAnno(int kind);
    void updateMeasureActors(const std::array<double,3>& a,
                             const std::array<double,3>& b);
    void updateRoiActors(const std::array<double,3>& a,
                         const std::array<double,3>& b);
    void updateAngleActors(const std::array<double,3>& a,
                           const std::array<double,3>& b,
                           const std::array<double,3>& c);
    void updateAnnotationVisibility();
    /// Rebuild an annotation's line/handle/label geometry + text.
    struct Anno;
    void rebuildAnno(Anno& an);
    void addAnnoActors(Anno& an, double r, double g, double b);
    void removeAnnoActors(Anno& an);
    void setAnnoVisible(Anno& an, bool on);
    /// Nearest annotation handle to a world point, in display px.
    /// Returns {annoIndex, pointIndex} or {-1,-1}.
    std::pair<int,int> pickAnnoHandle(
        const std::array<double,3>& worldPt) const;
    void editAnnoPoint(int annoIdx, int ptIdx,
                       const std::array<double,3>& worldPt);

    VolumePtr      m_volume;
    Orientation    m_orientation = Orientation::Axial;
    double         m_obliquePitch = 0.0;  // degrees off orthogonal
    double         m_obliqueYaw   = 0.0;
    int            m_slice = 0;
    bool           m_showCrosshair = false;
    QString        m_studyText;   // prepended to the TR slice line
    std::array<double,3> m_crosshairIjk{0,0,0};

    // Graphics list: each annotation is a line polyline,
    // square handle glyphs at its control points, and a world-space
    // label. Multiple annotations per slice; handles are draggable.
    struct Anno {
        int kind = 0;      // 0=distance 1=roi 2=angle
        int slice = -1;
        int npts = 0;      // points placed so far (drafts may be partial)
        std::array<std::array<double,3>,3> p{};
        vtkSmartPointer<vtkActor>                 line;
        vtkSmartPointer<vtkActor>                 handles;      // color ring
        vtkSmartPointer<vtkActor>                 handlesInner; // bright core
        vtkSmartPointer<vtkBillboardTextActor3D>  text;
    };
    std::vector<Anno> m_annos;
    int m_draftAnno = -1;                 // index being drawn now
    int m_editAnno  = -1, m_editPt = -1;  // handle being dragged

    vtkSmartPointer<vtkImageData> m_labelmap;   // overlay being edited
    int    m_editLabel = 1;
    double m_brushMm = 5.0;
    // W/L drag state — start window/level captured on mouse down.
    double m_wlStartW = 400.0, m_wlStartC = 40.0;
    std::array<double,3> m_lastPaint{0, 0, 0};
    bool   m_paintHasLast = false;   // stroke interpolation anchor
    bool   m_zoomKeyHeld = false;   // Z held → wheel zooms
    vtkSmartPointer<vtkCallbackCommand> m_keyGuard;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer>            m_renderer;
    vtkSmartPointer<vtkRenderer>            m_overlay;  // layer-1 anno pass
    vtkSmartPointer<vtkImageSlice>          m_imageActor;
    vtkSmartPointer<vtkImageResliceMapper>  m_mapper;
    vtkSmartPointer<vtkPlane>               m_plane;
    vtkSmartPointer<vtkImageSlice>          m_overlayActor;
    vtkSmartPointer<vtkImageResliceMapper>  m_overlayMapper;
    vtkSmartPointer<vtkImageSlice>          m_fusionActor;
    vtkSmartPointer<vtkImageResliceMapper>  m_fusionMapper;
    vtkSmartPointer<vtkImageData>           m_fusionImg;
    vtkSmartPointer<vtkLookupTable>         m_fusionLut;
    vtkSmartPointer<vtkScalarBarActor>      m_fusionBar;
    double m_fusionOpacity = 0.5;
    int    m_slabType = 0;
    double m_slabMm   = 0.0;
    bool   m_smoothing = false;
    bool   m_scaleVisible = true;

    // Animated slice position (world mm) — scroll targets m_slice, the
    // rendered plane eases toward it through interpolated positions.
    double m_planePos = 0.0;
    QTimer m_scrollAnim;
    vtkSmartPointer<vtkActor>               m_crossLineH;
    vtkSmartPointer<vtkActor>               m_crossLineV;
    vtkSmartPointer<vtkTextActor>           m_hud[4];   // BL BR TL TR
    // Scale ruler: polydata drawn in display px (dark outline pass +
    // white pass), plus a label per axis.
    vtkSmartPointer<vtkPolyData>            m_rulerPd;
    vtkSmartPointer<vtkPoints>              m_rulerPts;
    vtkSmartPointer<vtkActor2D>             m_rulerDark;
    vtkSmartPointer<vtkActor2D>             m_rulerLight;
    vtkSmartPointer<vtkTextActor>           m_rulerLabelH;
    vtkSmartPointer<vtkTextActor>           m_rulerLabelV;
    vtkSmartPointer<vtkTextActor>           m_flipBadge;
    vtkSmartPointer<vtkTextActor>           m_downsampleBadge;
    vtkSmartPointer<vtkTextActor>           m_fusionBadge;
    vtkSmartPointer<vtkTextActor>           m_dirLabel[4]; // L,R,top,bottom
    vtkSmartPointer<SliceInteractionStyle>  m_style;
    vtkSmartPointer<vtkLookupTable>         m_overlayLut;
    vtkSmartPointer<vtkLookupTable>         m_colorLut;

    QTimer m_cineTimer;
    QTimer m_lodIdleTimer;     // fires ~150ms after last interaction → HQ
    bool   m_interacting = false;
};

} // namespace meda
