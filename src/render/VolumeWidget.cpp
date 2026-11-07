#include "VolumeWidget.h"

#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkColorTransferFunction.h>
#include <vtkCoordinate.h>
#include <vtkAnnotatedCubeActor.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMatrix4x4.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPlaneCollection.h>
#include <vtkPlanes.h>
#include <vtkPlaneSource.h>
#include <vtkPoints.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
#include <vtkVolumeProperty.h>

#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPolygonF>

namespace meda {

VolumeWidget::VolumeWidget(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent)
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    setRenderWindow(m_renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.05, 0.05, 0.08);
    m_renderWindow->AddRenderer(m_renderer);

    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    interactor()->SetInteractorStyle(style);

    for (auto& a : m_planeActors) {
        auto src = vtkSmartPointer<vtkPlaneSource>::New();
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(src->GetOutputPort());
        a = vtkSmartPointer<vtkActor>::New();
        a->SetMapper(mapper);
        a->GetProperty()->SetOpacity(0.0);
        a->PickableOff();
        m_renderer->AddActor(a);
    }

    // Lasso overlay — display-space polyline, hidden until drawing.
    m_lassoPoly = vtkSmartPointer<vtkPolyData>::New();
    auto lassoMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
    auto lassoCoord = vtkSmartPointer<vtkCoordinate>::New();
    lassoCoord->SetCoordinateSystemToDisplay();
    lassoMapper->SetInputData(m_lassoPoly);
    lassoMapper->SetTransformCoordinate(lassoCoord);
    m_lassoActor = vtkSmartPointer<vtkActor2D>::New();
    m_lassoActor->SetMapper(lassoMapper);
    m_lassoActor->GetProperty()->SetColor(1.0, 0.55, 0.1);
    m_lassoActor->GetProperty()->SetLineWidth(2.0);
    m_lassoActor->GetProperty()->SetLineStipplePattern(0xF0F0);
    m_lassoActor->GetProperty()->SetLineStippleRepeatFactor(1);
    m_lassoActor->VisibilityOff();
    m_renderer->AddActor2D(
        static_cast<vtkProp*>(m_lassoActor.GetPointer()));

    // Crop box widget — created off; setCropBox arms it.
    m_boxWidget = vtkSmartPointer<vtkBoxWidget>::New();
    auto cb = vtkSmartPointer<vtkCallbackCommand>::New();
    cb->SetClientData(this);
    cb->SetCallback(+[](vtkObject*, unsigned long, void* cd, void*) {
        static_cast<VolumeWidget*>(cd)->applyCropBox();
    });
    m_boxWidget->AddObserver(vtkCommand::InteractionEvent, cb);

    // Orientation cube — LPS patient axes labelled like medecoro's
    // orientation menu (A/P/L/R/H/F snaps are snapView()).
    auto cube = vtkSmartPointer<vtkAnnotatedCubeActor>::New();
    cube->SetXPlusFaceText("L");
    cube->SetXMinusFaceText("R");
    cube->SetYPlusFaceText("P");
    cube->SetYMinusFaceText("A");
    cube->SetZPlusFaceText("H");
    cube->SetZMinusFaceText("F");
    cube->SetFaceTextScale(0.6);
    m_orientWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_orientWidget->SetOrientationMarker(cube);
    m_orientWidget->SetViewport(0.0, 0.0, 0.2, 0.2);
}

