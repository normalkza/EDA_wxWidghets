#include "Canvas.h"
#include "SelectionState.h"

#include <wx/dcbuffer.h>
#include <cmath>
#include <algorithm>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <sstream>
#include <iomanip>
#include <memory>
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

namespace
{
    Component* CreateComponent(const wxString& type)
    {
        if (type == "AND") return new AndGate();
        if (type == "OR") return new OrGate();
        if (type == "NOT") return new NotGate();
        if (type == "NAND") return new NandGate();
        if (type == "NOR") return new NorGate();
        if (type == "XOR") return new XorGate();
        if (type == "INPUT") return new InputComponent();
        if (type == "OUTPUT") return new OutputComponent();
        return nullptr;
    }

    wxDataFormat CanvasClipboardFormat()
    {
        return wxDataFormat("wx_demo.canvas.object.v1");
    }
}

wxBEGIN_EVENT_TABLE(Canvas, wxPanel)
EVT_PAINT(Canvas::OnPaint)
wxEND_EVENT_TABLE()

Canvas::Canvas(wxWindow* parent, wxClipboard* clipboard)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxFULL_REPAINT_ON_RESIZE), m_dragTimer(this),
        m_clipboard(clipboard ? clipboard : wxTheClipboard), m_textTimer(this)
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
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { UpdateTextInput(); }, m_textTimer.GetId());
    Bind(wxEVT_TIMER, &Canvas::OnDragTimer, this, m_dragTimer.GetId());
    m_textTimer.Start(150);
}

void Canvas::OnLeftDown(wxMouseEvent& event)
{
    CancelMouseInteraction();
    SetFocusIgnoringChildren();
    // 连线模式预留给引脚操作，不触发切换或元件拖动。
    if (g_selectedType == "WIRE") return;

    const wxPoint mouse = event.GetPosition();
    if (g_selectedType == "DELETE")
    {
        DeleteAtPoint(mouse);
        return;
    }
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

    Component* comp = CreateComponent(type);

    if (comp)
    {
        m_components.push_back({ comp, x, y });
        m_selectedIndex = static_cast<int>(m_components.size()) - 1;
        // 单次放置后恢复选择/拖动模式，同时保留新元件的选中状态。
        g_selectedType.Clear();
        SetCursor(wxCursor(wxCURSOR_ARROW));
        if (m_componentPlacedCallback) m_componentPlacedCallback();
        if (m_selectionCallback) m_selectionCallback(comp, x, y);
        Refresh();
    }
}

void Canvas::DeleteAtPoint(const wxPoint& point)
{
    // 与普通选择保持一致：重叠时优先命中最上面的元件。
    const int componentIndex = HitTest(point.x, point.y);
    const int textIndex = componentIndex < 0 ? HitTestText(point, true) : -1;
    DeleteItem(componentIndex, textIndex);
}

void Canvas::DeleteItem(int componentIndex, int textIndex)
{
    m_selectedIndex = -1;
    m_selectedTextIndex = -1;
    if (componentIndex >= 0)
    {
        delete m_components[componentIndex].comp;
        m_components.erase(m_components.begin() + componentIndex);
    }
    else if (textIndex >= 0)
    {
        auto* editor = m_textBoxes[textIndex].editor;
        m_textBoxes.erase(m_textBoxes.begin() + textIndex);
        editor->Destroy();
    }
    if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
    Refresh(false);
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
        // 松开时处理最后一个鼠标位置，避免定时器尚未绘制最后一帧。
        if (m_dragging || !releasedWithoutMoving)
        {
            m_dragging = true;
            m_togglePending = false;
            MoveDraggedComponent(mouse, true);
        }
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
        m_dragFrame = 0;
        m_dragTimer.Start(16);
    }
    // 合并高频鼠标事件；绘制按约 60 帧/秒处理最新位置。
    m_dragPosition = mouse;
}

