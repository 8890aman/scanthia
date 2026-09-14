#include "VolumeWidget.h"

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLight.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkVolumeProperty.h>

#include <vtkCommand.h>
#include <vtkCallbackCommand.h>
#include <vtkRenderWindow.h>

namespace meda {

namespace {

/// Interaction observer — fires begin/end when the user starts/stops
/// rotating or zooming. Drives the LOD quality swap.
class InteractionObserver : public vtkCommand {
public:
    static InteractionObserver* New() { return new InteractionObserver; }
    void Execute(vtkObject*, unsigned long event, void*) override
    {
        if (!widget) return;
        if (event == vtkCommand::StartInteractionEvent)
            widget->beginInteraction();
        else if (event == vtkCommand::EndInteractionEvent)
            widget->endInteraction();
    }
    VolumeWidget* widget = nullptr;
};

} // namespace

VolumeWidget::VolumeWidget(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent)
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_renderWindow->SetMultiSamples(4);   // MSAA — smooth edges
    setRenderWindow(m_renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.05, 0.05, 0.08);
    m_renderWindow->AddRenderer(m_renderer);

    // Two-light rig: key + fill for depth cues on bone surfaces.
    m_renderer->SetTwoSidedLighting(1);
    auto light1 = vtkSmartPointer<vtkLight>::New();
    light1->SetPosition(0.5, -1.0, 0.8);
    light1->SetIntensity(0.9);
    m_renderer->AddLight(light1);
    auto light2 = vtkSmartPointer<vtkLight>::New();
    light2->SetPosition(-0.6, 0.4, 0.5);
    light2->SetIntensity(0.35);
    light2->SetAmbientColor(0.4, 0.45, 0.55);
    m_renderer->AddLight(light2);

    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    interactor()->SetInteractorStyle(style);

    // LOD: when interaction starts, drop sample distance for speed.
    // When it ends, restore full quality after a short idle window.
    auto obs = vtkSmartPointer<InteractionObserver>::New();
    obs->widget = this;
    interactor()->AddObserver(vtkCommand::StartInteractionEvent, obs);
    interactor()->AddObserver(vtkCommand::EndInteractionEvent, obs);

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(150);
    connect(&m_idleTimer, &QTimer::timeout, this,
            [this] { setLodQuality(false); });

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
        m_mapper = nullptr;
    }
    if (!vol)
        return;

    m_mapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    m_mapper->SetInputData(vol->vtkImage());
    m_mapper->SetBlendModeToComposite();
    m_mapper->SetAutoAdjustSampleDistances(1);
    // Stream large volumes as textures instead of one big upload.
    m_mapper->SetRequestedRenderModeToGPU();
    // Interactive budget: aim for ~30fps while rotating.
    m_mapper->SetInteractiveUpdateRate(30.0);
    m_mapper->SetInteractiveAdjustSampleDistances(1);

    auto prop = vtkSmartPointer<vtkVolumeProperty>::New();
    prop->SetInterpolationTypeToLinear();
    prop->ShadeOn();
    prop->SetAmbient(0.25);
    prop->SetDiffuse(0.65);
    prop->SetSpecular(0.30);
    prop->SetSpecularPower(15.0);

    m_volumeProp = vtkSmartPointer<vtkVolume>::New();
    m_volumeProp->SetMapper(m_mapper);
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

void VolumeWidget::beginInteraction()
{
    if (m_interacting)
        return;
    m_interacting = true;
    m_idleTimer.stop();
    setLodQuality(true);
}

void VolumeWidget::endInteraction()
{
    m_interacting = false;
    m_idleTimer.start();
}

void VolumeWidget::setLodQuality(bool interactive)
{
    if (!m_mapper)
        return;
    if (interactive) {
        // Coarse ray marching during rotate/zoom — fast.
        m_mapper->SetAutoAdjustSampleDistances(0);
        m_mapper->SetSampleDistance(
            m_volume ? m_volume->spacing()[0] * 4.0 : 4.0);
    } else {
        // Full quality when idle — sharp.
        m_mapper->SetAutoAdjustSampleDistances(1);
    }
    m_renderWindow->Render();
}

