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
    Component* CreateComponent(const wxString& type, int inputCount = 2)
    {
        const bool variable = type == "AND" || type == "OR" || type == "NAND" ||
            type == "NOR" || type == "XOR";
        if (variable && (inputCount < MultiInputGate::MinInputCount ||
            inputCount > MultiInputGate::MaxInputCount)) return nullptr;
        if (type == "AND") return new AndGate(inputCount);
        if (type == "OR") return new OrGate(inputCount);
        if (type == "NOT") return new NotGate();
        if (type == "NAND") return new NandGate(inputCount);
        if (type == "NOR") return new NorGate(inputCount);
        if (type == "XOR") return new XorGate(inputCount);
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
    SetBackgroundColour(wxColour(251, 252, 254));
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
    const wxPoint mouse = event.GetPosition();
    if (g_selectedType == "WIRE")
    {
        HandleWireClick(mouse);
        return;
    }
    m_selectedWireIndex = -1;
    if (g_selectedType == "DELETE")
    {
        DeleteAtPoint(mouse);
        return;
    }
    if (g_selectedType.IsEmpty() && HitTest(mouse.x, mouse.y) < 0)
    {
        const int wire = HitTestWire(mouse);
        if (wire >= 0) { SelectWire(wire); return; }
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
            wxRect(pc.x, pc.y, m_portSize, m_portSize).Contains(mouse);
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

    Component* comp = CreateComponent(type, m_defaultGateInputCount);

    if (comp)
    {
        RecordUndo();
        m_components.push_back({ comp, x, y, NextComponentId() });
        MarkModified();
        m_selectedIndex = static_cast<int>(m_components.size()) - 1;
        RecomputeCircuit();
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
    if (componentIndex < 0 && textIndex < 0)
    {
        DeleteWire(HitTestWire(point));
        return;
    }
    DeleteItem(componentIndex, textIndex);
}

void Canvas::DeleteItem(int componentIndex, int textIndex)
{
    if (componentIndex < 0 && textIndex < 0) return;
    RecordUndo();
    m_selectedIndex = -1;
    m_selectedTextIndex = -1;
    m_selectedWireIndex = -1;
    CancelPendingWire();
    if (componentIndex >= 0)
    {
        m_circuit.RemoveComponent(m_components[componentIndex].comp);
        delete m_components[componentIndex].comp;
        m_components.erase(m_components.begin() + componentIndex);
    }
    else if (textIndex >= 0)
    {
        auto* editor = m_textBoxes[textIndex].editor;
        m_textBoxes.erase(m_textBoxes.begin() + textIndex);
        editor->Destroy();
    }
    MarkModified();
    RecomputeCircuit();
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
            wxRect(pc.x, pc.y, m_portSize, m_portSize).Contains(mouse))
        {
            RecordUndo();
            auto& value = pc.comp->outputs[0].value;
            value = value == LogicValue::Low ? LogicValue::High : LogicValue::Low;
            MarkModified();
            RecomputeCircuit();
        }
    }
    CancelMouseInteraction();
    event.Skip();
}

