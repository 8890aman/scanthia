#include "SliceViewer.h"

#include "SliceInteractionStyle.h"
#include "Segmentation.h"

#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkImageMapToColors.h>
#include <vtkImageProperty.h>
#include <vtkLineSource.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>
#include <vtkPointData.h>
#include <vtkInteractorObserver.h>
#include <vtkRenderWindowInteractor.h>

#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QPanGesture>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace meda {

namespace {

vtkSmartPointer<vtkActor> makeLineActor(double r, double g, double b)
{
    auto src = vtkSmartPointer<vtkLineSource>::New();
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(src->GetOutputPort());
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(r, g, b);
    actor->GetProperty()->SetLineWidth(1.0f);
    actor->SetVisibility(0);
    actor->PickableOff();
    actor->SetDragable(0);
    // Keep the line source reachable through the mapper.
    actor->GetProperty()->SetAmbient(1.0);
    actor->GetProperty()->SetDiffuse(0.0);
    return actor;
}

void setLine(vtkActor* actor, const std::array<double,3>& a,
             const std::array<double,3>& b)
{
    auto* src = vtkLineSource::SafeDownCast(
        vtkPolyDataMapper::SafeDownCast(actor->GetMapper())->GetInputAlgorithm());
    src->SetPoint1(a.data());
    src->SetPoint2(b.data());
    src->Modified();
}

} // namespace