void VolumeWidget::setVolume(const VolumePtr& vol)
{
    m_volume = vol;
    m_carveImage = nullptr;
    m_srcImage = vol ? vol->vtkImage() : nullptr;
    m_lasso.clear();
    m_drawing = false;
    m_lassoActor->VisibilityOff();
    if (m_volumeProp) {
        m_renderer->RemoveVolume(m_volumeProp);
        m_volumeProp = nullptr;
    }
    if (!vol)
        return;

    auto mapper = vtkSmartPointer<vtkGPUVolumeRayCastMapper>::New();
    mapper->SetInputData(vol->vtkImage());
    mapper->SetBlendModeToComposite();
    mapper->SetAutoAdjustSampleDistances(1); // adapt while interacting
    mapper->SetUseJittering(1);              // anti-woodgrain banding

    auto prop = vtkSmartPointer<vtkVolumeProperty>::New();
    prop->SetInterpolationTypeToLinear();
    prop->ShadeOn();
    prop->SetAmbient(0.4);
    prop->SetDiffuse(0.6);
    prop->SetSpecular(0.2);

    m_volumeProp = vtkSmartPointer<vtkVolume>::New();
    m_volumeProp->SetMapper(mapper);
    m_volumeProp->SetProperty(prop);
    m_renderer->AddVolume(m_volumeProp);

    applyCtPreset("CT-Soft");
    for (auto& p : m_cutPlanes)    // old mapper held them; re-add below
        p = nullptr;
    for (auto& p : m_cropPlanes)
        p = nullptr;
    updateCutPlanes();
    resetCameraAnterior();
    if (m_cropArmed)
        m_boxWidget->PlaceWidget(vol->vtkImage()->GetBounds());
    updatePlaneActors();
    // The render window is realized by now — arm the orientation
    // cube if menu-build-time enable was skipped.
    setOrientationCube(m_cubeWanted);
    m_renderWindow->Render();
}

void VolumeWidget::resetCameraAnterior()
{
    m_renderer->ResetCamera();
    // Clinical default: anterior view, head up. Patient superior is +Z
    // in index space, anterior is -Y (LPS), so stand the camera in
    // front of the patient with Z as view-up.
    if (auto* cam = m_renderer->GetActiveCamera()) {
        const double* c = m_volumeProp->GetCenter();
        const double dist = cam->GetDistance();
        cam->SetViewUp(0, 0, 1);
        cam->SetPosition(c[0], c[1] - dist, c[2]);
        cam->SetFocalPoint(c[0], c[1], c[2]);
    }
}

void VolumeWidget::setPreset(const QString& preset)
{
    applyCtPreset(preset);
    m_renderWindow->Render();
}

void VolumeWidget::applyCtPreset(const QString& preset)
{
    if (!m_volumeProp)
        return;
    auto* prop = m_volumeProp->GetProperty();
    auto color = vtkSmartPointer<vtkColorTransferFunction>::New();
    auto opacity = vtkSmartPointer<vtkPiecewiseFunction>::New();
    auto* mapper =
        vtkGPUVolumeRayCastMapper::SafeDownCast(m_volumeProp->GetMapper());

    if (preset == "CT-Bone") {
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(300, 0.9, 0.85, 0.75);
        color->AddRGBPoint(1500, 1.0, 1.0, 1.0);
        opacity->AddPoint(-200, 0.0);
        opacity->AddPoint(300, 0.5);
        opacity->AddPoint(1500, 1.0);
        mapper->SetBlendModeToComposite();
    } else if (preset == "CT-Lung") {
        color->AddRGBPoint(-1000, 0.1, 0.1, 0.15);
        color->AddRGBPoint(-600, 0.35, 0.25, 0.3);
        color->AddRGBPoint(-200, 0.8, 0.6, 0.55);
        color->AddRGBPoint(400, 1.0, 0.9, 0.85);
        opacity->AddPoint(-1000, 0.0);
        opacity->AddPoint(-600, 0.15);
        opacity->AddPoint(-200, 0.5);
        opacity->AddPoint(400, 0.9);
        mapper->SetBlendModeToComposite();
    } else if (preset == "MIP") {
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(3000, 1.0, 1.0, 1.0);
        opacity->AddPoint(-1000, 0.0);
        opacity->AddPoint(3000, 1.0);
        mapper->SetBlendModeToMaximumIntensity();
    } else { // CT-Soft default
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(-200, 0.55, 0.35, 0.3);
        color->AddRGBPoint(60, 0.85, 0.6, 0.5);
        color->AddRGBPoint(300, 0.95, 0.85, 0.7);
        color->AddRGBPoint(1500, 1.0, 1.0, 1.0);
        opacity->AddPoint(-1000, 0.0);
        opacity->AddPoint(-200, 0.0);
        opacity->AddPoint(60, 0.15);
        opacity->AddPoint(300, 0.4);
        opacity->AddPoint(1500, 0.9);
        mapper->SetBlendModeToComposite();
    }
    prop->SetColor(color);
    prop->SetScalarOpacity(opacity);
    // Gradient opacity suppresses homogeneous regions so we see surfaces,
    // not a semi-transparent brick.
    auto grad = vtkSmartPointer<vtkPiecewiseFunction>::New();
    grad->AddPoint(0.0, 0.0);
    grad->AddPoint(40.0, 0.1);
    grad->AddPoint(80.0, 0.6);
    grad->AddPoint(200.0, 1.0);
    prop->SetGradientOpacity(grad);
}