void Canvas::OnMouseMove(wxMouseEvent& event)
{
    if (g_selectedType == "WIRE")
    {
        const auto pin = HitTestPin(event.GetPosition());
        SetCursor(wxCursor(pin ? wxCURSOR_HAND : wxCURSOR_CROSS));
        if (m_pendingWire)
        {
            m_wirePreview = pin ? GetPinPosition(*pin) : event.GetPosition();
            Refresh(false);
        }
        return;
    }
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
    if (!m_dragUndoDocument) m_dragUndoDocument = ExportDocument();
    const wxRect oldRect = GetComponentUpdateRect(pc);
    pc.x = x;
    pc.y = y;
    MarkModified();
    wxRect dirty = oldRect.Union(GetComponentUpdateRect(pc));
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
    if (restoreText) CancelPendingWire();
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
        RefreshModifiedState();
    }
    if (m_dragUndoDocument)
    {
        const auto& before = *m_dragUndoDocument;
        // 一次拖动只保存一个快照；拖回原位时不创建撤销操作。
        if (m_pressedIndex >= 0 && m_pressedIndex < static_cast<int>(m_components.size()) &&
            (before.components[m_pressedIndex].x != m_components[m_pressedIndex].x ||
                before.components[m_pressedIndex].y != m_components[m_pressedIndex].y))
            RecordUndo(std::move(*m_dragUndoDocument));
        m_dragUndoDocument.reset();
        RefreshModifiedState();
    }
    if (m_textTransformUndoDocument)
    {
        if (!restoreText && m_textTransformIndex >= 0 &&
            m_textBoxes[m_textTransformIndex].rect != m_textTransformRect)
            RecordUndo(std::move(*m_textTransformUndoDocument));
        m_textTransformUndoDocument.reset();
        RefreshModifiedState();
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
    else if (event.ControlDown() && !event.AltDown() && !event.ShiftDown() &&
        (event.GetKeyCode() == 'Z' || event.GetKeyCode() == 'D'))
        HandleEditCommand(event.GetKeyCode() == 'Z' ? wxID_UNDO : wxID_DELETE);
    else if (IsEditingText())
        event.Skip(); // 编辑文字时，交给原生控件处理剪贴板和退格。
    else if (event.ControlDown() && !event.AltDown() && !event.ShiftDown() &&
        (event.GetKeyCode() == 'C' || event.GetKeyCode() == 'V' || event.GetKeyCode() == 'X'))
    {
        const int command = event.GetKeyCode() == 'C' ? wxID_COPY :
            (event.GetKeyCode() == 'V' ? wxID_PASTE : wxID_CUT);
        HandleEditCommand(command);
    }
    else if ((event.GetKeyCode() == WXK_BACK || event.GetKeyCode() == WXK_DELETE) && !event.HasModifiers())
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
        else if (command == wxID_UNDO) UndoText(*editor);
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
    if (command == wxID_UNDO) return Undo();
    return false;
}

bool Canvas::DeleteSelection()
{
    if (m_selectedWireIndex >= 0)
    {
        CancelMouseInteraction(true);
        DeleteWire(m_selectedWireIndex);
        return true;
    }
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
        if (inputs.size() > MultiInputGate::MaxInputCount) return false;
        component.reset(CreateComponent(wxString::FromUTF8(type), static_cast<int>(inputs.size())));
        if (!component || inputs.size() != component->inputs.size() ||
            outputs.size() != component->outputs.size() ||
            inputs.find_first_not_of("01") != std::string::npos ||
            outputs.find_first_not_of("01") != std::string::npos) return false;
        for (std::size_t i = 0; i < inputs.size(); ++i)
            component->inputs[i].value = inputs[i] == '1' ? LogicValue::High : LogicValue::Low;
        for (std::size_t i = 0; i < outputs.size(); ++i)
            component->outputs[i].value = outputs[i] == '1' ? LogicValue::High : LogicValue::Low;
        const wxRect bounds = GetComponentRect({ component.get(), 0, 0 });
        width = bounds.GetRight() + 1;
        height = bounds.GetBottom() + 1;
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
    const int minX = component ? -GetComponentRect({ component.get(), 0, 0 }).x : 0;
    x = std::clamp(x + offset, minX, std::max(minX, size.x - width));
    y = std::clamp(y + offset, 0, std::max(0, size.y - height));
    CancelMouseInteraction(true);
    if (component)
    {
        x = std::max(SnapToGrid(x), ((minX + m_gridSize - 1) / m_gridSize) * m_gridSize);
        y = SnapToGrid(y);
        RecordUndo();
        m_components.push_back({ component.get(), x, y, NextComponentId() });
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
        box.lastValue = box.editor->GetValue();
        // 粘贴的初始内容属于同一次对象粘贴，不另外生成一次空文本框的撤销。
        m_textEditUndoDocument.reset();
        box.scrollY = scrollY;
    }
    m_selectedWireIndex = -1;
    MarkModified();
    RecomputeCircuit();
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
    auto editorOwner = std::make_unique<wxTextCtrl>(this, wxID_ANY, wxEmptyString,
        wxPoint(-32760, -32760), rect.GetSize(),
        wxTE_MULTILINE | wxTE_DONTWRAP | wxTE_NO_VSCROLL | wxBORDER_NONE);
    auto* editor = editorOwner.get();
    editor->SetFont(wxFontInfo(m_defaultTextPointSize).Family(wxFONTFAMILY_SWISS));
    const int index = static_cast<int>(m_textBoxes.size());
    RecordUndo();
    m_textBoxes.push_back({ editor, m_defaultTextPointSize, rect });
    editorOwner.release();
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
        if (!m_loadingDocument)
        {
            // 离开编辑后，这段文字已经进入画布历史。新的编辑会话只撤销新输入，
            // 避免原生控件跨越字号/对象操作撤销旧输入而使画布快照发生冲突。
#ifdef __WXMSW__
            ::SendMessage(static_cast<HWND>(editor->GetHandle()), EM_EMPTYUNDOBUFFER, 0, 0);
#endif
            m_textEditUndoDocument = ExportDocument();
        }
        NotifyTextSelection();
        Refresh();
        event.Skip();
    });
    editor->Bind(wxEVT_TEXT, [this, editor](wxCommandEvent& event) {
        // 已移除的文本控件可能仍有排队中的事件，不能改变新文档状态。
        const auto box = std::find_if(m_textBoxes.begin(), m_textBoxes.end(),
            [editor](const TextBox& text) { return text.editor == editor; });
        if (box != m_textBoxes.end() && box->lastValue != editor->GetValue())
        {
            if (!m_loadingDocument && !m_nativeTextUndo && !m_textEditUndoDocument)
            {
                auto before = ExportDocument();
                before.texts[box - m_textBoxes.begin()].text = box->lastValue;
                m_textEditUndoDocument = std::move(before);
            }
            box->lastValue = editor->GetValue();
            MarkModified();
        }
        UpdateTextInput();
        Refresh();
        event.Skip();
    });
    editor->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        FinishTextEditing();
        Refresh();
        event.Skip();
    });
    MarkModified();
    if (m_loadingDocument) return;
    m_selectedTextIndex = index;
    m_selectedIndex = -1;
    NotifyTextSelection();
    editor->SetFocus();
    Refresh();
}