SliceViewer::SliceViewer(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent)
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    setRenderWindow(m_renderWindow);

    // Touch gestures — pinch to zoom, pan to scroll slices.
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::PanGesture);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.0, 0.0, 0.0);
    m_renderWindow->AddRenderer(m_renderer);

    // vtkPlane in world space drives the reslice — image and overlay share it.
    m_plane = vtkSmartPointer<vtkPlane>::New();
    m_plane->SetNormal(0, 0, 1);
    m_plane->SetOrigin(0, 0, 0);

    m_mapper = vtkSmartPointer<vtkImageResliceMapper>::New();
    m_mapper->SetSlicePlane(m_plane);
    m_mapper->SetResampleToScreenPixels(1);  // screen-pixel-exact resampling
    m_mapper->SetAutoAdjustImageQuality(1);  // drop to fast path while moving
    m_mapper->SetImageSampleFactor(2);       // 2x supersampled reslice
    m_imageActor = vtkSmartPointer<vtkImageSlice>::New();
    m_imageActor->SetMapper(m_mapper);
    m_imageActor->SetVisibility(0);
    m_renderer->AddActor(m_imageActor);

    m_overlayMapper = vtkSmartPointer<vtkImageResliceMapper>::New();
    m_overlayMapper->SetSlicePlane(m_plane);
    m_overlayActor = vtkSmartPointer<vtkImageSlice>::New();
    m_overlayActor->SetMapper(m_overlayMapper);
    m_overlayActor->SetVisibility(0);
    m_overlayActor->PickableOff(); // so WL picks the base image
    m_renderer->AddActor(m_overlayActor);

    m_fusionMapper = vtkSmartPointer<vtkImageResliceMapper>::New();
    m_fusionMapper->SetSlicePlane(m_plane);
    m_fusionActor = vtkSmartPointer<vtkImageSlice>::New();
    m_fusionActor->SetMapper(m_fusionMapper);
    m_fusionActor->SetVisibility(0);
    m_fusionActor->PickableOff();
    m_renderer->AddActor(m_fusionActor);

    m_crossLineH = makeLineActor(0.2, 0.9, 0.9);
    m_crossLineV = makeLineActor(0.9, 0.7, 0.2);
    m_measureLine = makeLineActor(1.0, 0.85, 0.1);
    m_measureLine->GetProperty()->SetLineWidth(2.0f);
    m_renderer->AddActor(m_crossLineH);
    m_renderer->AddActor(m_crossLineV);
    m_renderer->AddActor(m_measureLine);

    m_measureLine2 = makeLineActor(1.0, 0.85, 0.1);
    m_measureLine2->GetProperty()->SetLineWidth(2.0f);
    m_renderer->AddActor(m_measureLine2);

    m_measureText = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_measureText->GetTextProperty()->SetColor(0.91, 0.64, 0.24);
    m_measureText->GetTextProperty()->SetFontSize(14);
    m_measureText->GetTextProperty()->BoldOn();
    m_measureText->SetVisibility(0);
    m_renderer->AddActor(m_measureText);

    // ROI rectangle: closed 5-point polyline on the slice plane.
    m_roiPts = vtkSmartPointer<vtkPoints>::New();
    m_roiPts->SetNumberOfPoints(5);
    auto roiCells = vtkSmartPointer<vtkCellArray>::New();
    roiCells->InsertNextCell(5);
    for (int i = 0; i < 5; ++i)
        roiCells->InsertCellPoint(i);
    auto roiPd = vtkSmartPointer<vtkPolyData>::New();
    roiPd->SetPoints(m_roiPts);
    roiPd->SetLines(roiCells);
    auto roiMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    roiMapper->SetInputData(roiPd);
    m_roiRect = vtkSmartPointer<vtkActor>::New();
    m_roiRect->SetMapper(roiMapper);
    m_roiRect->GetProperty()->SetColor(0.18, 0.80, 0.44);
    m_roiRect->GetProperty()->SetLineWidth(2.0f);
    m_roiRect->SetVisibility(0);
    m_renderer->AddActor(m_roiRect);

    m_roiText = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    m_roiText->GetTextProperty()->SetColor(0.18, 0.80, 0.44);
    m_roiText->GetTextProperty()->SetFontSize(13);
    m_roiText->GetTextProperty()->BoldOn();
    m_roiText->SetVisibility(0);
    m_renderer->AddActor(m_roiText);

    m_corner = vtkSmartPointer<vtkCornerAnnotation>::New();
    m_corner->GetTextProperty()->SetColor(0.90, 0.92, 0.94);
    m_corner->GetTextProperty()->SetFontSize(12);
    m_renderer->AddActor2D(m_corner);

    // Amber "FLIPPED" marker, fixed at bottom-left above the WW/WL text.
    m_flipBadge = vtkSmartPointer<vtkTextActor>::New();
    m_flipBadge->GetTextProperty()->SetColor(0.91, 0.64, 0.24);
    m_flipBadge->GetTextProperty()->SetFontSize(12);
    m_flipBadge->GetTextProperty()->BoldOn();
    m_flipBadge->SetInput("FLIPPED");
    m_flipBadge->SetDisplayPosition(8, 26);
    m_flipBadge->SetVisibility(0);
    m_renderer->AddActor2D(m_flipBadge);

    // Amber "DOWNSAMPLED" marker — shown when memory-safe mode kicked in.
    m_downsampleBadge = vtkSmartPointer<vtkTextActor>::New();
    m_downsampleBadge->GetTextProperty()->SetColor(0.91, 0.64, 0.24);
    m_downsampleBadge->GetTextProperty()->SetFontSize(12);
    m_downsampleBadge->GetTextProperty()->BoldOn();
    m_downsampleBadge->SetInput("DOWNSAMPLED");
    m_downsampleBadge->SetDisplayPosition(8, 44);
    m_downsampleBadge->SetVisibility(0);
    m_renderer->AddActor2D(m_downsampleBadge);

    // Orientation labels on the 4 view edges (R/L/A/P/H/F).
    const double pos[4][2] = {{0.015, 0.5}, {0.985, 0.5}, {0.5, 0.97},
                              {0.5, 0.03}};
    for (int i = 0; i < 4; ++i) {
        m_dirLabel[i] = vtkSmartPointer<vtkTextActor>::New();
        m_dirLabel[i]->GetPositionCoordinate()
            ->SetCoordinateSystemToNormalizedViewport();
        m_dirLabel[i]->SetPosition(pos[i][0], pos[i][1]);
        auto* tp = m_dirLabel[i]->GetTextProperty();
        tp->SetFontSize(14);
        tp->SetColor(0.91, 0.64, 0.24);   // avionics amber
        tp->SetJustificationToCentered();
        tp->SetVerticalJustificationToCentered();
        m_dirLabel[i]->SetVisibility(0);
        m_renderer->AddActor2D(m_dirLabel[i]);
    }

    m_style = vtkSmartPointer<SliceInteractionStyle>::New();
    m_style->SetInteractionModeToImageSlicing();

    m_style->pickPoint = [this](int x, int y) {
        return pickPointOnPlane(x, y);
    };
    m_style->onSliceScrolled = [this](int delta) {
        if (m_zoomKeyHeld) {
            zoomBy(delta > 0 ? 0.85 : 1.18);
        } else {
            setSlice(m_slice + delta);
        }
    };
    m_style->onWindowLevelEnded = [this] {
        double w, c;
        windowLevel(w, c);
        updateCornerText();
        emit windowLevelChanged(w, c);
    };
    m_style->onWindowLevelStarted = [this] {
        windowLevel(m_wlStartW, m_wlStartC);
    };
    m_style->onWindowLevelDelta = [this](int dx, int dy) {
        // Sensitivity: one full drag across ~512px spans the scalar range.
        double scale = 1.0;
        if (m_volume) {
            const auto r = m_volume->scalarRange();
            scale = std::max(0.05, (r[1] - r[0]) / 512.0);
        }
        // dx right → wider window; dy up → higher level (brighter).
        setWindowLevel(m_wlStartW + dx * scale,
                       m_wlStartC + dy * scale);
        updateCornerText();
        emit windowLevelChanged(m_wlStartW + dx * scale,
                                m_wlStartC + dy * scale);
    };
    m_style->onPointPicked = [this](std::array<double,3> p) {
        std::array<double,3> ijk;
        m_volume->worldToIjk(p, ijk);
        emit crosshairMoved(ijk);
    };
    m_style->onMeasured = [this](std::array<double,3> a,
                                 std::array<double,3> b) {
        updateMeasureActors(a, b);
    };
    m_style->onRoi = [this](std::array<double,3> a,
                            std::array<double,3> b) {
        updateRoiActors(a, b);
    };
    m_style->onAngle = [this](std::array<double,3> a,
                              std::array<double,3> b,
                              std::array<double,3> c) {
        updateAngleActors(a, b, c);
    };
    m_style->onActivated = [this] { emit activated(); };
    m_style->onPaint = [this](std::array<double,3> p, bool erase) {
        paintAt(p, erase);
    };
    m_style->onStroke = [this](bool) { m_paintHasLast = false; };
    m_style->onRightClick = [this] {
        emit toolDeselectRequested();
    };

    // Guard the VTK interactor: 'z' must never reach the style — VTK's
    // built-in handling flips the view. Abort the event at the source.
    m_keyGuard = vtkSmartPointer<vtkCallbackCommand>::New();
    m_keyGuard->SetCallback([](vtkObject* o, unsigned long, void*, void*) {
        auto* iren = static_cast<vtkRenderWindowInteractor*>(o);
        const char* ks = iren->GetKeySym();
        if (ks && (std::strcmp(ks, "z") == 0 || std::strcmp(ks, "Z") == 0))
            iren->SetKeySym("");   // neutralize before the style sees it
    });
    interactor()->AddObserver(vtkCommand::KeyPressEvent, m_keyGuard, 1.0);
    interactor()->AddObserver(vtkCommand::CharEvent, m_keyGuard, 1.0);
    setFocusPolicy(Qt::StrongFocus);
    m_style->onHovered = [this](std::array<double,3> p) {
        if (!m_volume)
            return;
        std::array<double,3> ijk;
        m_volume->worldToIjk(p, ijk);
        auto ext = m_volume->extent();
        int i = static_cast<int>(std::lround(ijk[0]));
        int j = static_cast<int>(std::lround(ijk[1]));
        int k = static_cast<int>(std::lround(ijk[2]));
        if (i < 0 || j < 0 || k < 0 || i >= ext[0] || j >= ext[1] || k >= ext[2])
            return;
        double v = m_volume->vtkImage()->GetScalarComponentAsDouble(i, j, k, 0);
        emit voxelHovered({static_cast<double>(i), static_cast<double>(j),
                           static_cast<double>(k)}, v);
    };

    interactor()->SetInteractorStyle(m_style);
    // Double-click -> maximize/restore this view (MPR layout). Handled
    // via Qt's mouseDoubleClickEvent() override below — VTK's own
    // generic-interactor double-click timing can miss clicks when the
    // custom interactor style is mid-drag (W/L, pan, etc).

    connect(&m_cineTimer, &QTimer::timeout, this, [this] {
        if (sliceCount() > 0)
            setSlice((m_slice + 1) % sliceCount());
    });

    // Smooth scroll: ease the slice plane toward the target index.
    m_scrollAnim.setInterval(16);
    connect(&m_scrollAnim, &QTimer::timeout, this, [this] {
        if (!m_volume) {
            m_scrollAnim.stop();
            return;
        }
        const auto sp = m_volume->spacing();
        const int    a = axisOf(m_orientation);
        const double target = m_slice * sp[a];
        const double diff = target - m_planePos;
        if (std::abs(diff) < 0.05 * sp[a]) {
            m_planePos = target;
            m_scrollAnim.stop();
        } else {
            m_planePos += diff * 0.30;
        }
        updatePlaneOrigin();
        m_renderWindow->Render();
    });
}