void Canvas::MoveDraggedComponent(const wxPoint& mouse, bool snap)
{
    if (m_pressedIndex < 0 || m_pressedIndex >= static_cast<int>(m_components.size())) return;
    auto& pc = m_components[m_pressedIndex];
    const int x = snap ? SnapToGrid(mouse.x - m_dragOffset.x) : mouse.x - m_dragOffset.x;
    const int y = snap ? SnapToGrid(mouse.y - m_dragOffset.y) : mouse.y - m_dragOffset.y;
    if (pc.x == x && pc.y == y) return;
    const wxRect oldRect = GetComponentRect(pc);
    pc.x = x;
    pc.y = y;
    wxRect dirty = oldRect.Union(GetComponentRect(pc));
    dirty.Inflate(3); // 包含选中虚线和画笔边缘，清除旧位置的残影。
    RefreshRect(dirty, false);
}

void Canvas::OnDragTimer(wxTimerEvent&)
{
    if (!m_dragging || m_pressedIndex < 0) return;
    MoveDraggedComponent(m_dragPosition, false);
    // 属性区约 20 次/秒更新，避免布局计算阻塞每一帧。
    if (++m_dragFrame % 3 == 0 && m_selectionCallback)
    {
        const auto& pc = m_components[m_pressedIndex];
        m_selectionCallback(pc.comp, pc.x, pc.y);
    }
    Update();
}

void Canvas::CancelMouseInteraction(bool restoreText)
{
    m_dragTimer.Stop();
    if (m_dragging && m_pressedIndex >= 0 &&
        m_pressedIndex < static_cast<int>(m_components.size()))
    {
        auto& pc = m_components[m_pressedIndex];
        MoveDraggedComponent(wxPoint(pc.x, pc.y) + m_dragOffset, true);
        if (m_selectionCallback) m_selectionCallback(pc.comp, pc.x, pc.y);
    }
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
        SetFocusIgnoringChildren();
    }
    else if (event.GetKeyCode() == WXK_RETURN && event.ControlDown() &&
        m_selectedTextIndex >= 0)
        SetFocusIgnoringChildren();
    else if (IsEditingText())
        event.Skip(); // 编辑文字时，交给原生控件处理剪贴板和退格。
    else if (event.ControlDown() && !event.AltDown() && !event.ShiftDown() &&
        (event.GetKeyCode() == 'C' || event.GetKeyCode() == 'V' || event.GetKeyCode() == 'X'))
    {
        const int command = event.GetKeyCode() == 'C' ? wxID_COPY :
            (event.GetKeyCode() == 'V' ? wxID_PASTE : wxID_CUT);
        HandleEditCommand(command);
    }
    else if (event.GetKeyCode() == WXK_BACK && !event.HasModifiers())
        DeleteSelection();
    else
        event.Skip();
}

bool Canvas::IsEditingText() const
{
    const wxWindow* focus = wxWindow::FindFocus();
    return std::any_of(m_textBoxes.begin(), m_textBoxes.end(),
        [focus](const TextBox& box) { return box.editor == focus; });
}

bool Canvas::HandleEditCommand(int command)
{
    // 菜单快捷键也必须保留文字编辑控件的正常行为。
    if (IsEditingText())
    {
        auto* editor = static_cast<wxTextCtrl*>(wxWindow::FindFocus());
        if (command == wxID_COPY) editor->Copy();
        else if (command == wxID_CUT) editor->Cut();
        else if (command == wxID_PASTE) editor->Paste();
        else if (command == wxID_DELETE)
        {
            long from, to;
            editor->GetSelection(&from, &to);
            if (from != to) editor->Remove(from, to);
        }
        else return false;
        return true;
    }
    if (command == wxID_COPY) return CopySelection();
    if (command == wxID_CUT) return CutSelection();
    if (command == wxID_PASTE) return PasteSelection();
    if (command == wxID_DELETE) return DeleteSelection();
    return false;
}

bool Canvas::DeleteSelection()
{
    const int component = m_selectedIndex;
    const int text = m_selectedTextIndex;
    if (component < 0 && text < 0) return false;
    CancelMouseInteraction();
    DeleteItem(component, text);
    return true;
}

