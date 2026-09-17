#include "SliceViewer.h"

#include "SliceInteractionStyle.h"
#include "Segmentation.h"

#include <vtkProperty2D.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkCoordinate.h>
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

#include <QColor>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGestureEvent>
#include <QResizeEvent>
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

    // Annotations live on a separate overlay layer rendered AFTER the
    // image layer — they can never end up "behind" the slice or a
    // thick slab (the reported z-index issue). Shares the camera.
    m_overlay = vtkSmartPointer<vtkRenderer>::New();
    m_overlay->SetLayer(1);
    m_renderWindow->SetNumberOfLayers(2);
    m_renderWindow->AddRenderer(m_overlay);
    m_overlay->SetActiveCamera(m_renderer->GetActiveCamera());

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
    m_renderer->AddActor(m_crossLineH);
    m_renderer->AddActor(m_crossLineV);

    // Corner metadata: 4 plain text actors (BL BR TL TR) — reliable
    // across render-layer changes where CornerAnnotation proved flaky.
    const double hudPos[4][2] = {{0.012, 0.035}, {0.988, 0.035},
                                 {0.012, 0.965}, {0.988, 0.965}};
    for (int i = 0; i < 4; ++i) {
        m_hud[i] = vtkSmartPointer<vtkTextActor>::New();
        m_hud[i]->GetPositionCoordinate()
            ->SetCoordinateSystemToNormalizedViewport();
        m_hud[i]->SetPosition(hudPos[i][0], hudPos[i][1]);
        auto* tp = m_hud[i]->GetTextProperty();
        tp->SetFontSize(11);
        tp->SetColor(0.90, 0.92, 0.94);
        tp->SetJustification(i % 2 == 0
                                 ? VTK_TEXT_LEFT : VTK_TEXT_RIGHT);
        tp->SetVerticalJustification(i < 2
                                         ? VTK_TEXT_BOTTOM : VTK_TEXT_TOP);
        m_renderer->AddActor2D(m_hud[i]);
    }

    // Physical scale ruler: a fixed "nice" length bar (20 cm, 10 cm,
    // 5 mm…) drawn in display pixels — dark outline pass under a white
    // pass � outlined strokes for readability. Rebuilt on zoom/resize.
    m_rulerPts = vtkSmartPointer<vtkPoints>::New();
    m_rulerPd  = vtkSmartPointer<vtkPolyData>::New();
    m_rulerPd->SetPoints(m_rulerPts);
    {
        auto coord = vtkSmartPointer<vtkCoordinate>::New();
        coord->SetCoordinateSystemToDisplay();
        auto mapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
        mapper->SetInputData(m_rulerPd);
        mapper->SetTransformCoordinate(coord);
        m_rulerDark = vtkSmartPointer<vtkActor2D>::New();
        m_rulerDark->SetMapper(mapper);
        m_rulerDark->GetProperty()->SetColor(0, 0, 0);
        m_rulerDark->GetProperty()->SetLineWidth(4.0f);
        m_rulerDark->GetProperty()->SetOpacity(0.85);
        m_rulerLight = vtkSmartPointer<vtkActor2D>::New();
        m_rulerLight->SetMapper(mapper);
        m_rulerLight->GetProperty()->SetColor(0.93, 0.94, 0.96);
        m_rulerLight->GetProperty()->SetLineWidth(1.3f);
        m_rulerDark->SetVisibility(0);
        m_rulerLight->SetVisibility(0);
        m_renderer->AddActor2D(m_rulerDark);
        m_renderer->AddActor2D(m_rulerLight);

        auto makeLabel = [this]() {
            auto t = vtkSmartPointer<vtkTextActor>::New();
            auto* tp = t->GetTextProperty();
            tp->SetColor(0.93, 0.94, 0.96);
            tp->SetFontSize(11);
            tp->ShadowOn();           // dark offset behind the text
            tp->SetShadowOffset(1, -1);
            t->SetVisibility(0);
            m_renderer->AddActor2D(t);
            return t;
        };
        m_rulerLabelH = makeLabel();
        m_rulerLabelV = makeLabel();
    }

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

    // Orange "FUSION" marker — compact 2-line block, top-centre: clear
    // of the corner metadata and the edge rulers.
    m_fusionBadge = vtkSmartPointer<vtkTextActor>::New();
    auto* ftp = m_fusionBadge->GetTextProperty();
    ftp->SetColor(1.0, 0.55, 0.20);
    ftp->SetFontSize(10);
    ftp->BoldOn();
    ftp->ShadowOn();
    ftp->SetShadowOffset(1, -1);
    ftp->SetJustificationToCentered();
    ftp->SetVerticalJustificationToTop();
    m_fusionBadge->GetPositionCoordinate()
        ->SetCoordinateSystemToNormalizedViewport();
    m_fusionBadge->SetPosition(0.5, 0.975);
    m_fusionBadge->SetVisibility(0);
    m_renderer->AddActor2D(m_fusionBadge);

    // Orientation labels on the 4 view edges (R/L/A/P/H/F). Inset far
    // enough to clear the scale-ruler bars at the view edges.
    const double pos[4][2] = {{0.055, 0.5}, {0.945, 0.5}, {0.5, 0.93},
                              {0.5, 0.065}};
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
        m_lodIdleTimer.start();   // schedule HQ re-render
    };
    m_style->onWindowLevelStarted = [this] {
        windowLevel(m_wlStartW, m_wlStartC);
        beginInteraction();
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
    m_style->onMeasureStart = [this](int kind) { beginAnno(kind); };
    m_style->onMeasureGrab  = [this](std::array<double,3> p) {
        const auto hit = pickAnnoHandle(p);
        if (hit.first >= 0) {
            m_editAnno = hit.first;
            m_editPt   = hit.second;
            return true;
        }
        return false;
    };
    m_style->onMeasureEdit = [this](std::array<double,3> p) {
        editAnnoPoint(m_editAnno, m_editPt, p);
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
    m_style->onInteractionBegin = [this] { beginInteraction(); };
    m_style->onInteractionEnd   = [this] { m_lodIdleTimer.start(); };

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

    // LOD: 150ms after the last interaction, restore full-quality rendering.
    m_lodIdleTimer.setSingleShot(true);
    m_lodIdleTimer.setInterval(150);
    connect(&m_lodIdleTimer, &QTimer::timeout, this, [this] {
        if (m_interacting)
            endInteraction();
    });
}

void SliceViewer::beginInteraction()
{
    if (m_interacting)
        return;
    m_interacting = true;
    // Tell VTK to prioritize frame rate over quality while interacting.
    // The render window's desired update rate controls how VTK trades
    // quality for FPS — high rate = coarser but faster rendering.
    m_renderWindow->SetDesiredUpdateRate(30.0);
    if (auto* iren = interactor())
        iren->SetDesiredUpdateRate(30.0);
}

void SliceViewer::endInteraction()
{
    if (!m_interacting)
        return;
    m_interacting = false;
    // Full-quality render: zero desired rate = still/quality mode.
    m_renderWindow->SetDesiredUpdateRate(0.0);
    if (auto* iren = interactor())
        iren->SetDesiredUpdateRate(0.0001);
    m_renderWindow->Render();
}

void SliceViewer::setScaleVisible(bool on)
{
    m_scaleVisible = on;
    updateScaleRuler();
    m_renderWindow->Render();
}

// Physical ruler: a fixed "nice" physical length (1-2-5 sequence)
// drawn as a centred bar on the bottom and left view edges, with
// end caps, mid ticks and 1/10 subticks. Rebuilt whenever the zoom or
// window size changes. Hidden for unscaled volumes — labels would lie.
void SliceViewer::updateScaleRuler()
{
    auto hideAll = [this] {
        m_rulerDark->SetVisibility(0);
        m_rulerLight->SetVisibility(0);
        m_rulerLabelH->SetVisibility(0);
        m_rulerLabelV->SetVisibility(0);
    };
    if (!m_scaleVisible || !m_volume ||
        !m_volume->spacingCalibrated() ||
        !m_imageActor->GetVisibility()) {
        hideAll();
        return;
    }
    auto* cam = m_renderer->GetActiveCamera();
    if (!cam || !cam->GetParallelProjection()) {
        hideAll();
        return;
    }
    const int* sz = m_renderWindow->GetSize();
    if (sz[0] < 160 || sz[1] < 160) {
        hideAll();
        return;
    }
    const double mmPerPx = cam->GetParallelScale() * 2.0 / sz[1];

    // Largest 1-2-5 length (mm) whose on-screen size fits maxPx.
    auto niceBar = [](double mmPerPx, double maxPx, double& mm) {
        const double maxMm = maxPx * mmPerPx;
        if (maxMm <= 0.0) { mm = 0.0; return 0.0; }
        double len = std::pow(10.0, int(std::log10(maxMm)) + 1);
        double px  = len / mmPerPx;
        for (int guard = 0; px > maxPx && guard < 60; ++guard) {
            const double first =
                len / std::pow(10.0, std::floor(std::log10(len)) + 1e-12);
            len /= (first > 4.9 && first < 5.1) ? 2.5 : 2.0;
            px = len / mmPerPx;
        }
        if (px > maxPx) { mm = 0.0; return 0.0; }
        mm = len;
        return px;
    };
    // "20 cm" style label; also yields the tick divisor (
    // labels containing 5 → 5 divisions, containing 2 → 2, else 10).
    auto label = [](double mm, int& divisor) {
        QString s;
        if (mm >= 10.0) {
            const double cm = mm / 10.0;
            s = (cm == int(cm) ? QString::number(int(cm))
                               : QString::number(cm)) + " cm";
        } else {
            s = (mm == int(mm) ? QString::number(int(mm))
                               : QString::number(mm, 'f', 1)) + " mm";
        }
        divisor = s.contains('5') ? 5 : s.contains('2') ? 2 : 10;
        return s;
    };

    const int  axis = axisOf(m_orientation);
    const int  u = (axis + 1) % 3, v = (axis + 2) % 3;
    const auto ext = m_volume->extent();
    const auto sp  = m_volume->spacing();
    const double imgWpx = ext[u] * sp[u] / mmPerPx;
    const double imgHpx = ext[v] * sp[v] / mmPerPx;

    const double capLen = 15.0, midLen = 10.0, subLen = 5.0;
    const double edge = 16.0;

    m_rulerPts->Reset();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    auto seg = [&](double x0, double y0, double x1, double y1) {
        const vtkIdType ids[2] = {m_rulerPts->InsertNextPoint(x0, y0, 0),
                                  m_rulerPts->InsertNextPoint(x1, y1, 0)};
        cells->InsertNextCell(2, ids);
    };

    bool anyBar = false;
    // --- Bottom bar, centred on the bottom edge ---
    double mmH = 0.0;
    const double barH = niceBar(mmPerPx, std::min(imgWpx, sz[0] / 2.0),
                                mmH);
    if (barH > 50.0) {
        int div;
        const QString s = label(mmH, div);
        const double x0 = sz[0] / 2.0 - barH / 2.0, y0 = edge;
        seg(x0, y0, x0 + barH, y0);
        seg(x0, y0, x0, y0 + capLen);
        seg(x0 + barH, y0, x0 + barH, y0 + capLen);
        const double step = barH / div;
        for (int i = 1; i < div; ++i)
            seg(x0 + step * i, y0, x0 + step * i, y0 + midLen);
        if (step > 90.0) {
            const double sub = step / 10.0;
            for (int i = 0; i < div; ++i)
                for (int k = 1; k < 10; ++k)
                    seg(x0 + step * i + sub * k, y0,
                        x0 + step * i + sub * k, y0 + subLen);
        }
        m_rulerLabelH->SetInput(s.toUtf8().constData());
        m_rulerLabelH->SetDisplayPosition(int(x0 + barH + 6),
                                          int(y0 - 2));
        m_rulerLabelH->SetVisibility(1);
        anyBar = true;
    } else {
        m_rulerLabelH->SetVisibility(0);
    }

    // --- Left bar, centred on the left edge ---
    double mmV = 0.0;
    const double barV = niceBar(mmPerPx, std::min(imgHpx, sz[1] / 2.0),
                                mmV);
    if (barV > 30.0) {
        int div;
        const QString s = label(mmV, div);
        const double x0 = edge, y0 = sz[1] / 2.0 - barV / 2.0;
        seg(x0, y0, x0, y0 + barV);
        seg(x0, y0, x0 + capLen, y0);
        seg(x0, y0 + barV, x0 + capLen, y0 + barV);
        const double step = barV / div;
        for (int i = 1; i < div; ++i)
            seg(x0, y0 + step * i, x0 + midLen, y0 + step * i);
        if (step > 90.0) {
            const double sub = step / 10.0;
            for (int i = 0; i < div; ++i)
                for (int k = 1; k < 10; ++k)
                    seg(x0, y0 + step * i + sub * k,
                        x0 + subLen, y0 + step * i + sub * k);
        }
        m_rulerLabelV->SetInput(s.toUtf8().constData());
        m_rulerLabelV->SetDisplayPosition(int(x0 + 4),
                                          int(y0 + barV + 6));
        m_rulerLabelV->SetVisibility(1);
        anyBar = true;
    } else {
        m_rulerLabelV->SetVisibility(0);
    }

    if (!anyBar) {
        hideAll();
        return;
    }
    m_rulerPd->SetLines(cells);
    m_rulerPd->Modified();
    m_rulerDark->SetVisibility(1);
    m_rulerLight->SetVisibility(1);
}

void SliceViewer::setVolume(const VolumePtr& vol, Orientation o)
{
    m_volume = vol;
    m_imageActor->SetVisibility(vol != nullptr);
    if (!vol) {
        updateScaleRuler();
        return;
    }
    // Crosshair starts at the volume centre, not the corner.
    const auto ext = vol->extent();
    m_crosshairIjk = {ext[0] / 2.0, ext[1] / 2.0, ext[2] / 2.0};
    m_mapper->SetInputData(m_smoothing ? vol->sharpenedVtk()
                                       : vol->vtkImage());
    setOrientation(o);
    setSlice(vol->clampSlice(o, vol->sliceCount(o) / 2));
    m_scrollAnim.stop();
    m_planePos = m_slice * vol->spacing()[axisOf(o)];
    updatePlaneOrigin();
    clearAnnotations();
    autoWindowLevel();
    // setupCamera (via setOrientation) already centres + fits; a
    // ResetCamera here would refit without the margin.
    updateScaleRuler();
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
    // Fit BOTH axes inside the view (parallel scale governs height) with
    // a small margin so the image sits centred with breathing room.
    const int* sz = m_renderWindow->GetSize();
    const double aspect = (sz[1] > 0) ? double(sz[0]) / sz[1] : 1.0;
    const double fitSpan =
        std::max(spans[v] / 2.0, spans[u] / (2.0 * aspect)) * 1.08;
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
    updateScaleRuler();
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
    updateScaleRuler();
    m_renderWindow->Render();
}

void SliceViewer::resizeEvent(QResizeEvent* e)
{
    QVTKOpenGLNativeWidget::resizeEvent(e);
    updateScaleRuler();   // mm-per-pixel changed → rebuild the ruler
}

void SliceViewer::setSlice(int s)
{
    if (!m_volume)
        return;
    const int clamped = m_volume->clampSlice(m_orientation, s);
    const bool same = clamped == m_slice;
    m_slice = clamped;

    // Interactive LOD: fast rendering while scrolling, HQ when idle.
    beginInteraction();
    m_lodIdleTimer.start();

    if (m_cineTimer.isActive()) {
        // Cine: snap straight to the slice — the easing animation costs
        // extra renders per frame and judders at playback speed.
        m_scrollAnim.stop();
        m_planePos = clamped * m_volume->spacing()[axisOf(m_orientation)];
        updatePlaneOrigin();
        m_renderWindow->Render();
    } else if (!m_scrollAnim.isActive()) {
        m_scrollAnim.start();
    }

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
        // Percentile fit — excludes pixel padding and hot-voxel outliers.
        auto r = m_volume->autoWindowRange();
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
    const bool measTool =
        (t == Tool::Measure || t == Tool::Roi || t == Tool::Angle);
    // Handles show only while the shape can be edited (a measure
    // tool active + shape on this slice); the shape itself persists.
    for (auto& an : m_annos) {
        const bool on = measTool && an.slice == m_slice;
        an.handles->SetVisibility(on);
        an.handlesInner->SetVisibility(on);
    }
    // Drop a degenerate draft (press with no drag → zero-length line).
    if (m_draftAnno >= 0 && m_draftAnno < int(m_annos.size())) {
        auto& an = m_annos[size_t(m_draftAnno)];
        bool degenerate = an.npts == 0;
        if (!degenerate && an.kind != 2) {
            const double dx = an.p[0][0]-an.p[1][0],
                         dy = an.p[0][1]-an.p[1][1],
                         dz = an.p[0][2]-an.p[1][2];
            degenerate = std::sqrt(dx*dx + dy*dy + dz*dz) < 1e-6;
        }
        if (degenerate) {
            removeAnnoActors(an);
            m_annos.erase(m_annos.begin() + m_draftAnno);
        }
    }
    m_draftAnno = m_editAnno = -1;
    m_renderWindow->Render();
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
    // Toggling on with no prior position → centre of the volume.
    if (on && m_volume && m_crosshairIjk[0] == 0.0 &&
        m_crosshairIjk[1] == 0.0 && m_crosshairIjk[2] == 0.0) {
        const auto ext = m_volume->extent();
        m_crosshairIjk = {ext[0] / 2.0, ext[1] / 2.0, ext[2] / 2.0};
    }
    updateCrosshairActors();
    m_crossLineH->SetVisibility(on ? 1 : 0);
    m_crossLineV->SetVisibility(on ? 1 : 0);
    m_renderWindow->Render();
}

void SliceViewer::clearAnnotations()
{
    for (auto& an : m_annos)
        removeAnnoActors(an);
    m_annos.clear();
    m_draftAnno = m_editAnno = -1;
    m_renderWindow->Render();
    emit annotationsChanged();
}

void SliceViewer::undoAnnotation()
{
    if (m_annos.empty())
        return;
    removeAnnoActors(m_annos.back());
    m_annos.pop_back();
    m_draftAnno = m_editAnno = -1;
    m_renderWindow->Render();
    emit annotationsChanged();   // persist the removal
}

void SliceViewer::updateAnnotationVisibility()
{
    // Annotations live on the slice where they were drawn — hide them on
    // other slices instead of letting the label float over new anatomy.
    const bool measTool = (m_style->GetTool() == Tool::Measure ||
                           m_style->GetTool() == Tool::Roi ||
                           m_style->GetTool() == Tool::Angle);
    for (auto& an : m_annos) {
        const bool here = (an.slice == m_slice);
        an.line->SetVisibility(here ? 1 : 0);
        an.handles->SetVisibility(here && measTool ? 1 : 0);
        an.handlesInner->SetVisibility(here && measTool ? 1 : 0);
        an.text->SetVisibility(here ? 1 : 0);
    }
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
    m_hud[2]->SetInput(text.toUtf8().constData());
    m_renderWindow->Render();
}

void SliceViewer::setStudyText(const QString& text)
{
    m_studyText = text;
    updateCornerText();
    m_renderWindow->Render();
}

void SliceViewer::setSeriesText(const QString& text)
{
    m_hud[1]->SetInput(text.toUtf8().constData());
    m_renderWindow->Render();
}

void SliceViewer::setFlipBadge(bool on)
{
    m_flipBadge->SetVisibility(on);
    m_renderWindow->Render();
}

void SliceViewer::setFusionBadge(const QString& name)
{
    m_fusionBadge->SetInput(
        name.isEmpty() ? "" : ("FUSION: " + name).toUtf8().constData());
    m_fusionBadge->SetVisibility(name.isEmpty() ? 0 : 1);
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
    std::string tr = m_studyText.toUtf8().constData();
    if (!tr.empty())
        tr += "\n";
    tr += buf;
    m_hud[3]->SetInput(tr.c_str());
    if (m_slabType != 0 && m_slabMm > 0) {
        const char* names[] = {"", "AVG", "MIP", "MinIP"};
        std::snprintf(buf, sizeof(buf), "WW %.0f  WL %.0f   %s %.0fmm",
                      w, c, names[m_slabType], m_slabMm);
    } else {
        std::snprintf(buf, sizeof(buf), "WW %.0f  WL %.0f", w, c);
    }
    m_hud[0]->SetInput(buf);
}

// ------------------------------------------------------------------
// Annotation model: a list of shapes per slice, each drawn as
// a polyline + square handle glyphs + a world-space label placed to
// the right of the shape bounds, vertically centred (
// AbstractGraphic.setLabel convention).
// ------------------------------------------------------------------

/// Allocate the three actors an annotation needs (line, handle glyphs,
/// label) and attach them to the renderer.
void SliceViewer::addAnnoActors(Anno& an, double r, double g, double b)
{
    auto pts = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    auto pd = vtkSmartPointer<vtkPolyData>::New();
    pd->SetPoints(pts);
    pd->SetLines(cells);
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(pd);
    an.line = vtkSmartPointer<vtkActor>::New();
    an.line->SetMapper(mapper);
    an.line->GetProperty()->SetColor(r, g, b);
    an.line->GetProperty()->SetLineWidth(2.0f);
    m_overlay->AddActor(an.line);

    auto hpts = vtkSmartPointer<vtkPoints>::New();
    auto hcells = vtkSmartPointer<vtkCellArray>::New();
    auto hpd = vtkSmartPointer<vtkPolyData>::New();
    hpd->SetPoints(hpts);
    hpd->SetVerts(hcells);
    auto hm = vtkSmartPointer<vtkPolyDataMapper>::New();
    hm->SetInputData(hpd);
    an.handles = vtkSmartPointer<vtkActor>::New();
    an.handles->SetMapper(hm);
    an.handles->GetProperty()->SetColor(r, g, b);
    an.handles->GetProperty()->SetPointSize(9);   // square GL points
    m_overlay->AddActor(an.handles);
    // Bright inner square → bordered handle.
    an.handlesInner = vtkSmartPointer<vtkActor>::New();
    an.handlesInner->SetMapper(hm);   // shares the handle polydata
    an.handlesInner->GetProperty()->SetColor(0.95, 0.95, 0.95);
    an.handlesInner->GetProperty()->SetPointSize(4);
    m_overlay->AddActor(an.handlesInner);

    an.text = vtkSmartPointer<vtkBillboardTextActor3D>::New();
    auto* tp = an.text->GetTextProperty();
    tp->SetColor(r, g, b);
    tp->SetFontSize(13);
    tp->BoldOn();
    tp->ShadowOn();
    // Dark translucent chip behind the text — readable on bright tissue.
    tp->SetBackgroundColor(0.05, 0.05, 0.05);
    tp->SetBackgroundOpacity(0.55);
    tp->SetFrame(true);
    tp->SetFrameColor(r, g, b);
    tp->SetFrameWidth(1);
    m_overlay->AddActor(an.text);
}

void SliceViewer::removeAnnoActors(Anno& an)
{
    m_overlay->RemoveActor(an.line);
    m_overlay->RemoveActor(an.handles);
    m_overlay->RemoveActor(an.handlesInner);
    m_overlay->RemoveActor(an.text);
}

void SliceViewer::setAnnoVisible(Anno& an, bool on)
{
    an.line->SetVisibility(on ? 1 : 0);
    an.handles->SetVisibility(on ? 1 : 0);
    an.handlesInner->SetVisibility(on ? 1 : 0);
    an.text->SetVisibility(on ? 1 : 0);
}

void SliceViewer::beginAnno(int kind)
{
    Anno an;
    an.kind  = kind;
    an.slice = m_slice;
    // Distance/angle amber, ROI green.
    if (kind == 1)
        addAnnoActors(an, 0.18, 0.80, 0.44);
    else
        addAnnoActors(an, 1.0, 0.85, 0.1);
    m_annos.push_back(an);
    m_draftAnno = int(m_annos.size()) - 1;
}

void SliceViewer::updateMeasureActors(const std::array<double,3>& a,
                                      const std::array<double,3>& b)
{
    if (m_draftAnno < 0 || m_draftAnno >= int(m_annos.size()))
        return;
    auto& an = m_annos[size_t(m_draftAnno)];
    an.p[0] = a;
    an.p[1] = b;
    an.npts = 2;
    rebuildAnno(an);
    m_renderWindow->Render();
    emit annotationsChanged();

    const double dx = a[0]-b[0], dy = a[1]-b[1], dz = a[2]-b[2];
    emit measured(std::sqrt(dx*dx + dy*dy + dz*dz));
}

void SliceViewer::updateRoiActors(const std::array<double,3>& a,
                                  const std::array<double,3>& b)
{
    if (m_draftAnno < 0 || m_draftAnno >= int(m_annos.size()))
        return;
    auto& an = m_annos[size_t(m_draftAnno)];
    an.p[0] = a;
    an.p[1] = b;
    an.npts = 2;
    rebuildAnno(an);
    m_renderWindow->Render();
    emit annotationsChanged();
}

void SliceViewer::updateAngleActors(const std::array<double,3>& a,
                                    const std::array<double,3>& b,
                                    const std::array<double,3>& c)
{
    if (m_draftAnno < 0 || m_draftAnno >= int(m_annos.size()))
        return;
    auto& an = m_annos[size_t(m_draftAnno)];
    an.p[0] = a;
    an.p[1] = b;
    an.p[2] = c;
    // Distinct-point count → how many clicks have landed so far.
    auto eq = [](const std::array<double,3>& p,
                 const std::array<double,3>& q) {
        const double dx = p[0]-q[0], dy = p[1]-q[1], dz = p[2]-q[2];
        return std::sqrt(dx*dx + dy*dy + dz*dz) < 1e-6;
    };
    an.npts = eq(a, b) && eq(b, c) ? 1 : (eq(b, c) ? 2 : 3);
    rebuildAnno(an);
    m_renderWindow->Render();
    emit annotationsChanged();
}

void SliceViewer::rebuildAnno(Anno& an)
{
    const int    axis  = axisOf(m_orientation);
    const double plane = slicePlaneOffset();
    const int    u = (axis + 1) % 3, v = (axis + 2) % 3;
    // Annotations render on a separate overlay layer (layer 1), so no
    // depth-vs-image tricks are needed — they always draw on top.

    auto* pd  = vtkPolyData::SafeDownCast(an.line->GetMapper()->GetInput());
    auto* hpd = vtkPolyData::SafeDownCast(
        an.handles->GetMapper()->GetInput());
    vtkPoints*   pts    = pd->GetPoints();
    vtkCellArray* cells = pd->GetLines();
    vtkPoints*   hpts   = hpd->GetPoints();
    vtkCellArray* hcells = hpd->GetVerts();
    pts->Reset();   cells->Reset();
    hpts->Reset();  hcells->Reset();

    auto onPlane = [&](std::array<double,3> p) {
        p[axis] = plane;
        return p;
    };
    auto polyline = [&](const std::vector<std::array<double,3>>& pl) {
        cells->InsertNextCell(vtkIdType(pl.size()));
        for (const auto& p : pl)
            cells->InsertCellPoint(pts->InsertNextPoint(p.data()));
    };
    auto handle = [&](const std::array<double,3>& p) {
        const vtkIdType id = hpts->InsertNextPoint(p.data());
        hcells->InsertNextCell(1, &id);
    };

    // Label spot: right of the shape bounds, vertically centred.
    double lp[3] = {0, 0, plane};
    double shapeMinU = 0.0;   // left edge — fallback label side
    std::string label;

    if (an.kind == 0) {                      // distance line
        const auto pa = onPlane(an.p[0]), pb = onPlane(an.p[1]);
        polyline({pa, pb});
        handle(pa);
        handle(pb);
        lp[u] = std::max(pa[u], pb[u]) + 4.0;
        lp[v] = (pa[v] + pb[v]) / 2.0;
        shapeMinU = std::min(pa[u], pb[u]);
        const double dx = an.p[0][0]-an.p[1][0],
                     dy = an.p[0][1]-an.p[1][1],
                     dz = an.p[0][2]-an.p[1][2];
        const double mm = std::sqrt(dx*dx + dy*dy + dz*dz);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f %s", mm,
                      (!m_volume || m_volume->spacingCalibrated())
                          ? "mm" : "px");
        label = buf;
    } else if (an.kind == 1) {               // ROI rectangle
        std::array<double,3> lo, hi;
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(an.p[0][i], an.p[1][i]);
            hi[i] = std::max(an.p[0][i], an.p[1][i]);
        }
        lo[axis] = hi[axis] = plane;
        // Corners in the in-plane u,v axes — the previous index-fixed
        // construction collapsed to a line on sagittal views.
        std::array<double,3> c00 = lo, c10 = lo, c11 = hi, c01 = lo;
        c10[u] = hi[u];
        c01[v] = hi[v];
        polyline({c00, c10, c11, c01, c00});
        handle(c00); handle(c10); handle(c11); handle(c01);
        lp[u] = hi[u] + 4.0;
        lp[v] = (lo[v] + hi[v]) / 2.0;
        shapeMinU = lo[u];

        // Voxel stats inside the rect on this slice.
        if (m_volume) {
            const auto sp  = m_volume->spacing();
            const auto ext = m_volume->extent();
            std::array<double,3> ai, bi;
            m_volume->worldToIjk(an.p[0], ai);
            m_volume->worldToIjk(an.p[1], bi);
            const int iMin = std::clamp(
                int(std::floor(std::min(ai[u], bi[u]))), 0, ext[u] - 1);
            const int iMax = std::clamp(
                int(std::ceil(std::max(ai[u], bi[u]))), 0, ext[u] - 1);
            const int jMin = std::clamp(
                int(std::floor(std::min(ai[v], bi[v]))), 0, ext[v] - 1);
            const int jMax = std::clamp(
                int(std::ceil(std::max(ai[v], bi[v]))), 0, ext[v] - 1);
            const int k = std::clamp(
                int(std::lround(plane / sp[axis])), 0, ext[axis] - 1);
            double sum = 0, sum2 = 0, vmin = 1e30, vmax = -1e30;
            long   n = 0;
            const auto& meta = m_volume->meta();
            auto isPad = [&](double s) {
                return meta.hasPixelPadding && s >= meta.padLo &&
                       s <= meta.padHi;
            };
            int idx[3];
            idx[axis] = k;
            for (int j = jMin; j <= jMax; ++j) {
                idx[v] = j;
                for (int i = iMin; i <= iMax; ++i) {
                    idx[u] = i;
                    const double s = m_volume->vtkImage()
                        ->GetScalarComponentAsDouble(idx[0], idx[1],
                                                     idx[2], 0);
                    if (isPad(s))
                        continue;
                    sum += s;  sum2 += s * s;
                    vmin = std::min(vmin, s);
                    vmax = std::max(vmax, s);
                    ++n;
                }
            }
            if (n >= 2) {
                const double mean = sum / n;
                const double sd = std::sqrt(
                    std::max(0.0, sum2 / n - mean * mean));
                const double area = n * sp[u] * sp[v];
                char buf[192];
                if (meta.suvFactor > 0.0) {
                    std::snprintf(buf, sizeof(buf),
                        "ROI: SUV %.2f +/- %.2f\n"
                        "[max %.2f]  %ld px  %.1f mm2",
                        mean * meta.suvFactor, sd * meta.suvFactor,
                        vmax * meta.suvFactor, n, area);
                } else {
                    const char* unit =
                        (meta.modality == "CT") ? " HU" : "";
                    std::snprintf(buf, sizeof(buf),
                        "ROI: %.0f +/- %.0f%s\n"
                        "[%.0f .. %.0f]  %ld px  %.1f mm2",
                        mean, sd, unit, vmin, vmax, n, area);
                }
                label = buf;
            }
        }
    } else {                                 // angle  A — B — C
        const auto pa = onPlane(an.p[0]), pb = onPlane(an.p[1]),
                   pc = onPlane(an.p[2]);
        if (an.npts == 1) {
            auto stub = pa;
            stub[u] += 2.0;
            polyline({pa, stub});
            handle(pa);
        } else if (an.npts == 2) {
            polyline({pa, pb});
            handle(pa);
            handle(pb);
        } else {
            polyline({pa, pb, pc});
            handle(pa); handle(pb); handle(pc);
            double v1[3], v2[3];
            for (int i = 0; i < 3; ++i) {
                v1[i] = an.p[0][i] - an.p[1][i];
                v2[i] = an.p[2][i] - an.p[1][i];
            }
            const double n1 = std::sqrt(v1[0]*v1[0] + v1[1]*v1[1] +
                                        v1[2]*v1[2]);
            const double n2 = std::sqrt(v2[0]*v2[0] + v2[1]*v2[1] +
                                        v2[2]*v2[2]);
            if (n1 > 1e-6 && n2 > 1e-6) {
                const double dot =
                    (v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2]) /
                    (n1 * n2);
                const double deg =
                    std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 /
                    M_PI;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.1f\xc2\xb0", deg);
                label = buf;
            }
            lp[u] = std::max({pa[u], pb[u], pc[u]}) + 4.0;
            lp[v] = pb[v];
            shapeMinU = std::min({pa[u], pb[u], pc[u]});
        }
    }

    pts->Modified();
    cells->Modified();
    hpts->Modified();
    hcells->Modified();
    pd->Modified();
    hpd->Modified();

    // Keep the label inside the image bounds: if the right-of-shape
    // spot (anchor + text width) would run off the edge, flip it to
    // the shape's left.
    if (m_volume && !label.empty()) {
        const double uMax =
            m_volume->extent()[u] * m_volume->spacing()[u];
        size_t longest = 0, cur = 0;
        for (const char c : label) {
            if (c == '\n') { longest = std::max(longest, cur); cur = 0; }
            else ++cur;
        }
        longest = std::max(longest, cur);
        const double w = 2.6 * double(longest) + 6.0;
        if (lp[u] + w > uMax - 2.0)
            lp[u] = std::max(2.0, shapeMinU - 4.0 - w);
    }
    an.text->SetInput(label.c_str());
    an.text->SetPosition(lp);
    const bool vis = (an.slice == m_slice);
    setAnnoVisible(an, vis);
    const bool measTool = (m_style->GetTool() == Tool::Measure ||
                           m_style->GetTool() == Tool::Roi ||
                           m_style->GetTool() == Tool::Angle);
    an.handles->SetVisibility(vis && measTool ? 1 : 0);
    an.handlesInner->SetVisibility(vis && measTool ? 1 : 0);
    an.text->SetVisibility(vis && !label.empty() ? 1 : 0);

    // Selection feedback: the dragged shape draws heavier.
    const bool sel = (m_editAnno >= 0 &&
                      &an == &m_annos[size_t(m_editAnno)]);
    an.line->GetProperty()->SetLineWidth(sel ? 3.2f : 2.0f);
    an.handles->GetProperty()->SetPointSize(sel ? 11 : 9);
    an.handlesInner->GetProperty()->SetPointSize(sel ? 6 : 4);
}

