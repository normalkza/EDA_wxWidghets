#pragma once

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <vector>

#include "Component.h"


class Canvas : public wxPanel
{
public:
    Canvas(wxWindow* parent);


private:
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);

    // 把坐标对齐到网格点
    int SnapToGrid(int value) const { return (value / m_gridSize) * m_gridSize; }

    // 六种门电路的绘制函数
    void DrawAndGate(wxDC& dc, int x, int y);
    void DrawOrGate(wxDC& dc, int x, int y);
    void DrawNotGate(wxDC& dc, int x, int y);
    void DrawNandGate(wxDC& dc, int x, int y);
    void DrawNorGate(wxDC& dc, int x, int y);
    void DrawXorGate(wxDC& dc, int x, int y);

    // 画布上的元件
    struct PlacedComponent
    {
        Component* comp;
        int x, y;
    };
    std::vector<PlacedComponent> m_components;


    // 网格
    int m_gridSize = 20;
    wxColour m_gridColour = wxColour(220, 220, 220);

    wxDECLARE_EVENT_TABLE();
};