bool Canvas::CopySelection()
{
    CancelMouseInteraction();
    std::ostringstream stream;
    wxString plainText;
    bool text = false;
    stream << "WX_DEMO_1 ";
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_components.size()))
    {
        const auto& pc = m_components[m_selectedIndex];
        std::string inputs, outputs;
        for (const auto& pin : pc.comp->inputs) inputs += pin.value == LogicValue::High ? '1' : '0';
        for (const auto& pin : pc.comp->outputs) outputs += pin.value == LogicValue::High ? '1' : '0';
        stream << "COMPONENT " << std::quoted(pc.comp->name) << ' ' << pc.x << ' ' << pc.y
            << ' ' << std::quoted(inputs) << ' ' << std::quoted(outputs);
    }
    else if (m_selectedTextIndex >= 0 && m_selectedTextIndex < static_cast<int>(m_textBoxes.size()))
    {
        const auto& box = m_textBoxes[m_selectedTextIndex];
        plainText = box.editor->GetValue();
        text = true;
        stream << "TEXT " << box.rect.x << ' ' << box.rect.y << ' ' << box.rect.width << ' '
            << box.rect.height << ' ' << box.pointSize << ' ' << box.scrollY << ' '
            << std::quoted(plainText.ToStdString(wxConvUTF8));
    }
    else return false;
    wxClipboardLocker locker(m_clipboard);
    if (!locker) return false;
    auto* data = new wxDataObjectComposite();
    auto* object = new wxCustomDataObject(CanvasClipboardFormat());
    const std::string payload = stream.str();
    object->SetData(payload.size(), payload.data());
    data->Add(object, true);
    if (text) data->Add(new wxTextDataObject(plainText));
    if (!m_clipboard->SetData(data)) return false;
    m_clipboard->Flush();
    m_pasteCount = 0;
    return true;
}

bool Canvas::CutSelection()
{
    // 写入剪贴板成功之后才移除原对象。
    return CopySelection() && DeleteSelection();
}