void VolumeWidget::setCursor(const std::array<double,3>& ijk)
{
    m_cursor = ijk;
    updatePlaneActors();
}

void VolumeWidget::setShowPlanes(bool on)
{
    m_showPlanes = on;
    updatePlaneActors();
}

void VolumeWidget::setCutMask(unsigned mask)
{
    m_cutMask = mask;
    updateCutPlanes();
    m_renderWindow->Render();
}

void VolumeWidget::setFreeCut(bool on)
{
    m_freeCut = on;
    if (on) {
        // Kill-switch: disable the VTK interactor entirely. Trackball
        // orbit can't consume ANY input — mouse, wheel, or gestures —
        // regardless of which delivery path it arrives on. Our own Qt
        // mouse handlers still record the lasso.
        if (interactor())
            interactor()->Disable();
        QWidget::setCursor(Qt::CrossCursor);
    } else {
        m_drawing = false;
        m_lasso.clear();
        m_lassoActor->VisibilityOff();
        if (interactor())
            interactor()->Enable();
        QWidget::unsetCursor();
    }
    m_renderWindow->Render();
}

void VolumeWidget::restoreVolume()
{
    if (!m_volumeProp)
        return;
    auto* mapper = vtkGPUVolumeRayCastMapper::SafeDownCast(
        m_volumeProp->GetMapper());
    if (mapper && m_srcImage && m_carveImage) {
        mapper->SetInputData(m_srcImage);
        m_carveImage = nullptr;
        m_renderWindow->Render();
    }
}

void VolumeWidget::setCropBox(bool on)
{
    m_cropArmed = on;
    if (on && m_volume) {
        m_boxWidget->SetInteractor(interactor());
        m_boxWidget->SetProp3D(m_volumeProp);
        m_boxWidget->PlaceWidget(m_srcImage->GetBounds());
        m_boxWidget->On();
        applyCropBox();
    } else {
        m_boxWidget->Off();
        // Remove the six box planes from the mapper.
        auto* mapper = m_volumeProp
            ? vtkGPUVolumeRayCastMapper::SafeDownCast(
                  m_volumeProp->GetMapper())
            : nullptr;
        if (mapper)
            for (auto& p : m_cropPlanes) {
                if (p)
                    mapper->RemoveClippingPlane(p);
                p = nullptr;
            }
        else
            for (auto& p : m_cropPlanes)
                p = nullptr;
    }
    m_renderWindow->Render();
}

void VolumeWidget::applyCropBox()
{
    if (!m_volumeProp)
        return;
    auto* mapper = vtkGPUVolumeRayCastMapper::SafeDownCast(
        m_volumeProp->GetMapper());
    if (!mapper)
        return;
    auto planes = vtkSmartPointer<vtkPlanes>::New();
    m_boxWidget->GetPlanes(planes);
    auto* normals = planes->GetNormals();
    auto* points  = planes->GetPoints();
    if (!normals || !points)
        return;
    // vtkBoxWidget normals point outward; clipping keeps the positive
    // side, so flip them to keep the box interior.
    const int n = std::min(6, int(normals->GetNumberOfTuples()));
    for (int i = 0; i < n; ++i) {
        auto& p = m_cropPlanes[i];
        if (!p) {
            p = vtkSmartPointer<vtkPlane>::New();
            mapper->AddClippingPlane(p);
        }
        const double* nrm = normals->GetTuple3(i);
        const double* pt  = points->GetPoint(i);
        p->SetNormal(-nrm[0], -nrm[1], -nrm[2]);
        p->SetOrigin(pt);
    }
    m_renderWindow->Render();
}