std::pair<int,int> SliceViewer::pickAnnoHandle(
    const std::array<double,3>& worldPt) const
{
    if (!m_overlay)
        return {-1, -1};
    const int axis = axisOf(m_orientation);
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;

    // Clicked point → display px.
    m_overlay->SetWorldPoint(worldPt[0], worldPt[1], worldPt[2], 1.0);
    m_overlay->WorldToDisplay();
    double dc[3];
    m_overlay->GetDisplayPoint(dc);

    double best = 10.0;   // px pick radius
    int bi = -1, bp = -1;
    for (int i = 0; i < int(m_annos.size()); ++i) {
        const Anno& an = m_annos[size_t(i)];
        if (an.slice != m_slice || an.npts == 0)
            continue;
        // Handle positions in world space.
        std::vector<std::array<double,3>> hs;
        if (an.kind == 1) {              // ROI: 4 corners
            std::array<double,3> lo, hi;
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(an.p[0][c], an.p[1][c]);
                hi[c] = std::max(an.p[0][c], an.p[1][c]);
            }
            std::array<double,3> c10 = lo, c01 = lo;
            c10[u] = hi[u];
            c01[v] = hi[v];
            hs = {lo, c10, hi, c01};
        } else {
            for (int p = 0; p < an.npts; ++p)
                hs.push_back(an.p[size_t(p)]);
        }
        for (int p = 0; p < int(hs.size()); ++p) {
            m_overlay->SetWorldPoint(hs[size_t(p)][0],
                                      hs[size_t(p)][1],
                                      hs[size_t(p)][2], 1.0);
            m_overlay->WorldToDisplay();
            double hp[3];
            m_overlay->GetDisplayPoint(hp);
            const double d = std::hypot(hp[0] - dc[0], hp[1] - dc[1]);
            if (d < best) {
                best = d;
                bi = i;
                bp = p;
            }
        }
    }
    return {bi, bp};
}

