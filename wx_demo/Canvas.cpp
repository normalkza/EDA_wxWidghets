#include "Canvas.h"
#include <wx/dcbuffer.h> 

wxBEGIN_EVENT_TABLE(Canvas, wxPanel)
EVT_PAINT(Canvas::OnPaint)
wxEND_EVENT_TABLE()

Canvas::Canvas(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundColour(*wxWHITE);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

void Canvas::OnPaint(wxPaintEvent& event)
{
    wxBufferedPaintDC dc(this);

    // 设置背景为白色
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();

    wxSize size = GetClientSize();
    int w = size.GetWidth();
    int h = size.GetHeight();

    // 绘制点阵网格
    // 设置点的颜色（浅灰色，类似 Logisim）
    dc.SetPen(wxPen(wxColour(180, 180, 180), 1, wxPENSTYLE_SOLID));

    // 用双重循环遍历每一个网格点
    for (int x = 0; x <= w; x += m_gridSize)
    {
        for (int y = 0; y <= h; y += m_gridSize)
        {
            // 在坐标 (x, y) 处画一个点
            // DrawPoint 画的是一个 1x1 像素的点，如果想要大一点，可以改用 dc.DrawCircle
            // 替换掉 dc.DrawPoint(x, y);
            dc.DrawCircle(x, y, 1);
        }
    }
}
