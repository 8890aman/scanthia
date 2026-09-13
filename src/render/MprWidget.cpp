#include "MprWidget.h"

#include "SliceViewer.h"
#include "VolumeWidget.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QMainWindow>
#include <QEvent>

#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkRenderer.h>

#include <cmath>

namespace meda {

namespace {
QFrame* frameFor(QWidget* w)
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::StyledPanel);
    f->setLineWidth(2);
    f->setStyleSheet("QFrame { border: 1px solid #2E3540; }");
    auto* lay = new QVBoxLayout(f);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(w);
    return f;
}
} // namespace

/// clientData = CamLink* — tells us which camera fired.
struct MprWidget::CamLink {
    MprWidget* self;
    int        idx;
};

namespace {
void cameraCallback(vtkObject*, unsigned long, void* clientData, void*)
{
    auto* link = static_cast<MprWidget::CamLink*>(clientData);
    link->self->onCameraMoved(link->idx);
}
} // namespace

MprWidget::MprWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    const Orientation orients[3] = {
        Orientation::Axial, Orientation::Sagittal, Orientation::Coronal};
    for (int i = 0; i < 3; ++i) {
        m_views[i] = new SliceViewer(this);
        m_views[i]->setCineFps(15);
        m_frames[i] = frameFor(m_views[i]);
        const Orientation o = orients[i];
        connect(m_views[i], &SliceViewer::sliceChanged, this,
                [this, o](int s) { onViewSliceChanged(o, s); });
        connect(m_views[i], &SliceViewer::crosshairMoved, this,
                &MprWidget::onCrosshairMoved);
        connect(m_views[i], &SliceViewer::activated, this,
                [this, i] { setActiveView(m_views[i]); });
        connect(m_views[i], &SliceViewer::voxelHovered, this,
                &MprWidget::voxelHovered);
        connect(m_views[i], &SliceViewer::doubleClicked, this,
                [this, i] { toggleMaximize(m_views[i]); });
        connect(m_views[i], &SliceViewer::annotationsChanged, this,
                &MprWidget::annotationsChanged);
        // Paint in one pane → repaint all sibling panes.
        connect(m_views[i], &SliceViewer::overlayEdited, this,
                [this, i] {
                    for (int j = 0; j < 3; ++j)
                        if (j != i)
                            m_views[j]->renderer()->Render();
                });
        // A fresh labelmap created by painting must reach all panes.
        connect(m_views[i], &SliceViewer::overlayCreated, this,
                [this](vtkImageData* map, vtkLookupTable* lut) {
                    setOverlay(map, lut, 0.5);
                });
    }

    m_volumeView = new VolumeWidget(this);
    m_volumeFrame = frameFor(m_volumeView);

    grid->addWidget(m_frames[0], 0, 0); // axial
    grid->addWidget(m_frames[1], 0, 1); // sagittal
    grid->addWidget(m_frames[2], 1, 0); // coronal
    grid->addWidget(m_volumeFrame, 1, 1);
}

void MprWidget::toggleMaximize(SliceViewer* v)
{
    if (m_maximized == v) {
        // restore 2x2
        m_maximized = nullptr;
        for (auto* f : m_frames)
            f->setVisible(true);
        m_volumeFrame->setVisible(true);
    } else {
        m_maximized = v;
        for (int i = 0; i < 3; ++i)
            m_frames[i]->setVisible(m_views[i] == v);
        m_volumeFrame->setVisible(false);
    }
    // Force a resize/re-render pass on the visible view.
    for (auto* vw : m_views)
        vw->update();
}

void MprWidget::setVolume(const VolumePtr& vol)
{
    m_volume = vol;
    const Orientation orients[3] = {
        Orientation::Axial, Orientation::Sagittal, Orientation::Coronal};
    for (int i = 0; i < 3; ++i)
        m_views[i]->setVolume(vol, orients[i]);
    m_volumeView->setVolume(vol);

    if (vol) {
        auto ext = vol->extent();
        m_cursor = {ext[0] / 2.0, ext[1] / 2.0, ext[2] / 2.0};
        setCursor(m_cursor);
        if (m_overlay)
            setOverlay(m_overlay, m_overlayLut, m_overlayOpacity);
    }
    linkCameras();
}

