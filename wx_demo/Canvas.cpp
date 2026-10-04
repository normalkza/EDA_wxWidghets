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
#include "IOComponent.h"


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
    Bind(wxEVT_LEFT_UP, &Canvas::OnLeftUp, this);
    Bind(wxEVT_MOTION, &Canvas::OnMouseMove, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &Canvas::OnCaptureLost, this);
    Bind(wxEVT_CHAR_HOOK, &Canvas::OnKeyDown, this);
    Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent&) { CancelMouseInteraction(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        CancelMouseInteraction();
        event.Skip();
    });
}

void Canvas::OnLeftDown(wxMouseEvent& event)
{
    CancelMouseInteraction();
    SetFocus();
    // 连线模式预留给引脚操作，不触发切换或元件拖动。
    if (g_selectedType == "WIRE") return;

    const wxPoint mouse = event.GetPosition();
    const int hit = HitTest(mouse.x, mouse.y);

    if (hit >= 0)
    {
        m_selectedIndex = hit;
        m_pressedIndex = hit;
        m_pressPosition = mouse;
        m_dragging = false;
        const auto& pc = m_components[hit];
        m_togglePending = pc.comp->name == "INPUT" &&
            g_selectedType != "DELETE" && g_selectedType != "TEXT" &&
            wxRect(pc.x, pc.y, 40, 40).Contains(mouse);
        m_dragOffset = mouse - wxPoint(m_components[hit].x, m_components[hit].y);
        if (m_selectionCallback)
            m_selectionCallback(m_components[hit].comp, m_components[hit].x, m_components[hit].y);
        CaptureMouse();
        Refresh();
        return;
    }

    m_selectedIndex = -1;
    if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
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
    else if (type == "INPUT")  comp = new InputComponent();
    else if (type == "OUTPUT") comp = new OutputComponent();

    if (comp)
    {
        m_components.push_back({ comp, x, y });
        m_selectedIndex = static_cast<int>(m_components.size()) - 1;
        if (m_selectionCallback) m_selectionCallback(comp, x, y);
        Refresh();
    }
}

void Canvas::OnLeftUp(wxMouseEvent& event)
{
    if (m_pressedIndex >= 0 && m_pressedIndex < static_cast<int>(m_components.size()))
    {
        auto& pc = m_components[m_pressedIndex];
        const wxPoint mouse = event.GetPosition();
        const wxPoint delta = mouse - m_pressPosition;
        const bool releasedWithoutMoving = std::abs(delta.x) < m_dragThreshold &&
            std::abs(delta.y) < m_dragThreshold;
        if (m_togglePending && !m_dragging && releasedWithoutMoving &&
            g_selectedType != "WIRE" && g_selectedType != "DELETE" &&
            g_selectedType != "TEXT" &&
            HitTest(mouse.x, mouse.y) == m_pressedIndex &&
            wxRect(pc.x, pc.y, 40, 40).Contains(mouse))
        {
            auto& value = pc.comp->outputs[0].value;
            value = value == LogicValue::Low ? LogicValue::High : LogicValue::Low;
            if (m_selectionCallback) m_selectionCallback(pc.comp, pc.x, pc.y);
        }
    }
    CancelMouseInteraction();
    event.Skip();
}

void Canvas::OnMouseMove(wxMouseEvent& event)
{
    if (m_pressedIndex < 0 || m_pressedIndex >= static_cast<int>(m_components.size()))
    {
        event.Skip();
        return;
    }

    if (!event.LeftIsDown())
    {
        CancelMouseInteraction();
        event.Skip();
        return;
    }

    const wxPoint mouse = event.GetPosition();
    const wxPoint delta = mouse - m_pressPosition;
    if (!m_dragging)
    {
        if (std::abs(delta.x) < m_dragThreshold && std::abs(delta.y) < m_dragThreshold)
            return;
        m_dragging = true;
        m_togglePending = false;
    }
    auto& pc = m_components[m_pressedIndex];
    pc.x = SnapToGrid(mouse.x - m_dragOffset.x);
    pc.y = SnapToGrid(mouse.y - m_dragOffset.y);
    if (m_selectionCallback) m_selectionCallback(pc.comp, pc.x, pc.y);
    Refresh();
}

void Canvas::CancelMouseInteraction()
{
    m_pressedIndex = -1;
    m_dragging = false;
    m_togglePending = false;
    if (HasCapture()) ReleaseMouse();
    Refresh();
}

void Canvas::OnCaptureLost(wxMouseCaptureLostEvent&)
{
    CancelMouseInteraction();
}

void Canvas::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_ESCAPE)
        CancelMouseInteraction();
    else
        event.Skip();
}

int Canvas::HitTest(int x, int y) const
{
    for (int i = static_cast<int>(m_components.size()) - 1; i >= 0; --i)
    {
        if (GetComponentRect(m_components[i]).Contains(x, y))
            return i;
    }
    return -1;
}

wxRect Canvas::GetComponentRect(const PlacedComponent& pc) const
{
    if (!pc.comp) return wxRect();

    // 输入/输出的本体及引线占 60 x 40，四周留出 5 像素命中余量。
    if (pc.comp->name == "INPUT" || pc.comp->name == "OUTPUT")
        return wxRect(pc.x - 5, pc.y - 5, 70, 50);

    // 包围盒覆盖当前已实现的六种门电路，并留出拖拽余量。
    const int width = (pc.comp->name == "NOT") ? 50 :
        ((pc.comp->name == "AND" || pc.comp->name == "NAND") ? 75 : 95);
    return wxRect(pc.x - 5, pc.y - 5, width, 70);
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
        else if (pc.comp->name == "INPUT")
            DrawInput(dc, pc.x, pc.y, pc.comp->outputs[0].value);
        else if (pc.comp->name == "OUTPUT")
            DrawOutput(dc, pc.x, pc.y, pc.comp->inputs[0].value);

    }

    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_components.size()))
    {
        dc.SetPen(wxPen(wxColour(30, 120, 220), 2, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(GetComponentRect(m_components[m_selectedIndex]));
    }
}


// 输入：方框 + 右侧输出引线，连接端落在 (x + 60, y + 20)。
void Canvas::DrawInput(wxDC& dc, int x, int y, LogicValue value)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxWHITE_BRUSH);
    dc.SetTextForeground(*wxBLACK);
    dc.DrawRectangle(x, y, 40, 40);
    dc.DrawLine(x + 40, y + 20, x + 60, y + 20);
    dc.DrawLabel(value == LogicValue::High ? wxT("1") : wxT("0"),
        wxRect(x, y, 40, 40), wxALIGN_CENTER);
}

// 输出：圆框 + 左侧输入引线，连接端落在 (x, y + 20)。
void Canvas::DrawOutput(wxDC& dc, int x, int y, LogicValue value)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxWHITE_BRUSH);
    dc.SetTextForeground(*wxBLACK);
    dc.DrawCircle(x + 40, y + 20, 20);
    dc.DrawLine(x, y + 20, x + 20, y + 20);
    dc.DrawLabel(value == LogicValue::High ? wxT("1") : wxT("0"),
        wxRect(x + 20, y, 40, 40), wxALIGN_CENTER);
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