void SliceViewer::setVolume(const VolumePtr& vol, Orientation o)
{
    m_volume = vol;
    m_imageActor->SetVisibility(vol != nullptr);
    if (!vol)
        return;
    m_mapper->SetInputData(m_smoothing ? vol->sharpenedVtk()
                                       : vol->vtkImage());
    setOrientation(o);
    setSlice(vol->clampSlice(o, vol->sliceCount(o) / 2));
    m_scrollAnim.stop();
    m_planePos = m_slice * vol->spacing()[axisOf(o)];
    updatePlaneOrigin();
    clearAnnotations();
    autoWindowLevel();
    m_renderer->ResetCamera();
}

void SliceViewer::setOrientation(Orientation o)
{
    m_orientation = o;
    m_obliquePitch = 0.0;
    m_obliqueYaw   = 0.0;
    double n[3] = {0, 0, 0};
    n[axisOf(o)] = 1.0;
    m_plane->SetNormal(n);
    m_plane->Modified();
    m_mapper->Modified();
    m_overlayMapper->Modified();
    if (m_volume) {
        m_slice = m_volume->clampSlice(o, m_slice);
        m_scrollAnim.stop();
        m_planePos = m_slice * m_volume->spacing()[axisOf(o)];
        updatePlaneOrigin();
    }
    setupCamera();
    updateCornerText();
}

void SliceViewer::setObliqueAngles(double pitchDeg, double yawDeg)
{
    m_obliquePitch = pitchDeg;
    m_obliqueYaw   = yawDeg;
    if (!m_volume)
        return;
    // Base normal for the current orientation.
    double n[3] = {0, 0, 0};
    n[axisOf(m_orientation)] = 1.0;
    // In-plane axes: u = horizontal on screen, v = vertical on screen.
    // For axial: u=+X, v=-Y (anterior up). For sagittal: u=+Y, v=+Z.
    // For coronal:  u=+X, v=+Z.
    double u[3], v[3];
    switch (m_orientation) {
    case Orientation::Axial:    u[0]=1; u[1]=0; u[2]=0;  v[0]=0; v[1]=-1; v[2]=0; break;
    case Orientation::Sagittal: u[0]=0; u[1]=1; u[2]=0;  v[0]=0; v[1]=0;  v[2]=1; break;
    case Orientation::Coronal:  u[0]=1; u[1]=0; u[2]=0;  v[0]=0; v[1]=0;  v[2]=1; break;
    }
    // Rotate normal around v (pitch) then around u (yaw).
    const double pr = qDegreesToRadians(pitchDeg);
    const double yr = qDegreesToRadians(yawDeg);
    auto rotate = [](double* n, const double* axis, double rad) {
        const double c = std::cos(rad), s = std::sin(rad);
        const double k[3] = {axis[0], axis[1], axis[2]};
        const double dot = n[0]*k[0] + n[1]*k[1] + n[2]*k[2];
        double r[3];
        r[0] = n[0]*c + (k[1]*n[2] - k[2]*n[1])*s + k[0]*dot*(1-c);
        r[1] = n[1]*c + (k[2]*n[0] - k[0]*n[2])*s + k[1]*dot*(1-c);
        r[2] = n[2]*c + (k[0]*n[1] - k[1]*n[0])*s + k[2]*dot*(1-c);
        n[0]=r[0]; n[1]=r[1]; n[2]=r[2];
    };
    rotate(n, v, pr);
    rotate(n, u, yr);
    // Renormalize.
    const double mag = std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
    if (mag > 1e-9) { n[0]/=mag; n[1]/=mag; n[2]/=mag; }
    m_plane->SetNormal(n);
    m_plane->Modified();
    m_mapper->Modified();
    m_overlayMapper->Modified();
    m_fusionMapper->Modified();
    m_renderWindow->Render();
}

void SliceViewer::obliqueAngles(double& pitchDeg, double& yawDeg) const
{
    pitchDeg = m_obliquePitch;
    yawDeg   = m_obliqueYaw;
}

void SliceViewer::setupCamera()
{
    if (!m_volume)
        return;
    // Radiology display conventions (LPS volume space):
    //   Axial    — viewed from feet: +X on screen right, anterior (-Y) up
    //   Sagittal — viewed from patient left: posterior (+Y) on screen
    //              right, superior (+Z) up
    //   Coronal  — viewed from front: +X on screen right, +Z up
    double dir[3], up[3];
    switch (m_orientation) {
    case Orientation::Axial:    dir[0]=0;  dir[1]=0; dir[2]=1;
                                up[0]=0;   up[1]=-1; up[2]=0;   break;
    case Orientation::Sagittal: dir[0]=-1; dir[1]=0; dir[2]=0;
                                up[0]=0;   up[1]=0;  up[2]=1;   break;
    case Orientation::Coronal:  dir[0]=0;  dir[1]=1; dir[2]=0;
                                up[0]=0;   up[1]=0;  up[2]=1;   break;
    }

    const double* b = m_volume->vtkImage()->GetBounds();
    const double center[3] = {(b[0] + b[1]) / 2, (b[2] + b[3]) / 2,
                              (b[4] + b[5]) / 2};
    const double spans[3] = {b[1] - b[0], b[3] - b[2], b[5] - b[4]};
    const int axis = axisOf(m_orientation);
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    const double fitSpan = std::max(spans[u], spans[v]) / 2.0;
    const double dist = 2.0 * std::sqrt(spans[0] * spans[0] +
                                        spans[1] * spans[1] +
                                        spans[2] * spans[2]);

    auto* cam = m_renderer->GetActiveCamera();
    cam->ParallelProjectionOn();
    cam->SetFocalPoint(center);
    cam->SetPosition(center[0] - dir[0] * dist,
                     center[1] - dir[1] * dist,
                     center[2] - dir[2] * dist);
    cam->SetViewUp(up);
    cam->SetParallelScale(fitSpan);
    m_renderer->ResetCameraClippingRange();
    updateCrosshairActors();
    updateOrientationLabels();
}