void SliceViewer::editAnnoPoint(int annoIdx, int ptIdx,
                                const std::array<double,3>& worldPt)
{
    if (annoIdx < 0 || annoIdx >= int(m_annos.size()))
        return;
    auto& an = m_annos[size_t(annoIdx)];
    const int axis = axisOf(m_orientation);
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;

    if (an.kind == 1) {
        // Corner handles: 0=(au,av) 1=(bu,av) 2=(bu,bv) 3=(au,bv)
        auto& a = an.p[0];
        auto& b = an.p[1];
        switch (ptIdx) {
        case 0: a[u] = worldPt[u]; a[v] = worldPt[v]; break;
        case 1: b[u] = worldPt[u]; a[v] = worldPt[v]; break;
        case 2: b[u] = worldPt[u]; b[v] = worldPt[v]; break;
        case 3: a[u] = worldPt[u]; b[v] = worldPt[v]; break;
        default: break;
        }
    } else if (ptIdx >= 0 && ptIdx < 3) {
        auto p = worldPt;
        p[axis] = an.p[size_t(ptIdx)][axis];
        an.p[size_t(ptIdx)] = p;
    }
    rebuildAnno(an);
    m_renderWindow->Render();
    emit annotationsChanged();
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

    // Piecewise-linear ramp through RGB anchor points.
    auto ramp = [this](std::initializer_list<std::array<double,3>> pts) {
        const int n = int(pts.size());
        for (int i = 0; i < 256; ++i) {
            const double t = i / 255.0 * (n - 1);
            const int k = std::min(int(t), n - 2);
            const double f = t - k;
            const auto& a = *(pts.begin() + k);
            const auto& b = *(pts.begin() + k + 1);
            m_colorLut->SetTableValue(i,
                a[0] + (b[0] - a[0]) * f,
                a[1] + (b[1] - a[1]) * f,
                a[2] + (b[2] - a[2]) * f, 1.0);
        }
    };

    switch (which) {
    case 1: // inverted grayscale
        m_colorLut->SetHueRange(0, 0);
        m_colorLut->SetSaturationRange(0, 0);
        m_colorLut->SetValueRange(1, 0);
        break;
    case 2: // hot iron (black -> red -> orange -> yellow -> white)
        ramp({{0,0,0}, {1,0,0}, {1,1,0}, {1,1,1}});
        break;
    case 3: // PET-style rainbow
        m_colorLut->SetHueRange(0.6667, 0.0);
        m_colorLut->SetSaturationRange(1, 1);
        m_colorLut->SetValueRange(1, 1);
        break;
    case 4: // bone-ish sepia
        ramp({{0,0,0}, {0.38,0.34,0.30}, {0.79,0.74,0.68}, {1,1,0.96}});
        break;
    case 5: // Jet (MATLAB): dark blue -> cyan -> yellow -> dark red
        ramp({{0,0,0.5}, {0,0,1}, {0,1,1}, {1,1,0}, {1,0,0}, {0.5,0,0}});
        break;
    case 6: // Cool (MATLAB): cyan -> magenta
        ramp({{0,1,1}, {1,0,1}});
        break;
    case 7: // Copper: black -> copper
        ramp({{0,0,0}, {0.8,0.50,0.20}, {1,0.78,0.50}});
        break;
    case 8: // Viridis (perceptually uniform): purple -> teal -> yellow
        ramp({{0.267,0.005,0.329}, {0.283,0.141,0.458},
              {0.231,0.322,0.545}, {0.127,0.566,0.550},
              {0.369,0.789,0.382}, {0.993,0.906,0.144}});
        break;
    case 9: // Hot-metal blue (Siemens/GE PET): black -> blue -> red
            // -> orange -> yellow -> white
        ramp({{0,0,0}, {0,0,0.55}, {0.55,0,1}, {1,0,0.55},
              {1,0.5,0.15}, {1,0.85,0.35}, {1,1,0.65}, {1,1,1}});
        break;
    case 10: // PET 20-step — discrete blue->red bands (GE consoles)
        for (int i = 0; i < 256; ++i) {
            const int band = std::min(int(i / 255.0 * 20), 19);
            const double h = (1.0 - band / 19.0) * 0.6667;
            const QColor c = QColor::fromHsvF(h, 1.0, 1.0);
            m_colorLut->SetTableValue(i, c.redF(), c.greenF(),
                                      c.blueF(), 1.0);
        }
        break;
    case 11: // Autumn (MATLAB): red -> yellow
        ramp({{1,0,0}, {1,1,0}});
        break;
    case 12: // Winter (MATLAB): blue -> green
        ramp({{0,0,1}, {0,1,0.5}});
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
    QJsonArray  list;
    for (const auto& an : m_annos) {
        QJsonObject j;
        j["kind"]  = an.kind;
        j["slice"] = an.slice;
        j["n"]     = an.npts;
        j["a"] = ptToJson(an.p[0]);
        j["b"] = ptToJson(an.p[1]);
        j["c"] = ptToJson(an.p[2]);
        list.append(j);
    }
    o["annos"] = list;
    return o;
}

void SliceViewer::annotationsFromJson(const QJsonObject& o)
{
    if (!m_volume || o.isEmpty())
        return;
    auto read = [](const QJsonObject& a, const char* key,
                   std::array<double,3>* p) {
        const auto j = a.value(key).toArray();
        if (!j.isEmpty())
            *p = ptFromJson(j);
    };
    auto addAnno = [&](int kind, int slice,
                       const std::array<double,3>& a,
                       const std::array<double,3>& b,
                       const std::array<double,3>& c, int npts) {
        beginAnno(kind);
        auto& an = m_annos.back();
        an.slice = slice;
        an.p[0] = a;
        an.p[1] = b;
        an.p[2] = c;
        an.npts = npts;
        m_draftAnno = -1;        // restored shapes are final, not drafts
        rebuildAnno(an);
    };

    // Current format: a list of shapes.
    for (const auto& v : o.value("annos").toArray()) {
        const auto j = v.toObject();
        std::array<double,3> a{0,0,0}, b{0,0,0}, c{0,0,0};
        read(j, "a", &a);
        read(j, "b", &b);
        read(j, "c", &c);
        addAnno(j.value("kind").toInt(), j.value("slice").toInt(),
                a, b, c, j.value("n").toInt(2));
    }

    // Legacy format: one shape per key.
    const QJsonObject m = o.value("measure").toObject();
    if (!m.isEmpty()) {
        std::array<double,3> a{0,0,0}, b{0,0,0};
        read(m, "a", &a);
        read(m, "b", &b);
        addAnno(0, m.value("slice").toInt(), a, b, b, 2);
    }
    const QJsonObject r = o.value("roi").toObject();
    if (!r.isEmpty()) {
        std::array<double,3> a{0,0,0}, b{0,0,0};
        read(r, "a", &a);
        read(r, "b", &b);
        addAnno(1, r.value("slice").toInt(), a, b, b, 2);
    }
    const QJsonObject g = o.value("angle").toObject();
    if (!g.isEmpty()) {
        std::array<double,3> a{0,0,0}, b{0,0,0}, c{0,0,0};
        read(g, "a", &a);
        read(g, "b", &b);
        read(g, "c", &c);
        addAnno(2, g.value("slice").toInt(), a, b, c, 3);
    }
    updateAnnotationVisibility();
    m_renderWindow->Render();
}

} // namespace meda
