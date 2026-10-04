#include "Canvas.h"
#include "SelectionState.h"

#include <wx/dcbuffer.h>
#include <cmath>
#include <algorithm>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#endif

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
        wxFULL_REPAINT_ON_RESIZE), m_textTimer(this)
{
    SetBackgroundColour(*wxWHITE);
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    Bind(wxEVT_LEFT_DOWN, &Canvas::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &Canvas::OnLeftUp, this);
    Bind(wxEVT_LEFT_DCLICK, &Canvas::OnDoubleClick, this);
    Bind(wxEVT_MOTION, &Canvas::OnMouseMove, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &Canvas::OnCaptureLost, this);
    Bind(wxEVT_CHAR_HOOK, &Canvas::OnKeyDown, this);
    Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent&) { CancelMouseInteraction(true); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        CancelMouseInteraction(true);
        event.Skip();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateTextInput(); });
    m_textTimer.Start(150);
}

void Canvas::OnLeftDown(wxMouseEvent& event)
{
    CancelMouseInteraction();
    SetFocus();
    // 连线模式预留给引脚操作，不触发切换或元件拖动。
    if (g_selectedType == "WIRE") return;

    const wxPoint mouse = event.GetPosition();
    if (g_selectedType != "DELETE")
    {
        const int handle = HitTestTextHandle(mouse);
        if (handle >= 0 || IsTextBorder(mouse))
        {
            BeginTextTransform(m_selectedTextIndex, handle >= 0 ? handle : 8, mouse);
            return;
        }
        if (g_selectedType.IsEmpty() && m_selectedTextIndex >= 0 &&
            m_textBoxes[m_selectedTextIndex].rect.Contains(mouse) && HitTest(mouse.x, mouse.y) < 0)
        {
            BeginTextTransform(m_selectedTextIndex, 8, mouse);
            return;
        }
    }
    m_selectedTextIndex = -1;
    // 文本工具可点击框内编辑；其他工具优先操作元件，空白文本区域不拦截鼠标。
    const int textHit = HitTestText(mouse, g_selectedType == "TEXT");
    if (textHit >= 0 && (g_selectedType == "TEXT" || HitTest(mouse.x, mouse.y) < 0))
    {
        if (g_selectedType.IsEmpty()) BeginTextTransform(textHit, 8, mouse);
        else EditTextBox(textHit, mouse);
        return;
    }
    if (g_selectedType == "TEXT")
    {
        m_selectedIndex = -1;
        m_textStart = m_textEnd = mouse;
        m_creatingText = true;
        if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
        CaptureMouse();
        Refresh();
        return;
    }
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
    if (m_textTransformIndex >= 0)
    {
        UpdateTextTransform(event.GetPosition());
        CancelMouseInteraction();
        UpdateTextCursor(event.GetPosition());
        return;
    }
    if (m_creatingText)
    {
        m_textEnd = event.GetPosition();
        const wxRect rect = GetPendingTextRect();
        CancelMouseInteraction();
        // 先绘制框，再创建编辑控件；单击不会误建一个微小文本框。
        if (rect.width >= FromDIP(40) && rect.height >= FromDIP(24))
            CreateTextBox(rect);
        return;
    }
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
    if (m_textTransformIndex >= 0)
    {
        if (event.LeftIsDown()) UpdateTextTransform(event.GetPosition());
        else CancelMouseInteraction(true);
        return;
    }
    if (m_creatingText)
    {
        if (!event.LeftIsDown())
            CancelMouseInteraction();
        else
        {
            m_textEnd = event.GetPosition();
            Refresh();
        }
        return;
    }
    if (m_pressedIndex < 0 || m_pressedIndex >= static_cast<int>(m_components.size()))
    {
        UpdateTextCursor(event.GetPosition());
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

void Canvas::CancelMouseInteraction(bool restoreText)
{
    if (restoreText && m_textTransformIndex >= 0)
    {
        auto& box = m_textBoxes[m_textTransformIndex];
        box.rect = m_textTransformRect;
        box.scrollY = m_textTransformScrollY;
        box.editor->SetSize(box.rect.GetSize());
        NotifyTextSelection();
    }
    m_textTransformIndex = -1;
    m_textTransformHandle = -1;
    m_textTransformMoved = false;
    m_creatingText = false;
    m_pressedIndex = -1;
    m_dragging = false;
    m_togglePending = false;
    if (HasCapture()) ReleaseMouse();
    Refresh();
}

void Canvas::OnCaptureLost(wxMouseCaptureLostEvent&)
{
    CancelMouseInteraction(true);
}

void Canvas::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_ESCAPE)
    {
        CancelMouseInteraction(true);
        SetFocus();
    }
    else if (event.GetKeyCode() == WXK_RETURN && event.ControlDown() &&
        m_selectedTextIndex >= 0)
        SetFocus();
    else
        event.Skip();
}