void VolumeWidget::resetView()
{
    setFreeCut(false);
    setCropBox(false);
    setCutMask(0);
    restoreVolume();
    applyCtPreset("CT-Soft");
    if (m_volumeProp)
        resetCameraAnterior();
    m_renderWindow->Render();
}

void VolumeWidget::setOrientationCube(bool on)
{
    m_cubeWanted = on;
    auto* iren = interactor();
    if (!iren || !m_renderWindow ||
        !m_renderWindow->GetInitialized())
        return;   // render window not realized yet — setVolume retries
    m_orientWidget->SetInteractor(iren);
    m_orientWidget->SetEnabled(on);
    m_renderWindow->Render();
}

void VolumeWidget::snapView(char dir)
{
    if (!m_volumeProp)
        return;
    auto* cam = m_renderer->GetActiveCamera();
    if (!cam)
        return;
    const double* c = m_volumeProp->GetCenter();
    const double dist = cam->GetDistance();
    // Patient axes in index space: +X=L, +Y=P, +Z=H (LPS canonical).
    double pos[3], up[3] = {0, 0, 1};
    switch (dir) {
    case 'A': pos[0] = c[0]; pos[1] = c[1] - dist; pos[2] = c[2]; break;
    case 'P': pos[0] = c[0]; pos[1] = c[1] + dist; pos[2] = c[2]; break;
    case 'L': pos[0] = c[0] + dist; pos[1] = c[1]; pos[2] = c[2];
              up[0] = 0; up[1] = 0; up[2] = 1; break;
    case 'R': pos[0] = c[0] - dist; pos[1] = c[1]; pos[2] = c[2];
              up[0] = 0; up[1] = 0; up[2] = 1; break;
    case 'H': pos[0] = c[0]; pos[1] = c[1]; pos[2] = c[2] + dist;
              up[0] = 0; up[1] = -1; up[2] = 0; break;
    case 'F': pos[0] = c[0]; pos[1] = c[1]; pos[2] = c[2] - dist;
              up[0] = 0; up[1] = 1; up[2] = 0; break;
    default: return;
    }
    cam->SetPosition(pos);
    cam->SetFocalPoint(c[0], c[1], c[2]);
    cam->SetViewUp(up);
    m_renderWindow->Render();
}

void VolumeWidget::updateLassoActor()
{
    auto pts = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    const int n = static_cast<int>(m_lasso.size());
    lines->InsertNextCell(n);
    for (int i = 0; i < n; ++i) {
        pts->InsertNextPoint(m_lasso[i][0], m_lasso[i][1], 0.0);
        lines->InsertCellPoint(i);
    }
    m_lassoPoly->SetPoints(pts);
    m_lassoPoly->SetLines(lines);
    m_lassoPoly->Modified();
    m_lassoActor->SetVisibility(m_lasso.size() > 1);
    m_renderWindow->Render();
}

std::array<double,2> VolumeWidget::displayPos(const QPoint& p) const
{
    const int* rs = m_renderWindow->GetSize();
    const double sx = rs[0] / double(width());
    const double sy = rs[1] / double(height());
    return {p.x() * sx, (height() - 1 - p.y()) * sy};
}

void VolumeWidget::mousePressEvent(QMouseEvent* e)
{
    if (m_freeCut) {
        // Free Cut armed = camera LOCKED on every button. Left draws
        // the lasso; right/middle/wheel do nothing until the cut is
        // done or the tool is disarmed — the model must not move
        // while you're sculpting it.
        if (e->button() == Qt::LeftButton) {
            m_drawing = true;
            m_lasso.clear();
            m_lasso.push_back(displayPos(e->pos()));
        }
        e->accept();
        return;
    }
    QVTKOpenGLNativeWidget::mousePressEvent(e);
}

void VolumeWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drawing) {
        m_lasso.push_back(displayPos(e->pos()));
        updateLassoActor();
        e->accept();
        return;
    }
    if (m_freeCut) {          // armed but not drawing — swallow all
        e->accept();
        return;
    }
    QVTKOpenGLNativeWidget::mouseMoveEvent(e);
}

void VolumeWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_drawing && e->button() == Qt::LeftButton) {
        m_drawing = false;
        m_lassoActor->VisibilityOff();
        if (m_lasso.size() >= 3)
            carveLasso();
        m_lasso.clear();
        // Back to camera-orbit mode after every cut — one shot, not a
        // mode you stay in.
        setFreeCut(false);
        emit freeCutDone();
        e->accept();
        return;
    }
    if (m_freeCut) {          // swallowed press — matching release
        e->accept();
        return;
    }
    QVTKOpenGLNativeWidget::mouseReleaseEvent(e);
}

bool VolumeWidget::event(QEvent* e)
{
    // Touchpad pinch/pan gestures bypass mousePressEvent entirely —
    // swallow them too while sculpting.
    if (m_freeCut &&
        (e->type() == QEvent::Gesture ||
         e->type() == QEvent::TouchBegin ||
         e->type() == QEvent::TouchUpdate ||
         e->type() == QEvent::TouchEnd)) {
        e->accept();
        return true;
    }
    return QVTKOpenGLNativeWidget::event(e);
}

void VolumeWidget::wheelEvent(QWheelEvent* e)
{
    if (m_freeCut) {          // no zoom while sculpting
        e->accept();
        return;
    }
    QVTKOpenGLNativeWidget::wheelEvent(e);
}

void VolumeWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!m_freeCut && !m_drawing && e->button() == Qt::LeftButton)
        emit doubleClicked();
    e->accept();
}

void VolumeWidget::carveLasso()
{
    if (!m_volumeProp)
        return;
    auto* mapper = vtkGPUVolumeRayCastMapper::SafeDownCast(
        m_volumeProp->GetMapper());
    if (!mapper || !m_srcImage)
        return;
    // Carve on a detached copy so the MPR slices keep the real data.
    if (!m_carveImage) {
        m_carveImage = vtkSmartPointer<vtkImageData>::New();
        m_carveImage->DeepCopy(m_srcImage);
        mapper->SetInputData(m_carveImage);
    }
    vtkImageData* img = m_carveImage;
    int dims[3];
    img->GetDimensions(dims);
    const double* org = img->GetOrigin();
    const double* spc = img->GetSpacing();

    const int* rs = m_renderWindow->GetSize();
    const int w = rs[0], h = rs[1];
    if (w < 2 || h < 2)
        return;

    // Rasterize the lasso into a display-space mask (Qt coords: y down,
    // so display y is flipped when rasterizing and when sampling).
    QImage maskImg(w, h, QImage::Format_Grayscale8);
    maskImg.fill(0);
    QPolygonF poly;
    for (const auto& p : m_lasso)
        poly << QPointF(p[0], h - 1.0 - p[1]);
    {
        QPainter pt(&maskImg);
        pt.setPen(Qt::NoPen);
        pt.setBrush(Qt::white);
        pt.drawPolygon(poly);
    }
    const int stride = maskImg.bytesPerLine();
    const uchar* mask = maskImg.constBits();
    const QRectF bb = poly.boundingRect();

    // world -> clip 4x4 (projection * view; the volume sits at identity).
    auto* cam = m_renderer->GetActiveCamera();
    m_renderer->ComputeAspect();
    const double asp = m_renderer->GetAspect()[0] / m_renderer->GetAspect()[1];
    const double* cr = cam->GetClippingRange();
    auto comp = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkMatrix4x4::Multiply4x4(
        cam->GetProjectionTransformMatrix(asp, cr[0], cr[1]),
        cam->GetViewTransformMatrix(), comp);
    const double* M = &comp->Element[0][0];   // row-major 4x4

    auto* scalars = img->GetPointData()->GetScalars();
    double rng[2];
    scalars->GetRange(rng);
    const double air = rng[0];
    const double dxs = spc[0], dys = spc[1], dzs = spc[2];
    long carved = 0;

    for (int k = 0; k < dims[2]; ++k) {
        const double wz = org[2] + k * dzs;
        for (int j = 0; j < dims[1]; ++j) {
            const double wy = org[1] + j * dys;
            // Clip-space values advance linearly along the row.
            double cx = M[0] * org[0] + M[1] * wy + M[2] * wz + M[3];
            double cy = M[4] * org[0] + M[5] * wy + M[6] * wz + M[7];
            double cw = M[12] * org[0] + M[13] * wy + M[14] * wz + M[15];
            const double dcx = M[0] * dxs, dcy = M[4] * dxs, dcw = M[12] * dxs;
            for (int i = 0; i < dims[0]; ++i, cx += dcx, cy += dcy, cw += dcw) {
                if (cw <= 1e-9)
                    continue;
                const int px = int((cx / cw * 0.5 + 0.5) * w + 0.5);
                const int py = int((cy / cw * 0.5 + 0.5) * h + 0.5);
                if (px < bb.left() || px > bb.right() ||
                    py < (h - 1 - bb.bottom()) || py > (h - 1 - bb.top()) ||
                    px < 0 || py < 0 || px >= w || py >= h)
                    continue;
                if (mask[(h - 1 - py) * stride + px]) {
                    scalars->SetComponent(k * dims[0] * dims[1] + j * dims[0] + i,
                                          0, air);
                    ++carved;
                }
            }
        }
    }
    if (carved) {
        scalars->Modified();
        img->Modified();
        mapper->Modified();
    }
    m_renderWindow->Render();
}

