#pragma once

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <vector>

#include "Component.h"
#include <functional>
#include <utility>


class Canvas : public wxPanel
{
public:
    Canvas(wxWindow* parent);
    void SetSelectionCallback(std::function<void(const Component*, int, int)> callback)
    {
        m_selectionCallback = std::move(callback);
    }


private:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void CancelMouseInteraction();

    // 把坐标对齐到网格点
    int SnapToGrid(int value) const { return (value / m_gridSize) * m_gridSize; }

    // 六种门电路的绘制函数
    void DrawAndGate(wxDC& dc, int x, int y);
    void DrawOrGate(wxDC& dc, int x, int y);
    void DrawNotGate(wxDC& dc, int x, int y);
    void DrawNandGate(wxDC& dc, int x, int y);
    void DrawNorGate(wxDC& dc, int x, int y);
    void DrawXorGate(wxDC& dc, int x, int y);
    void DrawInput(wxDC& dc, int x, int y, LogicValue value);
    void DrawOutput(wxDC& dc, int x, int y, LogicValue value);

    // 画布上的元件
    struct PlacedComponent
    {
        Component* comp;
        int x, y;
    };
    int HitTest(int x, int y) const;
    wxRect GetComponentRect(const PlacedComponent& pc) const;
    std::vector<PlacedComponent> m_components;
    int m_selectedIndex = -1;
    int m_pressedIndex = -1;
    bool m_dragging = false;
    bool m_togglePending = false;
    wxPoint m_pressPosition;
    static constexpr int m_dragThreshold = 4;
    wxPoint m_dragOffset;
    std::function<void(const Component*, int, int)> m_selectionCallback;


    // 网格
    int m_gridSize = 20;
    wxColour m_gridColour = wxColour(220, 220, 220);

    wxDECLARE_EVENT_TABLE();
};