wxRect Canvas::GetPendingTextRect() const
{
    return wxRect(std::min(m_textStart.x, m_textEnd.x),
        std::min(m_textStart.y, m_textEnd.y),
        std::abs(m_textEnd.x - m_textStart.x),
        std::abs(m_textEnd.y - m_textStart.y));
}

void Canvas::CreateTextBox(const wxRect& rect)
{
    auto* editor = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
        wxPoint(-32760, -32760), rect.GetSize(),
        wxTE_MULTILINE | wxTE_DONTWRAP | wxTE_NO_VSCROLL | wxBORDER_NONE);
    editor->SetFont(wxFontInfo(m_defaultTextPointSize).Family(wxFONTFAMILY_SWISS));
    editor->SetToolTip(wxT("在框内输入文字，Ctrl+Enter 完成编辑"));
    const int index = static_cast<int>(m_textBoxes.size());
    m_textBoxes.push_back({ editor, m_defaultTextPointSize, rect });
    editor->Bind(wxEVT_SET_FOCUS, [this, index](wxFocusEvent& event) {
        m_selectedTextIndex = index;
        m_selectedIndex = -1;
        NotifyTextSelection();
        Refresh();
        event.Skip();
    });
    editor->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        UpdateTextInput();
        Refresh();
        event.Skip();
    });
    editor->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        Refresh();
        event.Skip();
    });
    m_selectedTextIndex = index;
    m_selectedIndex = -1;
    NotifyTextSelection();
    editor->SetFocus();
    Refresh();
}

void Canvas::NotifyTextSelection()
{
    if (m_textSelectionCallback && m_selectedTextIndex >= 0 &&
        m_selectedTextIndex < static_cast<int>(m_textBoxes.size()))
    {
        const auto& box = m_textBoxes[m_selectedTextIndex];
        m_textSelectionCallback(box.rect, box.pointSize);
    }
}

int Canvas::GetTextFontSize() const
{
    if (m_selectedTextIndex >= 0 && m_selectedTextIndex < static_cast<int>(m_textBoxes.size()))
        return m_textBoxes[m_selectedTextIndex].pointSize;
    return m_defaultTextPointSize;
}

void Canvas::SetTextFontSize(int pointSize)
{
    m_defaultTextPointSize = std::clamp(pointSize, 6, 96);
    if (m_selectedTextIndex >= 0 && m_selectedTextIndex < static_cast<int>(m_textBoxes.size()))
    {
        auto& box = m_textBoxes[m_selectedTextIndex];
        box.pointSize = m_defaultTextPointSize;
        box.editor->SetFont(wxFontInfo(box.pointSize).Family(wxFONTFAMILY_SWISS));
        box.scrollY = 0;
        UpdateTextInput();
        NotifyTextSelection();
    }
    Refresh();
}

void Canvas::OnToolChanged()
{
    CancelMouseInteraction(true);
    m_selectedTextIndex = -1;
    m_selectedIndex = -1;
    SetCursor(wxCursor(g_selectedType == "TEXT" ? wxCURSOR_CROSS : wxCURSOR_ARROW));
    // 切换工具时结束框内编辑，但保留树状列表的键盘焦点。
    wxWindow* focus = wxWindow::FindFocus();
    if (focus && IsDescendant(focus)) SetFocus();
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
    DrawCanvas(dc);
}