void Canvas::NotifyTextSelection()
{
    if (m_loadingDocument) return;
    if (m_selectedTextIndex >= 0) m_selectedWireIndex = -1;
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
        if (box.pointSize == m_defaultTextPointSize) return;
        RecordUndo();
        box.pointSize = m_defaultTextPointSize;
        MarkModified();
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
    m_selectedWireIndex = -1;
    SetCursor(wxCursor(g_selectedType == "TEXT" || g_selectedType == "WIRE" ? wxCURSOR_CROSS : wxCURSOR_ARROW));
    ReportStatus(wxEmptyString);
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

Canvas::GateGeometry Canvas::GetGateGeometry(const Component& component) const
{
    const auto& type = component.name;
    if (type == "NOT") return { 40, 40, 0, 60, { wxPoint(0, 20) } };
    const bool andShape = type == "AND" || type == "NAND";
    const bool inverted = type == "NAND" || type == "NOR";
    const bool leftPins = type == "NOR" || type == "XOR";
    const int count = static_cast<int>(component.inputs.size());
    // 偶数输入在中线两侧对称排列；引脚和输出都落在网格点上。
    const int height = std::max(andShape ? 80 : 120, (count / 2 + 1) * 2 * m_gridSize);
    GateGeometry geometry{ 80, height, leftPins ? -20 : 0, inverted ? 100 : 80, {} };
    for (int i = 0; i < count; ++i)
    {
        int row = i - count / 2;
        if (count % 2 == 0 && i >= count / 2) ++row;
        geometry.inputs.emplace_back(geometry.left, height / 2 + row * m_gridSize);
    }
    return geometry;
}

wxRect Canvas::GetComponentRect(const PlacedComponent& pc) const
{
    if (!pc.comp) return wxRect();
    if (pc.comp->name == "INPUT" || pc.comp->name == "OUTPUT")
        return wxRect(pc.x - 5, pc.y - 5, m_portSize + 11, m_portSize + 11);
    const auto geometry = GetGateGeometry(*pc.comp);
    // 覆盖本体、全部引脚、反相圆圈及画笔边缘，不再使用旧的固定范围。
    return wxRect(pc.x + geometry.left - 5, pc.y - 5,
        geometry.outputX - geometry.left + 11, geometry.height + 11);
}

int Canvas::GetGateInputCount() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_components.size()) &&
        m_components[m_selectedIndex].comp->SupportsVariableInputs())
        return static_cast<int>(m_components[m_selectedIndex].comp->inputs.size());
    return m_defaultGateInputCount;
}

