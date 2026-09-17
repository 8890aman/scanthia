#include "SliceInteractionStyle.h"

#include <vtkCallbackCommand.h>
#include <vtkObjectFactory.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

vtkStandardNewMacro(meda::SliceInteractionStyle);

namespace meda {

void SliceInteractionStyle::OnMouseMove()
{
    const int* pos = this->Interactor->GetEventPosition();
    if (m_editing && pickPoint && onMeasureEdit) {
        onMeasureEdit(pickPoint(pos[0], pos[1]));
        vtkInteractorStyleImage::OnMouseMove();
        return;
    }
    if (m_measuring && pickPoint) {
        auto p = pickPoint(pos[0], pos[1]);
        if (m_tool == Tool::Roi) {
            if (onRoi) onRoi(m_measureStart, p);
        } else if (onMeasured) {
            onMeasured(m_measureStart, p);
        }
    }
    // Live angle preview: show segments as the cursor moves.
    if (m_tool == Tool::Angle && pickPoint && !m_anglePts.empty()) {
        auto p = pickPoint(pos[0], pos[1]);
        if (m_anglePts.size() == 1)
            onAngle(m_anglePts[0], p, p);     // A → cursor
        else if (m_anglePts.size() == 2)
            onAngle(m_anglePts[0], m_anglePts[1], p); // B→A + B→cursor
    }
    if (!m_measuring && pickPoint && onHovered)
        onHovered(pickPoint(pos[0], pos[1]));
    if (m_painting && pickPoint && onPaint)
        onPaint(pickPoint(pos[0], pos[1]), m_tool == Tool::Eraser);
    // Custom W/L drag — dx right widens the window (less contrast),
    // dy up raises the level (brighter). Handled here, not by VTK's
    // picker-dependent WindowLevel().
    if (m_windowLevelling && onWindowLevelDelta) {
        onWindowLevelDelta(pos[0] - m_wlStartPos[0],
                           pos[1] - m_wlStartPos[1]);
    }
    vtkInteractorStyleImage::OnMouseMove();
}

void SliceInteractionStyle::OnMouseWheelForward()
{
    if (onSliceScrolled)
        onSliceScrolled(1);
}

void SliceInteractionStyle::OnMouseWheelBackward()
{
    if (onSliceScrolled)
        onSliceScrolled(-1);
}

void SliceInteractionStyle::OnLeftButtonDown()
{
    if (onActivated)
        onActivated();

    const int* pos = this->Interactor->GetEventPosition();

    if (m_tool == Tool::WindowLevel) {
        // Our own W/L drag: record the press point and let the viewer
        // capture its start WW/WL; deltas arrive via onWindowLevelDelta.
        this->FindPokedRenderer(pos[0], pos[1]);
        if (!this->CurrentRenderer)
            return;
        m_wlStartPos[0] = pos[0];
        m_wlStartPos[1] = pos[1];
        m_windowLevelling = true;
        if (onWindowLevelStarted)
            onWindowLevelStarted();
        this->GrabFocus(this->EventCallbackCommand);
        return;
    }

    this->FindPokedRenderer(this->Interactor->GetEventPosition()[0],
                            this->Interactor->GetEventPosition()[1]);
    if (!this->CurrentRenderer)
        return;
    this->GrabFocus(this->EventCallbackCommand);

    switch (m_tool) {
    case Tool::Pan:
        this->StartPan();
        if (onInteractionBegin) onInteractionBegin();
        break;
    case Tool::Zoom:
        this->StartDolly();
        if (onInteractionBegin) onInteractionBegin();
        break;
    case Tool::Crosshair:
        if (pickPoint && onPointPicked)
            onPointPicked(pickPoint(pos[0], pos[1]));
        break;
    case Tool::Measure:
        if (pickPoint) {
            const auto p = pickPoint(pos[0], pos[1]);
            // Grab an existing handle → edit it, don't start a new line.
            if (onMeasureGrab && onMeasureGrab(p)) {
                m_editing = true;
                break;
            }
            m_measureStart = p;
            m_measuring = true;
            if (onMeasureStart)
                onMeasureStart(0);
            if (onMeasured)
                onMeasured(m_measureStart, m_measureStart);
        }
        break;
    case Tool::Roi:
        if (pickPoint) {
            const auto p = pickPoint(pos[0], pos[1]);
            if (onMeasureGrab && onMeasureGrab(p)) {
                m_editing = true;
                break;
            }
            m_measureStart = p;
            m_measuring = true;
            if (onMeasureStart)
                onMeasureStart(1);
            if (onRoi)
                onRoi(m_measureStart, m_measureStart);
        }
        break;
    case Tool::Angle:
        if (pickPoint && onAngle) {
            auto p = pickPoint(pos[0], pos[1]);
            if (onMeasureGrab && onMeasureGrab(p)) {
                m_editing = true;
                break;
            }
            if (m_anglePts.empty() && onMeasureStart)
                onMeasureStart(2);
            m_anglePts.push_back(p);
            if (m_anglePts.size() == 1) {
                onAngle(p, p, p); // point A only — show marker
            } else if (m_anglePts.size() == 2) {
                onAngle(m_anglePts[0], p, p); // A + vertex, C = preview
            } else if (m_anglePts.size() == 3) {
                onAngle(m_anglePts[0], m_anglePts[1], p);
                m_anglePts.clear();
            }
        }
        break;
    case Tool::Brush:
    case Tool::Eraser:
        if (pickPoint && onPaint) {
            m_painting = true;
            if (onStroke)
                onStroke(true);
            onPaint(pickPoint(pos[0], pos[1]), m_tool == Tool::Eraser);
        }
        break;
    default:
        break;
    }
}

void SliceInteractionStyle::OnLeftButtonUp()
{
    m_editing = false;
    if (m_tool == Tool::WindowLevel) {
        if (m_windowLevelling) {
            m_windowLevelling = false;
            if (onWindowLevelEnded)
                onWindowLevelEnded();
        }
        this->ReleaseFocus();
        return;
    }

    switch (m_tool) {
    case Tool::Pan:
        this->EndPan();
        if (onInteractionEnd) onInteractionEnd();
        break;
    case Tool::Zoom:
        this->EndDolly();
        if (onInteractionEnd) onInteractionEnd();
        break;
    case Tool::Measure:
        if (m_measuring) {
            m_measuring = false;
            const int* pos = this->Interactor->GetEventPosition();
            if (pickPoint && onMeasured)
                onMeasured(m_measureStart, pickPoint(pos[0], pos[1]));
        }
        break;
    case Tool::Roi:
        if (m_measuring) {
            m_measuring = false;
            const int* pos = this->Interactor->GetEventPosition();
            if (pickPoint && onRoi)
                onRoi(m_measureStart, pickPoint(pos[0], pos[1]));
        }
        break;
    case Tool::Brush:
    case Tool::Eraser:
        if (m_painting && onStroke)
            onStroke(false);
        m_painting = false;
        break;
    default: break;
    }
    this->ReleaseFocus();
}

void SliceInteractionStyle::OnRightButtonDown()
{
    this->FindPokedRenderer(this->Interactor->GetEventPosition()[0],
                            this->Interactor->GetEventPosition()[1]);
    if (!this->CurrentRenderer)
        return;
    const int* pos = this->Interactor->GetEventPosition();
    m_rightPressPos[0] = pos[0];
    m_rightPressPos[1] = pos[1];
    this->GrabFocus(this->EventCallbackCommand);
    this->StartPan();
    if (onInteractionBegin) onInteractionBegin();
}

void SliceInteractionStyle::OnRightButtonUp()
{
    this->EndPan();
    this->ReleaseFocus();
    if (onInteractionEnd) onInteractionEnd();
    // A right click without a drag deselects the active tool.
    const int* pos = this->Interactor->GetEventPosition();
    const int dx = pos[0] - m_rightPressPos[0];
    const int dy = pos[1] - m_rightPressPos[1];
    if (dx * dx + dy * dy < 16 && onRightClick &&
        m_tool != Tool::WindowLevel)
        onRightClick();
}

} // namespace meda
