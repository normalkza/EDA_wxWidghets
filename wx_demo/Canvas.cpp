#include "Canvas.h"
#include "SelectionState.h"

#include <wx/dcbuffer.h>
#include <cmath>

#include "Toolbox.h"
#include "AndGate.h"
#include "OrGate.h"
#include "NotGate.h"
#include "NandGate.h"
#include "NorGate.h"
#include "XorGate.h"


wxBEGIN_EVENT_TABLE(Canvas, wxPanel)
EVT_PAINT(Canvas::OnPaint)
wxEND_EVENT_TABLE()

Canvas::Canvas(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundColour(*wxWHITE);
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_LEFT_DOWN, &Canvas::OnLeftDown, this);
}

void Canvas::OnLeftDown(wxMouseEvent& event)
{
    wxString type = g_selectedType;
    if (type.IsEmpty()) return;

    int x = SnapToGrid(event.GetX());
    int y = SnapToGrid(event.GetY());

    Component* comp = nullptr;
    if (type == "AND")       comp = new AndGate();
    else if (type == "OR")   comp = new OrGate();
    else if (type == "NOT")  comp = new NotGate();
    else if (type == "NAND") comp = new NandGate();
    else if (type == "NOR")  comp = new NorGate();
    else if (type == "XOR")  comp = new XorGate();

    if (comp)
    {
        m_components.push_back({ comp, x, y });
        Refresh();
    }
}

void Canvas::OnPaint(wxPaintEvent& event)
{
    wxBufferedPaintDC dc(this);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();

    // 网格
    wxSize size = GetClientSize();
    dc.SetPen(wxPen(wxColour(200, 200, 200), 1, wxPENSTYLE_SOLID));
    for (int x = 0; x <= size.GetWidth(); x += m_gridSize)
        for (int y = 0; y <= size.GetHeight(); y += m_gridSize)
            dc.DrawCircle(x, y, 1);

    // 元件
    for (const auto& pc : m_components)
    {
        if (!pc.comp) continue;

        if (pc.comp->name == "AND")       DrawAndGate(dc, pc.x, pc.y);
        else if (pc.comp->name == "OR")   DrawOrGate(dc, pc.x, pc.y);
        else if (pc.comp->name == "NOT")  DrawNotGate(dc, pc.x, pc.y);
        else if (pc.comp->name == "NAND") DrawNandGate(dc, pc.x, pc.y);
        else if (pc.comp->name == "NOR")  DrawNorGate(dc, pc.x, pc.y);
        else if (pc.comp->name == "XOR")  DrawXorGate(dc, pc.x, pc.y);

    }
}


// 非门 NOT：三角形 + 输出端小圆
void Canvas::DrawNotGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int w = 36;
    int h = 36;

    dc.DrawLine(x, y, x, y + h);
    dc.DrawLine(x, y, x + w, y + h / 2);
    dc.DrawLine(x, y + h, x + w, y + h / 2);

    dc.DrawCircle(x + w + 5, y + h / 2, 4);
}

// 与门 AND：左边竖线 + 右半圆（放大版）
void Canvas::DrawAndGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int h = 60;   // 高度
    int r = h / 2;

    // 左边竖线
    dc.DrawLine(x, y, x, y + h);

    // 上下两条横线，从左边到右半圆起点
    dc.DrawLine(x, y, x + r, y);
    dc.DrawLine(x, y + h, x + r, y + h);

    // 右半圆：包围盒 (x, y)，宽高都等于 h，从 270° 到 90°
    dc.DrawEllipticArc(x, y, h, h, 270, 90);
}








//或门
void Canvas::DrawOrGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int w = 80;    // 总宽
    int h = 60;    // 总高
    int midY = y + h / 2;

    const int N = 60;

    wxPoint left[N + 1];
    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double py = y + t * h;
        // 使用 sin 让凸起集中在中间，最大约 8 像素
        double bulge = 8.0 * std::sin(t * 3.1415926);
        double px = (x + 6) + bulge;   // 注意这里是“加”，因为要向右凸
        left[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, left);

     wxPoint upper[N + 1];
    double x0 = x + 6, y0 = y;
    double x1 = x + w * 0.65, y1 = y + 1;
    double x2 = x + w, y2 = midY;

    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double mt = 1 - t;
        double px = mt * mt * x0 + 2 * mt * t * x1 + t * t * x2;
        double py = mt * mt * y0 + 2 * mt * t * y1 + t * t * y2;
        upper[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, upper);

   
    wxPoint lower[N + 1];
    double y3 = y + h;
    double y4 = y + h - 1;

    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double mt = 1 - t;
        double px = mt * mt * x0 + 2 * mt * t * x1 + t * t * x2;
        double py = mt * mt * y3 + 2 * mt * t * y4 + t * t * y2;
        lower[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, lower);
}









// 与非门 NAND = 与门 + 输出端小圆
void Canvas::DrawNandGate(wxDC& dc, int x, int y)
{
    DrawAndGate(dc, x, y);

    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(x + 60 + 5, y + 30, 6);
}

// 或非门 NOR = 或门 + 输出端小圆
void Canvas::DrawNorGate(wxDC& dc, int x, int y)
{
    DrawOrGate(dc, x, y);

    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(x + 80 + 5, y + 30, 6);
}

// 异或门 XOR = 或门 + 左侧多一条弧线
void Canvas::DrawXorGate(wxDC& dc, int x, int y)
{
    // 先画出或门本体
    DrawOrGate(dc, x, y);

    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int h = 60;
    const int N = 60;

    wxPoint left[N + 1];
    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double py = y + t * h;
        double bulge = 8.0 * std::sin(t * 3.1415926);
        double px = (x + 6 - 8) + bulge;  // 在或门左弧基础上整体左移 8 像素
        left[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, left);
}