// Anatomical direction labels (volume is canonical LPS: +X=L +Y=P +Z=S).
void SliceViewer::updateOrientationLabels()
{
    const bool on = m_volume != nullptr;
    const char* labels[4] = {"", "", "", ""};
    switch (m_orientation) {
    case Orientation::Axial:    // viewed from feet
        labels[0] = "R"; labels[1] = "L"; labels[2] = "A"; labels[3] = "P";
        break;
    case Orientation::Sagittal: // viewed from patient left
        labels[0] = "A"; labels[1] = "P"; labels[2] = "S"; labels[3] = "I";
        break;
    case Orientation::Coronal:  // viewed from front
        labels[0] = "R"; labels[1] = "L"; labels[2] = "S"; labels[3] = "I";
        break;
    }
    for (int i = 0; i < 4; ++i) {
        m_dirLabel[i]->SetInput(labels[i]);
        m_dirLabel[i]->SetVisibility(on);
    }
}

int SliceViewer::sliceCount() const
{
    return m_volume ? m_volume->sliceCount(m_orientation) : 0;
}

void SliceViewer::keyPressEvent(QKeyEvent* e)
{
    // Alt+Z clears all annotations on this viewer.
    if (e->key() == Qt::Key_Z && e->modifiers() == Qt::AltModifier) {
        clearAnnotations();
        return;
    }
    // Consume 'Z' here: it is our zoom modifier (hold + wheel to zoom).
    // Without this it leaks to the VTK interactor which flips the image.
    if (e->key() == Qt::Key_Z && e->modifiers() == Qt::NoModifier) {
        m_zoomKeyHeld = true;
        return;
    }
    // Oblique MPR rotation: O + arrows. O alone resets to orthogonal.
    if (e->key() == Qt::Key_O && e->modifiers() == Qt::NoModifier) {
        setObliqueAngles(0.0, 0.0);
        return;
    }
    if (e->modifiers() & Qt::AltModifier) {
        const double step = 5.0;
        switch (e->key()) {
        case Qt::Key_Left:  setObliqueAngles(m_obliquePitch, m_obliqueYaw - step); return;
        case Qt::Key_Right: setObliqueAngles(m_obliquePitch, m_obliqueYaw + step); return;
        case Qt::Key_Up:    setObliqueAngles(m_obliquePitch + step, m_obliqueYaw); return;
        case Qt::Key_Down:  setObliqueAngles(m_obliquePitch - step, m_obliqueYaw); return;
        default: break;
        }
    }
    QVTKOpenGLNativeWidget::keyPressEvent(e);
}

void SliceViewer::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Z) {
        m_zoomKeyHeld = false;
        return;
    }
    QVTKOpenGLNativeWidget::keyReleaseEvent(e);
}

bool SliceViewer::event(QEvent* e)
{
    // Touch gestures: pinch to zoom, pan to scroll slices.
    if (e->type() == QEvent::Gesture) {
        auto* g = static_cast<QGestureEvent*>(e);
        if (auto* pin = g->gesture(Qt::PinchGesture)) {
            const double sf = static_cast<QPinchGesture*>(pin)->scaleFactor();
            if (sf > 0.01 && std::abs(sf - 1.0) > 0.01)
                zoomBy(sf);
            e->accept();
            return true;
        }
        if (auto* pan = g->gesture(Qt::PanGesture)) {
            auto* pg = static_cast<QPanGesture*>(pan);
            // Vertical pan → scroll slices.
            const QPointF d = pg->delta();
            if (std::abs(d.y()) > 2.0) {
                const int dir = d.y() > 0 ? -1 : 1;
                setSlice(m_slice + dir * std::max(1, int(std::abs(d.y()) / 8)));
            }
            e->accept();
            return true;
        }
    }
    return QVTKOpenGLNativeWidget::event(e);
}

void SliceViewer::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        emit doubleClicked();
    QVTKOpenGLNativeWidget::mouseDoubleClickEvent(e);
}

void SliceViewer::zoomBy(double factor)
{
    auto* cam = m_renderer->GetActiveCamera();
    if (!cam)
        return;
    cam->SetParallelScale(cam->GetParallelScale() * factor);
    m_renderWindow->Render();
}

void SliceViewer::setSlice(int s)
{
    if (!m_volume)
        return;
    const int clamped = m_volume->clampSlice(m_orientation, s);
    const bool same = clamped == m_slice;
    m_slice = clamped;

    if (!m_scrollAnim.isActive())
        m_scrollAnim.start();

    updateCrosshairActors();
    updateCornerText();
    updateAnnotationVisibility();
    if (!same)
        emit sliceChanged(m_slice);
}

void SliceViewer::updatePlaneOrigin()
{
    const auto sp = m_volume->spacing();
    const int  a = axisOf(m_orientation);
    double origin[3] = {0, 0, 0};
    origin[a] = m_planePos;
    m_plane->SetOrigin(origin);
    m_plane->Modified();
    m_mapper->Modified();
    m_overlayMapper->Modified();
}

void SliceViewer::setWindowLevel(double width, double center)
{
    auto* prop = m_imageActor->GetProperty();
    prop->SetColorWindow(std::max(width, 1e-3));
    prop->SetColorLevel(center);
    updateCornerText();
    m_renderWindow->Render();
}

void SliceViewer::windowLevel(double& width, double& center) const
{
    auto* prop = m_imageActor->GetProperty();
    width = prop->GetColorWindow();
    center = prop->GetColorLevel();
}

void SliceViewer::autoWindowLevel()
{
    if (!m_volume)
        return;
    const auto& meta = m_volume->meta();
    if (meta.hasWindowing) {
        setWindowLevel(meta.windowWidth, meta.windowCenter);
    } else {
        auto r = m_volume->scalarRange();
        setWindowLevel(std::max(1.0, r[1] - r[0]), (r[0] + r[1]) * 0.5);
    }
    // Inverted grayscale for MONOCHROME1 images.
    if (meta.monochrome1) {
        auto lut = vtkSmartPointer<vtkLookupTable>::New();
        lut->SetRange(m_volume->scalarRange().data());
        lut->SetTableValue(0, 1, 1, 1, 1);
        lut->SetTableValue(255, 0, 0, 0, 1);
        lut->SetNumberOfTableValues(256);
        lut->Build();
        m_imageActor->GetProperty()->SetLookupTable(lut);
        m_imageActor->GetProperty()->SetUseLookupTableScalarRange(1);
    }
}