void Canvas::SetGateInputCount(int count)
{
    if (count < MultiInputGate::MinInputCount || count > MultiInputGate::MaxInputCount) return;
    m_defaultGateInputCount = count;
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_components.size()))
    {
        if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
        return;
    }
    auto& pc = m_components[m_selectedIndex];
    if (!pc.comp->SupportsVariableInputs() || static_cast<int>(pc.comp->inputs.size()) == count) return;
    auto before = ExportDocument();
    if (!pc.comp->SetInputCount(count)) return;
    RecordUndo(std::move(before));
    MarkModified();
    m_selectedWireIndex = -1;
    CancelPendingWire();
    m_circuit.PruneInvalidConnections();
    RecomputeCircuit();
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
    gridDC.SetBackground(wxBrush(GetBackgroundColour()));
    gridDC.Clear();
    // 保留网格缓存；用纵横线恢复方格绘图区。
    for (int x = 0; x < size.GetWidth(); x += m_gridSize)
    {
        gridDC.SetPen(wxPen(x % (m_gridSize * 5) == 0 ? wxColour(198, 211, 228) : m_gridColour));
        gridDC.DrawLine(x, 0, x, size.GetHeight());
    }
    for (int y = 0; y < size.GetHeight(); y += m_gridSize)
    {
        gridDC.SetPen(wxPen(y % (m_gridSize * 5) == 0 ? wxColour(198, 211, 228) : m_gridColour));
        gridDC.DrawLine(0, y, size.GetWidth(), y);
    }
}