bool Canvas::PasteSelection()
{
    std::string payload;
    {
        wxClipboardLocker locker(m_clipboard);
        if (!locker) return false;
        if (m_clipboard->IsSupported(CanvasClipboardFormat()))
        {
            wxCustomDataObject data(CanvasClipboardFormat());
            if (!m_clipboard->GetData(data) || data.GetSize() == 0 ||
                data.GetSize() > 16 * 1024 * 1024) return false;
            payload.assign(static_cast<const char*>(data.GetData()), data.GetSize());
        }
        else
        {
            wxTextDataObject data;
            if (!m_clipboard->GetData(data) || data.GetText().empty()) return false;
            std::ostringstream stream;
            stream << "WX_DEMO_1 TEXT 0 0 240 100 " << m_defaultTextPointSize << " 0 "
                << std::quoted(data.GetText().ToStdString(wxConvUTF8));
            payload = stream.str();
        }
    }
    std::istringstream stream(payload);
    std::string magic, kind, type, inputs, outputs, text;
    int x = 0, y = 0, width = 0, height = 0, pointSize = 14, scrollY = 0;
    if (!(stream >> magic >> kind) || magic != "WX_DEMO_1") return false;
    std::unique_ptr<Component> component;
    if (kind == "COMPONENT")
    {
        if (!(stream >> std::quoted(type) >> x >> y >> std::quoted(inputs) >> std::quoted(outputs)))
            return false;
        component.reset(CreateComponent(wxString::FromUTF8(type)));
        if (!component || inputs.size() != component->inputs.size() ||
            outputs.size() != component->outputs.size() ||
            inputs.find_first_not_of("01") != std::string::npos ||
            outputs.find_first_not_of("01") != std::string::npos) return false;
        for (std::size_t i = 0; i < inputs.size(); ++i)
            component->inputs[i].value = inputs[i] == '1' ? LogicValue::High : LogicValue::Low;
        for (std::size_t i = 0; i < outputs.size(); ++i)
            component->outputs[i].value = outputs[i] == '1' ? LogicValue::High : LogicValue::Low;
        width = 95;
        height = 70;
    }
    else if (kind == "TEXT")
    {
        if (!(stream >> x >> y >> width >> height >> pointSize >> scrollY >> std::quoted(text)) ||
            width < 1 || width > 10000 || height < 1 || height > 10000 ||
            pointSize < 6 || pointSize > 96 || scrollY < 0 || scrollY > 1000000) return false;
    }
    else return false;
    stream >> std::ws;
    if (!stream.eof() || x < -1000000 || x > 1000000 || y < -1000000 || y > 1000000) return false;
    if (payload != m_lastPastePayload) m_pasteCount = 0;
    const int pasteCount = std::min(m_pasteCount + 1, 1000);
    const int offset = pasteCount * m_gridSize;
    const wxSize size = GetClientSize();
    x = std::clamp(x + offset, 0, std::max(0, size.x - width));
    y = std::clamp(y + offset, 0, std::max(0, size.y - height));
    CancelMouseInteraction(true);
    if (component)
    {
        x = SnapToGrid(x);
        y = SnapToGrid(y);
        m_components.push_back({ component.get(), x, y });
        component.release();
        m_selectedIndex = static_cast<int>(m_components.size()) - 1;
        m_selectedTextIndex = -1;
    }
    else
    {
        CreateTextBox(wxRect(x, y, width, height));
        auto& box = m_textBoxes.back();
        box.pointSize = pointSize;
        box.editor->SetFont(wxFontInfo(pointSize).Family(wxFONTFAMILY_SWISS));
        box.editor->ChangeValue(wxString::FromUTF8(text));
        box.scrollY = scrollY;
    }
    m_lastPastePayload = payload;
    m_pasteCount = pasteCount;
    g_selectedType.Clear();
    SetCursor(wxCursor(wxCURSOR_ARROW));
    if (m_componentPlacedCallback) m_componentPlacedCallback();
    SetFocusIgnoringChildren();
    if (m_selectedIndex >= 0 && m_selectionCallback)
    {
        const auto& pc = m_components[m_selectedIndex];
        m_selectionCallback(pc.comp, pc.x, pc.y);
    }
    else NotifyTextSelection();
    Refresh(false);
    return true;
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
    editor->Bind(wxEVT_SET_FOCUS, [this, editor](wxFocusEvent& event) {
        // 删除文本框后索引会变化；按编辑控件查找当前索引。
        const auto box = std::find_if(m_textBoxes.begin(), m_textBoxes.end(),
            [editor](const TextBox& text) { return text.editor == editor; });
        if (box == m_textBoxes.end())
        {
            event.Skip();
            return;
        }
        m_selectedTextIndex = static_cast<int>(box - m_textBoxes.begin());
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
    if (focus && IsDescendant(focus)) SetFocusIgnoringChildren();
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
    const wxRect updateRect = GetUpdateRegion().GetBox();
    wxAutoBufferedPaintDC dc(this);
    wxDCClipper clip(dc, updateRect);
    DrawCanvas(dc, updateRect);
}

void Canvas::UpdateGridBitmap()
{
    const wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;
    if (m_gridBitmap.IsOk() && m_gridBitmap.GetSize() == size) return;
    m_gridBitmap = wxBitmap(size.x, size.y);
    wxMemoryDC gridDC(m_gridBitmap);
    gridDC.SetBackground(*wxWHITE_BRUSH);
    gridDC.Clear();
    gridDC.SetPen(wxPen(wxColour(200, 200, 200), 1, wxPENSTYLE_SOLID));
    gridDC.SetBrush(*wxWHITE_BRUSH);
    for (int x = 0; x <= size.GetWidth(); x += m_gridSize)
        for (int y = 0; y <= size.GetHeight(); y += m_gridSize)
            gridDC.DrawCircle(x, y, 1);
}

void Canvas::DrawCanvas(wxDC& dc, const wxRect& updateRect)
{
    // 网格仅在画布尺寸改变时生成；拖动时直接复制缓存。
    UpdateGridBitmap();
    if (m_gridBitmap.IsOk()) dc.DrawBitmap(m_gridBitmap, 0, 0);

    // 元件
    for (const auto& pc : m_components)
    {
        if (!pc.comp || !GetComponentRect(pc).Intersects(updateRect)) continue;

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
    DrawTextBoxes(dc, updateRect);
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

void Canvas::DrawTextBoxes(wxDC& dc, const wxRect& updateRect)
{
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(*wxBLACK);
    for (const auto& box : m_textBoxes)
    {
        if (!box.rect.Intersects(updateRect)) continue;
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

// 非门 NOT
// ============================================================
void Canvas::DrawNotGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int w = 40;
    int h = 40;

    dc.DrawLine(x, y, x, y + h);
    dc.DrawLine(x, y, x + w, y + h / 2);
    dc.DrawLine(x, y + h, x + w, y + h / 2);
    dc.DrawCircle(x + w + 5, y + h / 2, 4);

    dc.SetBrush(*wxBLUE_BRUSH);
    dc.SetPen(wxPen(*wxBLUE, 1));
    dc.DrawCircle(x, y + h / 2, 3);

    dc.SetBrush(*wxRED_BRUSH);
    dc.SetPen(wxPen(*wxRED, 1));
    dc.DrawCircle(x + w + 10, y + h / 2, 3);
}


// ============================================================
// 与门 AND
// ============================================================
void Canvas::DrawAndGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int h = 80;   // 4 格高
    int r = h / 2;

    dc.DrawLine(x, y, x, y + h);
    dc.DrawLine(x, y, x + r, y);
    dc.DrawLine(x, y + h, x + r, y + h);
    dc.DrawEllipticArc(x, y, h, h, 270, 90);

    dc.SetBrush(*wxBLUE_BRUSH);
    dc.SetPen(wxPen(*wxBLUE, 1));
    int ys[5] = { 0, 20, 40, 60, 80 };
    for (int i = 0; i < 5; ++i)
        dc.DrawCircle(x, y + ys[i], 3);

    // 红点：右半圆最右端
    dc.SetBrush(*wxRED_BRUSH);
    dc.SetPen(wxPen(*wxRED, 1));
    dc.DrawCircle(x + h, y + h / 2, 3);
}

// ============================================================
// 或门 OR
// ============================================================
void Canvas::DrawOrGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int w = 80;
    int h = 120;
    int midY = y + h / 2;

    const int N = 80;

    wxPoint left[N + 1];
    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double py = y + t * h;
        double bulge = 10.0 * std::sin(t * 3.1415926);
        double px = x + bulge;
        left[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, left);

    wxPoint upper[N + 1];
    double x0 = x, y0 = y;
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

    // 5 条短横线 + 5 个蓝点
    dc.SetPen(wxPen(*wxBLACK, 2));
    int ys[5] = { 20, 40, 60, 80, 100 };
    for (int i = 0; i < 5; ++i)
        dc.DrawLine(x, y + ys[i], x + 8, y + ys[i]);

    dc.SetBrush(*wxBLUE_BRUSH);
    dc.SetPen(wxPen(*wxBLUE, 1));
    for (int i = 0; i < 5; ++i)
        dc.DrawCircle(x, y + ys[i], 3);

    // 输出红点
    dc.SetBrush(*wxRED_BRUSH);
    dc.SetPen(wxPen(*wxRED, 1));
    dc.DrawCircle(x + w, y + h / 2, 3);
}

// ============================================================
// 与非门 NAND
// ============================================================
void Canvas::DrawNandGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int h = 80;   // 与 AND 保持一致
    int r = h / 2;

    // 与门本体
    dc.DrawLine(x, y, x, y + h);
    dc.DrawLine(x, y, x + r, y);
    dc.DrawLine(x, y + h, x + r, y + h);
    dc.DrawEllipticArc(x, y, h, h, 270, 90);

    // 5 个蓝点：y+0/20/40/60/80，全部压在格点上
    dc.SetBrush(*wxBLUE_BRUSH);
    dc.SetPen(wxPen(*wxBLUE, 1));
    int ys[5] = { 0, 20, 40, 60, 80 };
    for (int i = 0; i < 5; ++i)
        dc.DrawCircle(x, y + ys[i], 3);

    // 黑小圆（贴在与门右端）
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(x + h + 5, y + h / 2, 6);

    // 短横线：从黑小圆右缘画到红点位置
    dc.DrawLine(x + h + 11, y + h / 2, x + h + 17, y + h / 2);

    // 红点（位置保持不变）
    dc.SetBrush(*wxRED_BRUSH);
    dc.SetPen(wxPen(*wxRED, 1));
    dc.DrawCircle(x + h + 17, y + h / 2, 3);
}

//或非门
void Canvas::DrawNorGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int w = 80;
    int h = 120;
    int midY = y + h / 2;

    const int N = 80;

    // 左侧短弧
    wxPoint left[N + 1];
    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double py = y + t * h;
        double bulge = 10.0 * std::sin(t * 3.1415926);
        double px = (x - 10) + bulge;
        left[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, left);

    // 上弧
    wxPoint upper[N + 1];
    double x0 = x - 10, y0 = y;
    double x1 = x + w * 0.55, y1 = y + 1;
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

    // 下弧
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

    // 5 条短横线 + 5 个蓝点，终点动态计算弧线 x 坐标
    dc.SetPen(wxPen(*wxBLACK, 2));
    int ys[5] = { 20, 40, 60, 80, 100 };
    for (int i = 0; i < 5; ++i)
    {
        double t = (double)ys[i] / h;
        double bulge = 10.0 * std::sin(t * 3.1415926);
        double arcX = (x - 10) + bulge;   // 弧线在该 y 处的 x 坐标

        // 从蓝点到弧线，画横线
        dc.DrawLine(x - 20, y + ys[i], (int)arcX, y + ys[i]);

        // 蓝点
        dc.SetBrush(*wxBLUE_BRUSH);
        dc.SetPen(wxPen(*wxBLUE, 1));
        dc.DrawCircle(x - 20, y + ys[i], 3);

        dc.SetPen(wxPen(*wxBLACK, 2));   // 恢复黑色画笔
    }

    // 输出：黑圆圈
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawCircle(x + w + 5, y + h / 2, 6);

    // 短横线：从黑圈连到红点
    dc.DrawLine(x + w + 11, y + h / 2, x + w + 17, y + h / 2);

    // 红点
    dc.SetBrush(*wxRED_BRUSH);
    dc.SetPen(wxPen(*wxRED, 1));
    dc.DrawCircle(x + w + 17, y + h / 2, 3);
}
// ============================================================
// 异或门 XOR
// ============================================================
void Canvas::DrawXorGate(wxDC& dc, int x, int y)
{
    dc.SetPen(wxPen(*wxBLACK, 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    int w = 80;
    int h = 120;
    int midY = y + h / 2;

    const int N = 80;

    // 左侧额外弧（异或门特征）：外凸最大处为 x - 18
    wxPoint left[N + 1];
    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double py = y + t * h;
        double bulge = 10.0 * std::sin(t * 3.1415926);
        double px = (x - 10 - 8) + bulge;   // 起点 x-18，最凸处 x-8
        left[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, left);

    // 左侧内弧（或门左弧）：外凸最大处为 x
    wxPoint left2[N + 1];
    for (int i = 0; i <= N; ++i)
    {
        double t = (double)i / N;
        double py = y + t * h;
        double bulge = 10.0 * std::sin(t * 3.1415926);
        double px = (x - 10) + bulge;
        left2[i] = wxPoint((int)px, (int)py);
    }
    dc.DrawLines(N + 1, left2);

    // 上弧
    wxPoint upper[N + 1];
    double x0 = x - 10, y0 = y;
    double x1 = x + w * 0.55, y1 = y + 1;
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

    // 下弧
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

    // 5 条短横线 + 5 个蓝点：连到额外左弧上
    dc.SetPen(wxPen(*wxBLACK, 2));
    int ys[5] = { 20, 40, 60, 80, 100 };
    for (int i = 0; i < 5; ++i)
    {
        double t = (double)ys[i] / h;
        double bulge = 10.0 * std::sin(t * 3.1415926);
        double arcX = (x - 10 - 8) + bulge;   // 额外弧在该 y 处的 x

        dc.DrawLine(x - 20, y + ys[i], (int)arcX, y + ys[i]);

        dc.SetBrush(*wxBLUE_BRUSH);
        dc.SetPen(wxPen(*wxBLUE, 1));
        dc.DrawCircle(x - 20, y + ys[i], 3);

        dc.SetPen(wxPen(*wxBLACK, 2));
    }

    // 输出红点
    dc.SetBrush(*wxRED_BRUSH);
    dc.SetPen(wxPen(*wxRED, 1));
    dc.DrawCircle(x + w, y + h / 2, 3);
}