void SliceViewer::setActiveTool(Tool t)
{
    m_style->SetTool(t);
    if (t != Tool::Measure) {
        m_measureLine->SetVisibility(0);
        m_measureText->SetVisibility(0);
        m_renderWindow->Render();
    }
}

Tool SliceViewer::activeTool() const
{
    return m_style->GetTool();
}

void SliceViewer::setCinePlaying(bool on)
{
    if (on) m_cineTimer.start();
    else    m_cineTimer.stop();
}

void SliceViewer::setCineFps(int fps)
{
    m_cineTimer.setInterval(1000 / std::max(1, fps));
}

void SliceViewer::setShowCrosshair(bool on)
{
    m_showCrosshair = on;
    m_crossLineH->SetVisibility(on ? 1 : 0);
    m_crossLineV->SetVisibility(on ? 1 : 0);
    m_renderWindow->Render();
}

void SliceViewer::clearAnnotations()
{
    m_measureLine->SetVisibility(0);
    m_measureLine2->SetVisibility(0);
    m_roiRect->SetVisibility(0);
    m_measureText->SetVisibility(0);
    m_roiText->SetVisibility(0);
    m_annoMeasure = m_annoRoi = m_annoAngle = false;
    m_annoSlice = -1;
    m_lastAnno = LastAnno::None;
    m_renderWindow->Render();
    emit annotationsChanged();
}

void SliceViewer::undoAnnotation()
{
    switch (m_lastAnno) {
    case LastAnno::Measure:
        m_measureLine->SetVisibility(0);
        m_measureText->SetVisibility(0);
        m_annoMeasure = false;
        break;
    case LastAnno::Roi:
        m_roiRect->SetVisibility(0);
        m_roiText->SetVisibility(0);
        m_annoRoi = false;
        break;
    case LastAnno::Angle:
        m_measureLine->SetVisibility(0);
        m_measureLine2->SetVisibility(0);
        m_measureText->SetVisibility(0);
        m_annoAngle = false;
        break;
    case LastAnno::None:
        return;
    }
    m_lastAnno = LastAnno::None;
    m_renderWindow->Render();
    emit annotationsChanged();   // persist the removal
}

void SliceViewer::updateAnnotationVisibility()
{
    // Annotations live on the slice where they were drawn — hide them on
    // other slices instead of letting the label float over new anatomy.
    const bool here = (m_annoSlice == m_slice);
    m_measureLine->SetVisibility(here && (m_annoMeasure || m_annoAngle));
    m_measureLine2->SetVisibility(here && m_annoAngle);
    m_roiRect->SetVisibility(here && m_annoRoi);
    m_measureText->SetVisibility(here && (m_annoMeasure || m_annoAngle));
    m_roiText->SetVisibility(here && m_annoRoi);
}

void SliceViewer::setCrosshairIjk(const std::array<double,3>& ijk)
{
    m_crosshairIjk = ijk;
    updateCrosshairActors();
    m_renderWindow->Render();
}

void SliceViewer::setOverlay(vtkImageData* labelmap, vtkLookupTable* lut,
                             double opacity)
{
    m_labelmap = labelmap;
    if (!labelmap) {
        m_overlayActor->SetVisibility(0);
        m_overlayMapper->SetInputData(nullptr);
        m_renderWindow->Render();
        return;
    }
    auto colors = vtkSmartPointer<vtkImageMapToColors>::New();
    colors->SetInputData(labelmap);
    colors->SetLookupTable(lut);
    colors->PassAlphaToOutputOn();
    colors->Update();
    // Keep the filter alive via the mapper's input reference graph.
    m_overlayMapper->SetInputConnection(colors->GetOutputPort());
    m_overlayLut = lut;
    m_overlayActor->GetProperty()->SetOpacity(opacity);
    m_overlayActor->SetVisibility(1);
    m_renderWindow->Render();
}

void SliceViewer::setFusion(vtkImageData* img, vtkLookupTable* lut,
                            double opacity)
{
    m_fusionImg = img;
    m_fusionLut = lut;
    m_fusionOpacity = opacity;
    if (!img) {
        m_fusionActor->SetVisibility(0);
        m_fusionMapper->SetInputData(nullptr);
        m_renderWindow->Render();
        return;
    }
    auto colors = vtkSmartPointer<vtkImageMapToColors>::New();
    colors->SetInputData(img);
    colors->SetLookupTable(lut);
    colors->PassAlphaToOutputOn();
    colors->Update();
    m_fusionMapper->SetInputConnection(colors->GetOutputPort());
    m_fusionActor->GetProperty()->SetOpacity(opacity);
    m_fusionActor->SetVisibility(1);
    m_renderWindow->Render();
}

void SliceViewer::setFusionOpacity(double o)
{
    m_fusionOpacity = o;
    m_fusionActor->GetProperty()->SetOpacity(o);
    m_renderWindow->Render();
}

void SliceViewer::setInfoText(const QString& text)
{
    m_corner->SetText(2, text.toUtf8().constData());
    m_renderWindow->Render();
}

void SliceViewer::setFlipBadge(bool on)
{
    m_flipBadge->SetVisibility(on);
    m_renderWindow->Render();
}

void SliceViewer::setDownsampleBadge(bool on)
{
    m_downsampleBadge->SetVisibility(on);
    m_renderWindow->Render();
}

double SliceViewer::slicePlaneOffset() const
{
    return m_planePos;
}

void SliceViewer::setSmoothing(bool on)
{
    m_smoothing = on;
    if (m_volume)
        m_mapper->SetInputData(on ? m_volume->sharpenedVtk()
                                  : m_volume->vtkImage());
    m_renderWindow->Render();
}

void SliceViewer::setSlab(int type, double thicknessMm)
{
    m_slabType = type;
    m_slabMm = thicknessMm;
    switch (type) {
    case 1:  m_mapper->SetSlabTypeToMean(); break;
    case 2:  m_mapper->SetSlabTypeToMax();  break;
    case 3:  m_mapper->SetSlabTypeToMin();  break;
    default: break;
    }
    m_mapper->SetSlabThickness(type == 0 ? 0.0 : thicknessMm);
    updateCornerText();
    m_renderWindow->Render();
}