void Canvas::DrawCanvas(wxDC& dc, const wxRect& updateRect)
{
    // 网格仅在画布尺寸改变时生成；拖动时直接复制缓存。
    UpdateGridBitmap();
    if (m_gridBitmap.IsOk()) dc.DrawBitmap(m_gridBitmap, 0, 0);
    DrawWires(dc, updateRect);

    // 元件
    for (const auto& pc : m_components)
    {
        if (!pc.comp || !GetComponentRect(pc).Intersects(updateRect)) continue;

        if (pc.comp->name == "INPUT")
            DrawInput(dc, pc.x, pc.y, pc.comp->outputs[0].value);
        else if (pc.comp->name == "OUTPUT")
            DrawOutput(dc, pc.x, pc.y, pc.comp->inputs[0].value);
        else DrawGate(dc, pc);

    }

    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_components.size()))
    {
        dc.SetPen(wxPen(wxColour(53, 120, 229), 2, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(GetComponentRect(m_components[m_selectedIndex]), 6);
    }
    if (m_creatingText)
    {
        dc.SetPen(wxPen(wxColour(53, 120, 229), 1, wxPENSTYLE_DOT));
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
    m_textTransformWasModified = m_modified;
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
    if (!m_textTransformUndoDocument) m_textTransformUndoDocument = ExportDocument();
    box.rect = rect;
    MarkModified(true);
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
    const wxColour colour(53, 120, 229);
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
    dc.SetTextForeground(wxColour(36, 50, 71));
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
                dc.SetPen(wxPen(wxColour(53, 120, 229), 1));
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


// 输入、输出使用同样的方框；连接点贴在框边，不再伸出短导线。
void Canvas::DrawPort(wxDC& dc, int x, int y, LogicValue value, PinType direction)
{
    const bool high = value == LogicValue::High;
    const wxColour terminal = direction == PinType::Output ? wxColour(225, 92, 99) : wxColour(53, 120, 229);
    const wxFont previousFont = dc.GetFont();
    dc.SetPen(wxPen(terminal, 2));
    dc.SetBrush(wxBrush(high ? wxColour(232, 247, 238) : wxColour(255, 255, 255)));
    dc.DrawRoundedRectangle(x, y, m_portSize, m_portSize, 6);
    dc.SetFont(wxFontInfo(14).FaceName("Microsoft YaHei UI").Bold());
    dc.SetTextForeground(high ? wxColour(30, 143, 83) : wxColour(36, 50, 71));
    dc.DrawLabel(high ? wxT("1") : wxT("0"), wxRect(x, y, m_portSize, m_portSize), wxALIGN_CENTER);
    dc.SetFont(previousFont);
    dc.SetPen(wxPen(terminal));
    dc.SetBrush(wxBrush(terminal));
    dc.DrawCircle(direction == PinType::Output ? x + m_portSize : x, y + m_portSize / 2, 3);
}

void Canvas::DrawInput(wxDC& dc, int x, int y, LogicValue value)
{
    DrawPort(dc, x, y, value, PinType::Output);
}

void Canvas::DrawOutput(wxDC& dc, int x, int y, LogicValue value)
{
    DrawPort(dc, x, y, value, PinType::Input);
}

// 门电路的轮廓和引脚使用与点击判定、拖动刷新相同的几何参数。
void Canvas::DrawGate(wxDC& dc, const PlacedComponent& pc)
{
    const auto geometry = GetGateGeometry(*pc.comp);
    const auto& type = pc.comp->name;
    const int x = pc.x, y = pc.y, w = geometry.width, h = geometry.height;
    const int midY = y + h / 2;
    const bool inverted = type == "NOT" || type == "NAND" || type == "NOR";
    const bool andShape = type == "AND" || type == "NAND";
    const int curveLeft = (type == "NOR" || type == "XOR") ? -10 : 0;
    dc.SetPen(wxPen(wxColour(36, 50, 71), 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    if (type == "NOT")
    {
        dc.DrawLine(x, y, x, y + h);
        dc.DrawLine(x, y, x + w, midY);
        dc.DrawLine(x, y + h, x + w, midY);
    }
    else if (andShape)
    {
        dc.DrawLine(x, y, x, y + h);
        dc.DrawLine(x, y, x + w / 2, y);
        dc.DrawLine(x, y + h, x + w / 2, y + h);
        dc.DrawEllipticArc(x, y, w, h, 270, 90);
    }
    else
    {
        constexpr int segments = 80;
        constexpr double pi = 3.141592653589793;
        auto drawLeft = [&](int left) {
            std::array<wxPoint, segments + 1> points;
            for (int i = 0; i <= segments; ++i)
            {
                const double t = static_cast<double>(i) / segments;
                points[i] = wxPoint(x + left + static_cast<int>(std::lround(10 * std::sin(t * pi))),
                    y + static_cast<int>(std::lround(t * h)));
            }
            dc.DrawLines(static_cast<int>(points.size()), points.data());
        };
        drawLeft(curveLeft);
        if (type == "XOR") drawLeft(curveLeft - 8);
        for (const bool lower : { false, true })
        {
            std::array<wxPoint, segments + 1> points;
            const double startY = lower ? y + h : y;
            const double controlY = lower ? y + h - 1 : y + 1;
            for (int i = 0; i <= segments; ++i)
            {
                const double t = static_cast<double>(i) / segments, mt = 1 - t;
                points[i] = wxPoint(static_cast<int>(std::lround(mt * mt * (x + curveLeft) +
                    2 * mt * t * (x + w * 0.65) + t * t * (x + w))),
                    static_cast<int>(std::lround(mt * mt * startY +
                    2 * mt * t * controlY + t * t * midY)));
            }
            dc.DrawLines(static_cast<int>(points.size()), points.data());
        }
        // 引线连接门本体的内弧，异或门仍保留左侧额外弧线。
        for (const auto& pin : geometry.inputs)
        {
            const double t = static_cast<double>(pin.y) / h;
            const int endX = x + curveLeft + static_cast<int>(std::lround(10 * std::sin(t * pi)));
            dc.DrawLine(x + pin.x, y + pin.y, endX, y + pin.y);
        }
    }
    if (inverted)
    {
        const int radius = type == "NOT" ? 4 : 6;
        dc.DrawCircle(x + w + radius, midY, radius);
        dc.DrawLine(x + w + 2 * radius, midY, x + geometry.outputX, midY);
    }
    dc.SetBrush(wxBrush(wxColour(53, 120, 229)));
    dc.SetPen(wxPen(wxColour(53, 120, 229), 1));
    for (const auto& pin : geometry.inputs)
        dc.DrawCircle(x + pin.x, y + pin.y, 3);
    dc.SetBrush(wxBrush(wxColour(225, 92, 99)));
    dc.SetPen(wxPen(wxColour(225, 92, 99), 1));
    dc.DrawCircle(x + geometry.outputX, midY, 3);
}
