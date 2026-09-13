#include "VolumeWidget.h"

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkVolumeProperty.h>

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
}

void VolumeWidget::setVolume(const VolumePtr& vol)
{
    m_volume = vol;
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
    updatePlaneActors();
    m_renderWindow->Render();
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
    m_renderWindow->Render();
}

} // namespace meda