void MprWidget::linkCameras()
{
    if (m_camLinked)
        return;
    for (int i = 0; i < 3; ++i) {
        auto* cam = m_views[i]->renderer()
                        ? m_views[i]->renderer()->GetActiveCamera()
                        : nullptr;
        if (!cam)
            continue;
        if (!m_camLink[i])
            m_camLink[i] = new CamLink{this, i};
        auto cb = vtkSmartPointer<vtkCallbackCommand>::New();
        cb->SetCallback(cameraCallback);
        cb->SetClientData(m_camLink[i]);
        cam->AddObserver(vtkCommand::ModifiedEvent, cb);
        cam->GetFocalPoint(m_lastFocal[i]);
    }
    m_camLinked = true;
}

void MprWidget::onCameraMoved(int src)
{
    if (m_linkGuard || !m_linkEnabled)
        return;
    auto* srcCam = m_views[src]->renderer()->GetActiveCamera();
    if (!srcCam)
        return;
    m_linkGuard = true;

    double focal[3];
    srcCam->GetFocalPoint(focal);
    const double scale = srcCam->GetParallelScale();
    const double delta[3] = {focal[0] - m_lastFocal[src][0],
                             focal[1] - m_lastFocal[src][1],
                             focal[2] - m_lastFocal[src][2]};
    m_lastFocal[src][0] = focal[0];
    m_lastFocal[src][1] = focal[1];
    m_lastFocal[src][2] = focal[2];

    for (int j = 0; j < 3; ++j) {
        if (j == src)
            continue;
        auto* cam = m_views[j]->renderer()->GetActiveCamera();
        if (!cam)
            continue;
        // Linked zoom: same parallel scale (same mm/pixel).
        cam->SetParallelScale(scale);
        // Linked pan: world delta projected onto this view's plane.
        double dir[3];
        cam->GetDirectionOfProjection(dir);
        const double dot = delta[0]*dir[0] + delta[1]*dir[1] +
                           delta[2]*dir[2];
        const double pd[3] = {delta[0] - dot*dir[0],
                              delta[1] - dot*dir[1],
                              delta[2] - dot*dir[2]};
        double f[3], p[3];
        cam->GetFocalPoint(f);
        cam->GetPosition(p);
        cam->SetFocalPoint(f[0]+pd[0], f[1]+pd[1], f[2]+pd[2]);
        cam->SetPosition(p[0]+pd[0], p[1]+pd[1], p[2]+pd[2]);
        m_views[j]->renderer()->Render();
        m_lastFocal[j][0] = f[0]+pd[0];
        m_lastFocal[j][1] = f[1]+pd[1];
        m_lastFocal[j][2] = f[2]+pd[2];
    }
    m_linkGuard = false;
}

void MprWidget::setCinePlaying(bool on)
{
    for (auto* v : m_views)
        v->setCinePlaying(on);
}

void MprWidget::setWindowLevel(double w, double c)
{
    for (auto* v : m_views)
        v->setWindowLevel(w, c);
}

void MprWidget::setCineFps(int fps)
{
    for (auto* v : m_views)
        v->setCineFps(fps);
}

void MprWidget::refresh()
{
    for (auto* v : m_views)
        if (v) v->refresh();
}

void MprWidget::setEditLabel(int label)
{
    for (auto* v : m_views)
        v->setEditLabel(label);
}

void MprWidget::setBrushRadiusMm(double mm)
{
    for (auto* v : m_views)
        v->setBrushRadiusMm(mm);
}

void MprWidget::setInfoText(const QString& text)
{
    for (auto* v : m_views)
        v->setInfoText(text);
}

void MprWidget::toggleDetach(int idx)
{
    QWidget* w = (idx < 3) ? static_cast<QWidget*>(m_views[idx])
                           : static_cast<QWidget*>(m_volumeView);
    QFrame*  f = (idx < 3) ? m_frames[idx] : m_volumeFrame;
    if (!w)
        return;

    if (!m_detached[idx]) {
        // Pop the viewer out of its frame into a floating window.
        f->layout()->removeWidget(w);
        auto* win = new QMainWindow(this, Qt::Window);
        static const char* titles[4] = {"Axial", "Sagittal", "Coronal",
                                        "3D Volume"};
        win->setWindowTitle(tr("Scanthia — %1").arg(titles[idx]));
        win->setCentralWidget(w);
        win->resize(800, 800);
        win->installEventFilter(this);
        m_detached[idx] = win;
        win->show();
    } else {
        // Reattach: put the viewer back into its grid frame.
        f->layout()->addWidget(w);
        m_detached[idx]->removeEventFilter(this);
        m_detached[idx]->deleteLater();
        m_detached[idx] = nullptr;
        w->show();
    }
}