std::array<double,3> SliceViewer::pickPointOnPlane(int displayX,
                                                 int displayY) const
{
    std::array<double,3> out{0, 0, 0};
    if (!m_renderer)
        return out;

    auto toWorld = [&](double dispZ, double* w) {
        m_renderer->SetDisplayPoint(displayX, displayY, dispZ);
        m_renderer->DisplayToWorld();
        m_renderer->GetWorldPoint(w);
        // w[3] is homogeneous w; divide through.
        if (w[3] != 0.0) {
            w[0] /= w[3]; w[1] /= w[3]; w[2] /= w[3];
        }
    };
    double nearP[4], farP[4];
    toWorld(0.0, nearP);
    toWorld(1.0, farP);

    const int axis = axisOf(m_orientation);
    const double d = slicePlaneOffset();
    const double denom = farP[axis] - nearP[axis];
    const double t = (std::abs(denom) < 1e-9) ? 0.0 : (d - nearP[axis]) / denom;
    for (int i = 0; i < 3; ++i)
        out[i] = nearP[i] + t * (farP[i] - nearP[i]);
    return out;
}

void SliceViewer::updateCrosshairActors()
{
    if (!m_showCrosshair || !m_volume)
        return;

    const double* bounds = m_volume->vtkImage()->GetBounds();
    const auto sp = m_volume->spacing();
    const int axis = axisOf(m_orientation);
    const double plane = m_slice * sp[axis];

    // The other two axes map to this view's horizontal/vertical screen axes:
    //   Axial:    V at x (sagittal index), H at y (coronal index)
    //   Sagittal: V at y (coronal index),  H at z (axial index)
    //   Coronal:  V at x (sagittal index), H at z (axial index)
    int vAxis, hAxis;
    std::array<double,3> a, b;
    a[axis] = b[axis] = plane;

    switch (m_orientation) {
    case Orientation::Axial:    vAxis = 0; hAxis = 1; break;
    case Orientation::Sagittal: vAxis = 1; hAxis = 2; break;
    case Orientation::Coronal:  vAxis = 0; hAxis = 2; break;
    default: return;
    }

    const double cV = m_crosshairIjk[vAxis] * sp[vAxis];
    const double cH = m_crosshairIjk[hAxis] * sp[hAxis];

    // Vertical line: fixed vAxis coordinate, spanning hAxis.
    a[vAxis] = b[vAxis] = cV;
    a[hAxis] = bounds[2 * hAxis];
    b[hAxis] = bounds[2 * hAxis + 1];
    setLine(m_crossLineV, a, b);

    // Horizontal line: fixed hAxis coordinate, spanning vAxis.
    a[hAxis] = b[hAxis] = cH;
    a[vAxis] = bounds[2 * vAxis];
    b[vAxis] = bounds[2 * vAxis + 1];
    setLine(m_crossLineH, a, b);
}

void SliceViewer::updateCornerText()
{
    if (!m_volume)
        return;
    double w, c;
    windowLevel(w, c);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Slice %d / %d   Loc: %.1f mm",
                  m_slice + 1, sliceCount(),
                  m_slice * m_volume->spacing()[axisOf(m_orientation)]);
    m_corner->SetText(3, buf);
    if (m_slabType != 0 && m_slabMm > 0) {
        const char* names[] = {"", "AVG", "MIP", "MinIP"};
        std::snprintf(buf, sizeof(buf), "WW %.0f  WL %.0f   %s %.0fmm",
                      w, c, names[m_slabType], m_slabMm);
    } else {
        std::snprintf(buf, sizeof(buf), "WW %.0f  WL %.0f", w, c);
    }
    m_corner->SetText(0, buf);
}

void SliceViewer::updateMeasureActors(const std::array<double,3>& a,
                                      const std::array<double,3>& b)
{
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    const double mm = std::sqrt(dx * dx + dy * dy + dz * dz);

    const int axis = axisOf(m_orientation);
    const double plane = slicePlaneOffset();
    auto pa = a, pb = b;
    pa[axis] = pb[axis] = plane; // draw on the slice plane
    setLine(m_measureLine, pa, pb);
    m_measureLine->SetVisibility(1);
    m_annoSlice = m_slice;
    m_annoMeasure = true;
    m_measA = a;
    m_measB = b;
    m_lastAnno = LastAnno::Measure;
    emit annotationsChanged();

    // Anchor the label in world space — immune to viewport/projection
    // drift that misplaced 2D display-positioned text in full-screen.
    double lp[3] = {(pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2, plane};
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    lp[u] += 4.0;
    lp[v] += 4.0;
    m_measureText->SetPosition(lp);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f mm", mm);
    m_measureText->SetInput(buf);
    m_measureText->SetVisibility(1);
    m_renderWindow->Render();
    emit measured(mm);
}

void SliceViewer::updateRoiActors(const std::array<double,3>& a,
                                  const std::array<double,3>& b)
{
    if (!m_volume)
        return;
    const int   axis = axisOf(m_orientation);
    const auto  sp   = m_volume->spacing();
    const auto  ext  = m_volume->extent();
    const int   u    = (axis + 1) % 3, v = (axis + 2) % 3;
    const double plane = slicePlaneOffset();

    // Rectangle corners in world space on the slice plane.
    double lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        lo[i] = std::min(a[i], b[i]);
        hi[i] = std::max(a[i], b[i]);
    }
    const double p[5][3] = {
        {lo[0], lo[1], lo[2]}, {hi[0], lo[1], lo[2]},
        {hi[0], hi[1], hi[2]}, {lo[0], hi[1], hi[2]},
        {lo[0], lo[1], lo[2]}};
    for (int i = 0; i < 5; ++i) {
        double q[3] = {p[i][0], p[i][1], p[i][2]};
        q[axis] = plane;
        m_roiPts->SetPoint(i, q);
    }
    m_roiPts->Modified();
    m_roiRect->SetVisibility(1);
    m_annoSlice = m_slice;
    m_annoRoi = true;
    m_roiA = a;
    m_roiB = b;
    m_lastAnno = LastAnno::Roi;
    emit annotationsChanged();

    // Voxel stats inside the rectangle on this slice.
    std::array<double,3> ai, bi;
    m_volume->worldToIjk(a, ai);
    m_volume->worldToIjk(b, bi);
    const int iMin = std::clamp(int(std::floor(std::min(ai[u], bi[u]))),
                                0, ext[u] - 1);
    const int iMax = std::clamp(int(std::ceil(std::max(ai[u], bi[u]))),
                                0, ext[u] - 1);
    const int jMin = std::clamp(int(std::floor(std::min(ai[v], bi[v]))),
                                0, ext[v] - 1);
    const int jMax = std::clamp(int(std::ceil(std::max(ai[v], bi[v]))),
                                0, ext[v] - 1);
    const int k = std::clamp(int(std::lround(plane / sp[axis])),
                             0, ext[axis] - 1);

    double sum = 0, sum2 = 0, vmin = 1e30, vmax = -1e30;
    long   n = 0;
    int    idx[3];
    idx[axis] = k;
    for (int j = jMin; j <= jMax; ++j) {
        idx[v] = j;
        for (int i = iMin; i <= iMax; ++i) {
            idx[u] = i;
            const double s = m_volume->vtkImage()->GetScalarComponentAsDouble(
                idx[0], idx[1], idx[2], 0);
            sum += s;
            sum2 += s * s;
            vmin = std::min(vmin, s);
            vmax = std::max(vmax, s);
            ++n;
        }
    }
    if (n < 2) {
        m_roiText->SetVisibility(0);
        m_renderWindow->Render();
        return;
    }
    const double mean = sum / n;
    const double sd   = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
    const double area = n * sp[u] * sp[v];

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "ROI: %.0f +/- %.0f HU\n[%.0f .. %.0f]  %ld px  %.1f mm2",
                  mean, sd, vmin, vmax, n, area);
    m_roiText->SetInput(buf);
    {
        double lp[3] = {hi[0], hi[1], plane};
        lp[u] += 4.0;
        lp[v] += 4.0;
        m_roiText->SetPosition(lp);
    }
    m_roiText->SetVisibility(1);
    m_renderWindow->Render();
}

