#pragma once

#include "Types.h"

#include <vtkInteractorStyleImage.h>
#include <vtkSmartPointer.h>

#include <array>
#include <functional>
#include <vector>

namespace meda {

/// Interactor style for 2D slice viewing. The left button performs the
/// action of the active tool; middle and right always pan, and the wheel
/// always scrolls slices.
class SliceInteractionStyle : public vtkInteractorStyleImage {
public:
    static SliceInteractionStyle* New();
    vtkTypeMacro(SliceInteractionStyle, vtkInteractorStyleImage);

    void SetTool(Tool t) { m_tool = t; }
    Tool GetTool() const { return m_tool; }

    /// Callbacks are set by the owning SliceViewer. World points are in
    /// display space (index * spacing).
    std::function<void(int)>                          onSliceScrolled;
    std::function<void()>                             onWindowLevelEnded;
    /// Custom W/L drag (we don't rely on vtkInteractorStyleImage's
    /// picker-based W/L — it fails when the image prop isn't pickable).
    /// Start: viewer captures current WW/WL. Delta: dx,dy in pixels.
    std::function<void()>                             onWindowLevelStarted;
    std::function<void(int, int)>                     onWindowLevelDelta;
    std::function<void(std::array<double,3>)>         onPointPicked;
    std::function<void(std::array<double,3>, std::array<double,3>)> onMeasured;
    std::function<void(std::array<double,3>, std::array<double,3>)> onRoi;
    std::function<void(std::array<double,3>, std::array<double,3>,
                       std::array<double,3>)>                       onAngle;
    std::function<void()>                             onActivated;
    std::function<void(std::array<double,3>)>         onHovered; // world pt
    /// Paint/erase at world point — brush & eraser tools.
    std::function<void(std::array<double,3>, bool)>   onPaint;
    std::function<void(bool)>                         onStroke;  // begin/end
    /// Right-click (no drag) while a tool is active → deselect it.
    std::function<void()>                             onRightClick;
    /// Called when a pan/zoom interaction begins/ends (for LOD).
    std::function<void()>                             onInteractionBegin;
    std::function<void()>                             onInteractionEnd;

    /// Supplied by the viewer so the style can convert display->world
    /// at the current slice plane.
    std::function<std::array<double,3>(int,int)>      pickPoint;

    void OnMouseMove() override;
    void OnMouseWheelForward() override;
    void OnMouseWheelBackward() override;
    void OnLeftButtonDown() override;
    void OnLeftButtonUp() override;
    void OnRightButtonDown() override;
    void OnRightButtonUp() override;

private:
    Tool m_tool = Tool::WindowLevel;
    bool m_measuring = false;
    bool m_painting = false;
    bool m_windowLevelling = false;
    int  m_wlStartPos[2] = {0, 0};
    int  m_rightPressPos[2] = {0, 0};
    std::array<double,3> m_measureStart{};
    std::vector<std::array<double,3>> m_anglePts;
};

} // namespace meda