void Canvas::DrawCanvas(wxDC& dc)
{
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
    if (m_creatingText)
    {
        dc.SetPen(wxPen(wxColour(30, 120, 220), 1, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(GetPendingTextRect());
    }
    DrawTextBoxes(dc);
    DrawTextSelection(dc);
}

std::array<wxPoint, 8> Canvas::GetTextHandles(const wxRect& rect) const
{
    const int left = rect.x, right = rect.GetRight();
    const int top = rect.y, bottom = rect.GetBottom();
    const int middleX = (left + right) / 2, middleY = (top + bottom) / 2;
    return {{ {left, top}, {middleX, top}, {right, top}, {right, middleY},
        {right, bottom}, {middleX, bottom}, {left, bottom}, {left, middleY} }};
}

int Canvas::HitTestTextHandle(const wxPoint& point) const
{
    if (m_selectedTextIndex < 0) return -1;
    const auto handles = GetTextHandles(m_textBoxes[m_selectedTextIndex].rect);
    const int radius = FromDIP(5);
    for (int i = 0; i < static_cast<int>(handles.size()); ++i)
        if (std::abs(point.x - handles[i].x) <= radius &&
            std::abs(point.y - handles[i].y) <= radius) return i;
    return -1;
}

bool Canvas::IsTextBorder(const wxPoint& point) const
{
    if (m_selectedTextIndex < 0) return false;
    const wxRect rect = m_textBoxes[m_selectedTextIndex].rect;
    wxRect outer = rect, inner = rect;
    const int margin = FromDIP(4);
    return outer.Inflate(margin).Contains(point) && !inner.Deflate(margin).Contains(point);
}

void Canvas::BeginTextTransform(int index, int handle, const wxPoint& point)
{
    m_selectedTextIndex = index;
    m_selectedIndex = -1;
    m_textTransformIndex = index;
    m_textTransformHandle = handle;
    m_textTransformRect = m_textBoxes[index].rect;
    m_textTransformScrollY = m_textBoxes[index].scrollY;
    m_textTransformStart = point;
    m_textTransformMoved = false;
    NotifyTextSelection();
    CaptureMouse();
    UpdateTextCursor(point);
    Refresh();
}

void Canvas::UpdateTextTransform(const wxPoint& point)
{
    const wxPoint delta = point - m_textTransformStart;
    if (!m_textTransformMoved && std::abs(delta.x) < m_dragThreshold &&
        std::abs(delta.y) < m_dragThreshold) return;
    m_textTransformMoved = true;
    wxRect rect = m_textTransformRect;
    if (m_textTransformHandle == 8) rect.Offset(delta);
    else
    {
        int left = rect.x, top = rect.y;
        int right = rect.x + rect.width, bottom = rect.y + rect.height;
        const int handle = m_textTransformHandle;
        const int minWidth = FromDIP(40), minHeight = FromDIP(24);
        if (handle == 0 || handle == 6 || handle == 7) left = std::min(left + delta.x, right - minWidth);
        if (handle == 2 || handle == 3 || handle == 4) right = std::max(right + delta.x, left + minWidth);
        if (handle == 0 || handle == 1 || handle == 2) top = std::min(top + delta.y, bottom - minHeight);
        if (handle == 4 || handle == 5 || handle == 6) bottom = std::max(bottom + delta.y, top + minHeight);
        rect = wxRect(left, top, right - left, bottom - top);
    }
    auto& box = m_textBoxes[m_textTransformIndex];
    if (box.rect == rect) return;
    box.rect = rect;
    if (m_textTransformHandle != 8)
    {
        box.editor->SetSize(rect.GetSize());
        box.scrollY = 0;
    }
    NotifyTextSelection();
    Refresh();
}

void Canvas::UpdateTextCursor(const wxPoint& point)
{
    if (g_selectedType == "WIRE" || g_selectedType == "DELETE")
    {
        SetCursor(wxCursor(wxCURSOR_ARROW));
        return;
    }
    const int handle = m_textTransformIndex >= 0 ? m_textTransformHandle : HitTestTextHandle(point);
    wxStockCursor cursor = g_selectedType == "TEXT" ? wxCURSOR_CROSS : wxCURSOR_ARROW;
    if (handle == 0 || handle == 4) cursor = wxCURSOR_SIZENWSE;
    else if (handle == 2 || handle == 6) cursor = wxCURSOR_SIZENESW;
    else if (handle == 1 || handle == 5) cursor = wxCURSOR_SIZENS;
    else if (handle == 3 || handle == 7) cursor = wxCURSOR_SIZEWE;
    else if (handle == 8 || IsTextBorder(point)) cursor = wxCURSOR_SIZING;
    else if (HitTestText(point, g_selectedType == "TEXT") >= 0 &&
        (g_selectedType == "TEXT" || HitTest(point.x, point.y) < 0))
        cursor = g_selectedType.IsEmpty() ? wxCURSOR_SIZING : wxCURSOR_IBEAM;
    SetCursor(wxCursor(cursor));
}

void Canvas::OnDoubleClick(wxMouseEvent& event)
{
    if (g_selectedType == "WIRE" || g_selectedType == "DELETE") return;
    const wxPoint point = event.GetPosition();
    const int index = HitTestText(point, true);
    if (index < 0 || (g_selectedType != "TEXT" && HitTest(point.x, point.y) >= 0)) return;
    CancelMouseInteraction();
    EditTextBox(index, point);
}

void Canvas::DrawTextSelection(wxDC& dc)
{
    if (m_selectedTextIndex < 0) return;
    const wxRect rect = m_textBoxes[m_selectedTextIndex].rect;
    const wxColour colour(30, 120, 220);
    dc.SetPen(wxPen(colour, 1, wxPENSTYLE_DOT));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(rect);
    dc.SetPen(wxPen(colour, 1));
    dc.SetBrush(*wxWHITE_BRUSH);
    const int radius = FromDIP(3);
    for (const auto& point : GetTextHandles(rect))
        dc.DrawRectangle(point.x - radius, point.y - radius, radius * 2 + 1, radius * 2 + 1);
    dc.SetFont(wxFontInfo(9));
    dc.SetTextForeground(colour);
    dc.SetBackgroundMode(wxTRANSPARENT);
    const wxString size = wxString::Format(wxT("%d × %d"), rect.width, rect.height);
    const int labelHeight = dc.GetTextExtent(size).y;
    dc.DrawText(size, rect.x, rect.y >= labelHeight + FromDIP(6) ?
        rect.y - labelHeight - FromDIP(6) : rect.y + rect.height + FromDIP(6));
}

std::vector<Canvas::TextLine> Canvas::LayoutText(const TextBox& box, wxDC& dc) const
{
    dc.SetFont(box.editor->GetFont());
    const int height = dc.GetCharHeight();
    std::vector<TextLine> lines;
    int y = box.rect.y - box.scrollY;
    for (int row = 0; row < box.editor->GetNumberOfLines(); ++row)
    {
        const wxString text = box.editor->GetLineText(row);
        wxArrayInt widths;
        dc.GetPartialTextExtents(text, widths);
        std::size_t begin = 0;
        do
        {
            std::size_t end = begin;
            const int previousWidth = begin ? widths[begin - 1] : 0;
            while (end < text.length() &&
                (end == begin || widths[end] - previousWidth <= box.rect.width)) ++end;
            lines.push_back({text.Mid(begin, end - begin),
                box.editor->XYToPosition(static_cast<long>(begin), row),
                wxPoint(box.rect.x, y), height});
            y += height;
            begin = end;
        } while (begin < text.length());
    }
    return lines;
}

int Canvas::HitTestText(const wxPoint& point, bool entireBox) const
{
    wxClientDC dc(const_cast<Canvas*>(this));
    for (int index = static_cast<int>(m_textBoxes.size()) - 1; index >= 0; --index)
    {
        const auto& box = m_textBoxes[index];
        if (!box.rect.Contains(point)) continue;
        if (entireBox) return index;
        for (const auto& line : LayoutText(box, dc))
            if (wxRect(line.origin, wxSize(dc.GetTextExtent(line.text).x, line.height)).Contains(point))
                return index;
    }
    return -1;
}

void Canvas::EditTextBox(int index, const wxPoint& point)
{
    m_selectedTextIndex = index;
    m_selectedIndex = -1;
    auto& box = m_textBoxes[index];
    wxClientDC dc(this);
    const auto lines = LayoutText(box, dc);
    long insertion = box.editor->GetLastPosition();
    for (const auto& line : lines)
    {
        if (point.y >= line.origin.y + line.height) continue;
        wxArrayInt widths;
        dc.GetPartialTextExtents(line.text, widths);
        std::size_t column = 0;
        int previous = 0;
        while (column < widths.size())
        {
            if (point.x - line.origin.x < (previous + widths[column]) / 2) break;
            previous = widths[column++];
        }
        insertion = line.start + static_cast<long>(column);
        break;
    }
    box.editor->SetFocus();
    box.editor->SetInsertionPoint(insertion);
    NotifyTextSelection();
    UpdateTextInput();
    Refresh();
}

void Canvas::UpdateTextInput()
{
    if (m_selectedTextIndex < 0 || m_selectedTextIndex >= static_cast<int>(m_textBoxes.size())) return;
    auto& box = m_textBoxes[m_selectedTextIndex];
    if (wxWindow::FindFocus() != box.editor) return;
    wxClientDC dc(this);
    const auto lines = LayoutText(box, dc);
    const long position = box.editor->GetInsertionPoint();
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        if (i + 1 < lines.size() && position >= lines[i + 1].start) continue;
        const int x = line.origin.x + dc.GetTextExtent(box.editor->GetRange(line.start, position)).x;
        const int offset = line.origin.y < box.rect.y ? line.origin.y - box.rect.y :
            std::max(0, line.origin.y + line.height - box.rect.GetBottom() - 1);
        box.scrollY = std::max(0, box.scrollY + offset);
#ifdef __WXMSW__
        // 输入控件位于画布外，把输入法候选窗口定位到画布的文字光标。
        const HWND hwnd = static_cast<HWND>(box.editor->GetHandle());
        const HIMC context = ImmGetContext(hwnd);
        if (context)
        {
            const wxPoint local = box.editor->ScreenToClient(ClientToScreen(wxPoint(x, line.origin.y - offset)));
            COMPOSITIONFORM composition = {};
            composition.dwStyle = CFS_FORCE_POSITION;
            composition.ptCurrentPos = {local.x, local.y};
            ImmSetCompositionWindow(context, &composition);
            CANDIDATEFORM candidate = {};
            candidate.dwStyle = CFS_CANDIDATEPOS;
            candidate.ptCurrentPos = {local.x, local.y + line.height};
            ImmSetCandidateWindow(context, &candidate);
            ImmReleaseContext(hwnd, context);
        }
#endif
        break;
    }
    Refresh();
}

void Canvas::DrawTextBoxes(wxDC& dc)
{
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(*wxBLACK);
    for (const auto& box : m_textBoxes)
    {
        wxDCClipper clip(dc, box.rect);
        const auto lines = LayoutText(box, dc);
        long from, to;
        box.editor->GetSelection(&from, &to);
        const bool editing = wxWindow::FindFocus() == box.editor;
        const long insertion = box.editor->GetInsertionPoint();
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const auto& line = lines[i];
            dc.DrawText(line.text, line.origin);
            const long end = line.start + static_cast<long>(line.text.length());
            if (editing && from < to && from < end && to > line.start)
            {
                const int left = dc.GetTextExtent(box.editor->GetRange(line.start, std::max(from, line.start))).x;
                const int right = dc.GetTextExtent(box.editor->GetRange(line.start, std::min(to, end))).x;
                dc.SetPen(wxPen(wxColour(30, 120, 220), 1));
                dc.DrawLine(line.origin.x + left, line.origin.y + line.height - 1,
                    line.origin.x + right, line.origin.y + line.height - 1);
            }
            if (editing && from == to && insertion >= line.start && insertion <= end &&
                (i + 1 == lines.size() || insertion < lines[i + 1].start))
            {
                const int x = line.origin.x + dc.GetTextExtent(box.editor->GetRange(line.start, insertion)).x;
                dc.SetPen(*wxBLACK_PEN);
                dc.DrawLine(x, line.origin.y, x, line.origin.y + line.height);
            }
        }
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