void SliceViewer::updateAngleActors(const std::array<double,3>& a,
                                    const std::array<double,3>& b,
                                    const std::array<double,3>& c)
{
    const int    axis  = axisOf(m_orientation);
    const double plane = slicePlaneOffset();
    auto pa = a, pb = b, pc = c;
    pa[axis] = pb[axis] = pc[axis] = plane;

    auto dist = [](const std::array<double,3>& p,
                   const std::array<double,3>& q) {
        const double dx = p[0]-q[0], dy = p[1]-q[1], dz = p[2]-q[2];
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    };
    m_annoSlice = m_slice;
    m_annoAngle = true;
    m_angA = a;
    m_angB = b;
    m_angC = c;
    m_lastAnno = LastAnno::Angle;
    emit annotationsChanged();

    // State detection by how many distinct points were clicked:
    //   a==b==c → 1 point (show marker line at A)
    //   b==c   → 2 points (show arm B→A only)
    //   else   → 3 points (full angle)
    if (dist(a, b) < 1e-6 && dist(b, c) < 1e-6) {
        // Only point A — draw a tiny stub so the user sees the click.
        auto stub = pa;
        stub[(axis + 1) % 3] += 2.0; // 2mm tick along first in-plane axis
        setLine(m_measureLine, pa, stub);
        m_measureLine->SetVisibility(1);
        m_measureLine2->SetVisibility(0);
        m_measureText->SetVisibility(0);
        m_renderWindow->Render();
        return;
    }

    // Always draw arm B→A once B is placed.
    setLine(m_measureLine, pb, pa);
    m_measureLine->SetVisibility(1);

    if (dist(b, c) < 1e-6) {
        // Only 2 points — no second arm yet, no angle text.
        m_measureLine2->SetVisibility(0);
        m_measureText->SetVisibility(0);
        m_renderWindow->Render();
        return;
    }

    // Full angle: both arms + degrees.
    setLine(m_measureLine2, pb, pc);
    m_measureLine2->SetVisibility(1);

    double v1[3], v2[3];
    for (int i = 0; i < 3; ++i) {
        v1[i] = a[i] - b[i];
        v2[i] = c[i] - b[i];
    }
    const double n1 = std::sqrt(v1[0]*v1[0] + v1[1]*v1[1] + v1[2]*v1[2]);
    const double n2 = std::sqrt(v2[0]*v2[0] + v2[1]*v2[1] + v2[2]*v2[2]);
    if (n1 < 1e-6 || n2 < 1e-6)
        return;
    const double dot = (v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2]) / (n1 * n2);
    const double deg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f\xc2\xb0", deg);
    m_measureText->SetInput(buf);
    {
        double lp[3] = {pb[0], pb[1], pb[2]};
        const int axis = axisOf(m_orientation);
        lp[(axis + 1) % 3] += 4.0;
        lp[(axis + 2) % 3] += 4.0;
        m_measureText->SetPosition(lp);
    }
    m_measureText->SetVisibility(1);
    m_renderWindow->Render();
    emit measured(deg);
}

void SliceViewer::setColorMap(int which)
{
    auto* prop = m_imageActor->GetProperty();
    if (which == 0) {
        prop->SetLookupTable(nullptr);
        prop->SetUseLookupTableScalarRange(0);
        m_renderWindow->Render();
        return;
    }

    m_colorLut = vtkSmartPointer<vtkLookupTable>::New();
    m_colorLut->SetNumberOfTableValues(256);
    m_colorLut->SetTableRange(0.0, 1.0); // input = windowed value

    switch (which) {
    case 1: // inverted grayscale
        m_colorLut->SetHueRange(0, 0);
        m_colorLut->SetSaturationRange(0, 0);
        m_colorLut->SetValueRange(1, 0);
        break;
    case 2: // hot iron (black -> red -> orange -> yellow -> white)
        for (int i = 0; i < 256; ++i) {
            const double t = i / 255.0;
            double r, g, b;
            if (t < 0.4)      { r = t / 0.4;             g = 0;                b = 0; }
            else if (t < 0.7) { r = 1; g = (t - 0.4) / 0.3;                   b = 0; }
            else              { r = 1; g = 1;             b = (t - 0.7) / 0.3; }
            m_colorLut->SetTableValue(i, r, g, b, 1.0);
        }
        break;
    case 3: // PET-style rainbow
        m_colorLut->SetHueRange(0.6667, 0.0);
        m_colorLut->SetSaturationRange(1, 1);
        m_colorLut->SetValueRange(1, 1);
        break;
    case 4: // bone-ish sepia
        for (int i = 0; i < 256; ++i) {
            const double t = i / 255.0;
            m_colorLut->SetTableValue(i,
                std::min(1.0, t * 1.05),
                std::min(1.0, t * 0.95 + 0.02 * t),
                std::min(1.0, t * 0.82), 1.0);
        }
        break;
    }
    m_colorLut->SetAlphaRange(1, 1);
    m_colorLut->Build();

    // LUT input is the normalized post-window/level value.
    prop->SetUseLookupTableScalarRange(0);
    prop->SetLookupTable(m_colorLut);
    m_renderWindow->Render();
}