void VolumeWidget::applyCtPreset(const QString& preset)
{
    if (!m_volumeProp)
        return;
    auto* prop = m_volumeProp->GetProperty();
    auto color = vtkSmartPointer<vtkColorTransferFunction>::New();
    auto opacity = vtkSmartPointer<vtkPiecewiseFunction>::New();

    if (preset == "CT-Bone") {
        // Smooth sigmoid — bone surfaces, suppress soft tissue.
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(200, 0.65, 0.55, 0.45);
        color->AddRGBPoint(400, 0.92, 0.85, 0.72);
        color->AddRGBPoint(800, 1.0, 0.97, 0.88);
        color->AddRGBPoint(1500, 1.0, 1.0, 1.0);
        opacity->AddPoint(-100, 0.0);
        opacity->AddPoint(200, 0.0);
        opacity->AddPoint(300, 0.45);
        opacity->AddPoint(600, 0.85);
        opacity->AddPoint(1500, 1.0);
        m_mapper->SetBlendModeToComposite();
    } else if (preset == "CT-Lung") {
        color->AddRGBPoint(-1000, 0.05, 0.05, 0.08);
        color->AddRGBPoint(-900, 0.15, 0.20, 0.30);
        color->AddRGBPoint(-700, 0.35, 0.45, 0.55);
        color->AddRGBPoint(-400, 0.70, 0.55, 0.50);
        color->AddRGBPoint(-100, 0.90, 0.75, 0.65);
        color->AddRGBPoint(400, 1.0, 0.92, 0.85);
        opacity->AddPoint(-1024, 0.0);
        opacity->AddPoint(-900, 0.05);
        opacity->AddPoint(-700, 0.25);
        opacity->AddPoint(-400, 0.55);
        opacity->AddPoint(-100, 0.75);
        opacity->AddPoint(400, 0.90);
        m_mapper->SetBlendModeToComposite();
    } else if (preset == "CT-Angio") {
        // Vessels pop — bright contrast, suppress everything else.
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(0, 0.20, 0.15, 0.15);
        color->AddRGBPoint(100, 0.45, 0.20, 0.20);
        color->AddRGBPoint(300, 0.85, 0.35, 0.20);
        color->AddRGBPoint(600, 1.0, 0.55, 0.25);
        opacity->AddPoint(-100, 0.0);
        opacity->AddPoint(100, 0.0);
        opacity->AddPoint(200, 0.30);
        opacity->AddPoint(400, 0.80);
        opacity->AddPoint(600, 1.0);
        m_mapper->SetBlendModeToComposite();
    } else if (preset == "MR-T1") {
        color->AddRGBPoint(0, 0.0, 0.0, 0.0);
        color->AddRGBPoint(200, 0.30, 0.20, 0.25);
        color->AddRGBPoint(500, 0.65, 0.55, 0.50);
        color->AddRGBPoint(900, 0.95, 0.90, 0.85);
        color->AddRGBPoint(1500, 1.0, 1.0, 1.0);
        opacity->AddPoint(0, 0.0);
        opacity->AddPoint(100, 0.0);
        opacity->AddPoint(300, 0.25);
        opacity->AddPoint(700, 0.70);
        opacity->AddPoint(1500, 1.0);
        m_mapper->SetBlendModeToComposite();
    } else if (preset == "MR-T2") {
        // Bright fluid/edema, suppress CSF-dark.
        color->AddRGBPoint(0, 0.0, 0.0, 0.0);
        color->AddRGBPoint(100, 0.10, 0.15, 0.30);
        color->AddRGBPoint(400, 0.40, 0.55, 0.75);
        color->AddRGBPoint(800, 0.80, 0.85, 0.95);
        color->AddRGBPoint(1500, 1.0, 1.0, 1.0);
        opacity->AddPoint(0, 0.0);
        opacity->AddPoint(100, 0.0);
        opacity->AddPoint(300, 0.20);
        opacity->AddPoint(600, 0.60);
        opacity->AddPoint(1500, 1.0);
        m_mapper->SetBlendModeToComposite();
    } else if (preset == "MIP") {
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(0, 0.20, 0.20, 0.20);
        color->AddRGBPoint(500, 0.60, 0.60, 0.60);
        color->AddRGBPoint(2000, 1.0, 1.0, 1.0);
        opacity->AddPoint(-1000, 0.0);
        opacity->AddPoint(3000, 1.0);
        m_mapper->SetBlendModeToMaximumIntensity();
    } else { // CT-Soft default
        color->AddRGBPoint(-1000, 0.0, 0.0, 0.0);
        color->AddRGBPoint(-200, 0.40, 0.25, 0.22);
        color->AddRGBPoint(40, 0.70, 0.50, 0.42);
        color->AddRGBPoint(120, 0.85, 0.70, 0.58);
        color->AddRGBPoint(300, 0.95, 0.85, 0.72);
        color->AddRGBPoint(600, 1.0, 0.95, 0.88);
        color->AddRGBPoint(1500, 1.0, 1.0, 1.0);
        opacity->AddPoint(-1000, 0.0);
        opacity->AddPoint(-200, 0.0);
        opacity->AddPoint(40, 0.10);
        opacity->AddPoint(120, 0.30);
        opacity->AddPoint(300, 0.55);
        opacity->AddPoint(600, 0.80);
        opacity->AddPoint(1500, 0.95);
        m_mapper->SetBlendModeToComposite();
    }
    prop->SetColor(color);
    prop->SetScalarOpacity(opacity);
    // Gradient opacity suppresses homogeneous regions so we see surfaces,
    // not a semi-transparent brick.
    auto grad = vtkSmartPointer<vtkPiecewiseFunction>::New();
    grad->AddPoint(0.0, 0.0);
    grad->AddPoint(20.0, 0.05);
    grad->AddPoint(60.0, 0.35);
    grad->AddPoint(120.0, 0.75);
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
