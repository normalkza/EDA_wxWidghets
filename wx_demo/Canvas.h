#pragma once


#include <wx/wx.h>
#include <wx/dcbuffer.h>
class Canvas : public wxPanel
{
public:
    Canvas(wxWindow* parent);

private:
    void OnPaint(wxPaintEvent& event);

    // 网格参数
    int m_gridSize = 20;       // 每个格子的像素大小
    wxColour m_gridColour = wxColour(220, 220, 220);  // 网格线颜色

    wxDECLARE_EVENT_TABLE();
};