bool MprWidget::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() == QEvent::Close) {
        for (int i = 0; i < 4; ++i)
            if (o == m_detached[i]) {
                toggleDetach(i);   // reattach instead of closing
                return true;
            }
    }
    return QWidget::eventFilter(o, e);
}

QJsonObject MprWidget::annotationsToJson() const
{
    QJsonObject o;
    static const char* keys[3] = {"axial", "sagittal", "coronal"};
    for (int i = 0; i < 3; ++i) {
        const auto j = m_views[i]->annotationsToJson();
        if (!j.isEmpty())
            o[keys[i]] = j;
    }
    return o;
}

void MprWidget::annotationsFromJson(const QJsonObject& o)
{
    m_views[0]->annotationsFromJson(o.value("axial").toObject());
    m_views[1]->annotationsFromJson(o.value("sagittal").toObject());
    m_views[2]->annotationsFromJson(o.value("coronal").toObject());
}

SliceViewer* MprWidget::viewer(Orientation o)
{
    return m_views[static_cast<int>(o)];
}

void MprWidget::setActiveTool(Tool t)
{
    for (auto* v : m_views)
        v->setActiveTool(t);
}

void MprWidget::setSlab(int type, double mm)
{
    for (auto* v : m_views)
        v->setSlab(type, mm);
}

void MprWidget::setSmoothing(bool on)
{
    for (auto* v : m_views)
        v->setSmoothing(on);
}

void MprWidget::setColorMap(int which)
{
    for (auto* v : m_views)
        v->setColorMap(which);
}

void MprWidget::clearAnnotations()
{
    for (auto* v : m_views)
        v->clearAnnotations();
}

void MprWidget::undoLastAnnotation()
{
    for (auto* v : m_views)
        v->undoAnnotation();
}

void MprWidget::setCrosshairVisible(bool on)
{
    for (auto* v : m_views)
        v->setShowCrosshair(on);
}

void MprWidget::setOverlay(vtkImageData* labelmap, vtkLookupTable* lut,
                           double opacity)
{
    m_overlay = labelmap;
    m_overlayLut = lut;
    m_overlayOpacity = opacity;
    for (auto* v : m_views)
        v->setOverlay(labelmap, lut, opacity);
}

void MprWidget::setFusion(vtkImageData* img, vtkLookupTable* lut,
                          double opacity)
{
    for (auto* v : m_views)
        v->setFusion(img, lut, opacity);
}

void MprWidget::setFusionOpacity(double o)
{
    for (auto* v : m_views)
        v->setFusionOpacity(o);
}

void MprWidget::setObliqueAngles(double pitchDeg, double yawDeg)
{
    if (m_activeView)
        m_activeView->setObliqueAngles(pitchDeg, yawDeg);
}

void MprWidget::resetOblique()
{
    for (auto* v : m_views)
        v->setObliqueAngles(0.0, 0.0);
}

void MprWidget::setCursor(const std::array<double,3>& ijk)
{
    if (!m_volume)
        return;
    m_cursor = ijk;
    const Orientation orients[3] = {
        Orientation::Axial, Orientation::Sagittal, Orientation::Coronal};
    for (int i = 0; i < 3; ++i) {
        auto* v = m_views[i];
        v->setSlice(static_cast<int>(std::lround(
            ijk[static_cast<size_t>(axisOf(orients[i]))])));
        v->setCrosshairIjk(ijk);
    }
    m_volumeView->setCursor(ijk);
}

void MprWidget::onViewSliceChanged(Orientation o, int slice)
{
    m_cursor[static_cast<size_t>(axisOf(o))] = slice;
    // Moving one plane's slice moves the crosshair lines in the other views.
    for (int i = 0; i < 3; ++i)
        if (static_cast<int>(o) != i)
            m_views[i]->setCrosshairIjk(m_cursor);
    m_volumeView->setCursor(m_cursor);
}

void MprWidget::onCrosshairMoved(std::array<double,3> ijk)
{
    setCursor(ijk);
}

void MprWidget::setActiveView(SliceViewer* v)
{
    m_activeView = v;
    for (int i = 0; i < 3; ++i) {
        m_frames[i]->setStyleSheet(m_views[i] == v
            ? "QFrame { border: 1px solid #4DA3E8; }"
            : "QFrame { border: 1px solid #2E3540; }");
    }
}

} // namespace meda