void VolumeWidget::updateCutPlanes()
{
    if (!m_volume || !m_volumeProp)
        return;
    auto* mapper = vtkGPUVolumeRayCastMapper::SafeDownCast(
        m_volumeProp->GetMapper());
    if (!mapper)
        return;
    const auto sp = m_volume->spacing();
    // A clipping plane keeps the side its normal points into, so to
    // remove the +axis half the normal faces -axis. Each plane sits on
    // the crosshair position along its axis.
    const double sign[2] = {-1.0, 1.0};
    for (int a = 0; a < 3; ++a) {
        for (int s = 0; s < 2; ++s) {
            const unsigned bit = 1u << (2 * a + s);
            if (!(m_cutMask & bit)) {
                if (m_cutPlanes[2 * a + s])
                    mapper->RemoveClippingPlane(m_cutPlanes[2 * a + s]);
                continue;
            }
            auto& plane = m_cutPlanes[2 * a + s];
            if (!plane) {
                plane = vtkSmartPointer<vtkPlane>::New();
                double n[3] = {0.0, 0.0, 0.0};
                n[a] = sign[s];
                plane->SetNormal(n);
                mapper->AddClippingPlane(plane);
            }
            double o[3] = {0.0, 0.0, 0.0};
            o[a] = m_cursor[a] * sp[a];
            plane->SetOrigin(o);
        }
    }
}

void VolumeWidget::updatePlaneActors()
{
    if (!m_volume)
        return;
    const double* bounds = m_volume->vtkImage()->GetBounds();
    const auto sp = m_volume->spacing();
    const std::array<double,3> colors[3] = {
        {0.2, 0.9, 0.9}, {0.9, 0.7, 0.2}, {0.9, 0.3, 0.3}};
    const double opacity = m_showPlanes ? 0.15 : 0.0;

    for (int axis = 0; axis < 3; ++axis) {
        auto* src = vtkPlaneSource::SafeDownCast(
            vtkPolyDataMapper::SafeDownCast(m_planeActors[axis]->GetMapper())
                ->GetInputAlgorithm());
        const double c = m_cursor[axis] * sp[axis];
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;

        double o[3] = {bounds[0], bounds[2], bounds[4]};
        double p1[3] = {bounds[0], bounds[2], bounds[4]};
        double p2[3] = {bounds[0], bounds[2], bounds[4]};
        o[axis] = p1[axis] = p2[axis] = c;
        p1[u] = bounds[2 * u + 1];
        p2[v] = bounds[2 * v + 1];
        src->SetOrigin(o);
        src->SetPoint1(p1);
        src->SetPoint2(p2);
        src->Modified();
        m_planeActors[axis]->GetProperty()->SetColor(
            colors[axis][0], colors[axis][1], colors[axis][2]);
        m_planeActors[axis]->GetProperty()->SetOpacity(opacity);
    }
    updateCutPlanes();   // cuts ride on the crosshair
    m_renderWindow->Render();
}

} // namespace meda