namespace {

QJsonArray ptToJson(const std::array<double,3>& p)
{
    return {p[0], p[1], p[2]};
}

std::array<double,3> ptFromJson(const QJsonArray& a)
{
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
}

QJsonObject annoToJson(int slice, const std::array<double,3>& a,
                       const std::array<double,3>& b,
                       const std::array<double,3>& c)
{
    QJsonObject o;
    o["slice"] = slice;
    o["a"] = ptToJson(a);
    o["b"] = ptToJson(b);
    o["c"] = ptToJson(c);
    return o;
}

} // namespace

void SliceViewer::paintAt(const std::array<double,3>& world, bool erase)
{
    if (!m_volume)
        return;
    // Create an empty labelmap on first paint so editing always works.
    if (!m_labelmap) {
        m_labelmap = vtkSmartPointer<vtkImageData>::New();
        const auto ext = m_volume->extent();
        m_labelmap->SetDimensions(ext[0], ext[1], ext[2]);
        const auto sp = m_volume->spacing();
        m_labelmap->SetSpacing(sp[0], sp[1], sp[2]);
        m_labelmap->SetOrigin(0, 0, 0);
        m_labelmap->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
        m_labelmap->GetPointData()->GetScalars()->Fill(0);
        m_overlayLut = Segmentation::makeLabelLut(8);
        m_overlayLut->Build();
        setOverlay(m_labelmap, m_overlayLut, 0.5);
        emit overlayCreated(m_labelmap, m_overlayLut);
    }

    const unsigned char label = erase ? 0
                                      : static_cast<unsigned char>(
                                            std::clamp(m_editLabel, 0, 8));
    const int  axis = axisOf(m_orientation);
    const int  u = (axis + 1) % 3, v = (axis + 2) % 3;
    const auto ext = m_volume->extent();
    const auto sp  = m_volume->spacing();
    const double ru = std::max(1.0, m_brushMm / sp[u]);
    const double rv = std::max(1.0, m_brushMm / sp[v]);

    // Stamp a filled ellipse centered at voxel (iu, jv) on this slice.
    auto stamp = [&](int iu, int jv, int k) {
        int idx[3];
        idx[axis] = k;
        for (int dj = int(-rv); dj <= int(rv); ++dj) {
            const int j = jv + dj;
            if (j < 0 || j >= ext[v])
                continue;
            for (int di = int(-ru); di <= int(ru); ++di) {
                const int i = iu + di;
                if (i < 0 || i >= ext[u])
                    continue;
                const double e = (di / ru) * (di / ru) +
                                 (dj / rv) * (dj / rv);
                if (e > 1.0)
                    continue;
                idx[u] = i;
                idx[v] = j;
                m_labelmap->SetScalarComponentFromDouble(
                    idx[0], idx[1], idx[2], 0, label);
            }
        }
    };

    // Interpolate stamps along the stroke so fast drags stay solid.
    std::vector<std::array<double,3>> pts;
    if (m_paintHasLast) {
        const double dx = world[0] - m_lastPaint[0];
        const double dy = world[1] - m_lastPaint[1];
        const double dz = world[2] - m_lastPaint[2];
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        const double step = std::max(0.4 * m_brushMm, sp[u] * 0.5);
        const int    n = std::min(512, int(dist / step));
        pts.reserve(n + 1);
        for (int i = 1; i <= n; ++i) {
            const double t = double(i) / (n + 1);
            pts.push_back({m_lastPaint[0] + dx * t,
                           m_lastPaint[1] + dy * t,
                           m_lastPaint[2] + dz * t});
        }
    }
    pts.push_back(world);

    for (const auto& p : pts) {
        std::array<double,3> ijk;
        m_volume->worldToIjk(p, ijk);
        const int k = std::clamp(int(std::lround(ijk[axis])), 0,
                                 ext[axis] - 1);
        stamp(int(std::lround(ijk[u])), int(std::lround(ijk[v])), k);
    }
    m_lastPaint = world;
    m_paintHasLast = true;

    m_labelmap->GetPointData()->GetScalars()->Modified();
    m_labelmap->Modified();
    m_renderWindow->Render();
    emit overlayEdited();
}

QJsonObject SliceViewer::annotationsToJson() const
{
    QJsonObject o;
    if (m_annoMeasure)
        o["measure"] = annoToJson(m_annoSlice, m_measA, m_measB, m_measB);
    if (m_annoRoi)
        o["roi"] = annoToJson(m_annoSlice, m_roiA, m_roiB, m_roiB);
    if (m_annoAngle)
        o["angle"] = annoToJson(m_annoSlice, m_angA, m_angB, m_angC);
    return o;
}

void SliceViewer::annotationsFromJson(const QJsonObject& o)
{
    if (!m_volume || o.isEmpty())
        return;
    auto read = [this](const QJsonObject& a, const char* key,
                       std::array<double,3>* p) {
        const auto j = a.value(key).toArray();
        if (!j.isEmpty())
            *p = ptFromJson(j);
    };
    const QJsonObject m = o.value("measure").toObject();
    if (!m.isEmpty()) {
        m_annoSlice = m.value("slice").toInt();
        read(m, "a", &m_measA);
        read(m, "b", &m_measB);
        m_annoMeasure = true;
        updateMeasureActors(m_measA, m_measB);
    }
    const QJsonObject r = o.value("roi").toObject();
    if (!r.isEmpty()) {
        m_annoSlice = r.value("slice").toInt();
        read(r, "a", &m_roiA);
        read(r, "b", &m_roiB);
        m_annoRoi = true;
        updateRoiActors(m_roiA, m_roiB);
    }
    const QJsonObject g = o.value("angle").toObject();
    if (!g.isEmpty()) {
        m_annoSlice = g.value("slice").toInt();
        read(g, "a", &m_angA);
        read(g, "b", &m_angB);
        read(g, "c", &m_angC);
        m_annoAngle = true;
        updateAngleActors(m_angA, m_angB, m_angC);
    }
    updateAnnotationVisibility();
}

} // namespace meda
