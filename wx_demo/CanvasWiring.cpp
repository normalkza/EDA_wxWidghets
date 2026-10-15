#include "Canvas.h"
#include <algorithm>

Canvas::~Canvas()
{
    m_dragTimer.Stop();
    m_textTimer.Stop();
    for (const auto& pc : m_components) delete pc.comp;
}

void Canvas::ReportStatus(const wxString& message)
{
    if (m_statusCallback) m_statusCallback(message);
}

wxPoint Canvas::GetPinPosition(const PinReference& pin) const
{
    for (const auto& pc : m_components)
    {
        if (pc.comp != pin.component || !pin.IsValid()) continue;
        if (pc.comp->name == "INPUT") return wxPoint(pc.x + m_portSize, pc.y + m_portSize / 2);
        if (pc.comp->name == "OUTPUT") return wxPoint(pc.x, pc.y + m_portSize / 2);
        const auto geometry = GetGateGeometry(*pc.comp);
        const wxPoint offset = pin.type == PinType::Input ? geometry.inputs.at(pin.index) :
            wxPoint(geometry.outputX, geometry.height / 2);
        return wxPoint(pc.x, pc.y) + offset;
    }
    return wxPoint();
}

std::optional<PinReference> Canvas::HitTestPin(const wxPoint& point) const
{
    const int tolerance = FromDIP(8);
    for (auto pc = m_components.rbegin(); pc != m_components.rend(); ++pc)
    {
        // 端口只有一个连接点，连线工具可点击整个方框；门电路仍逐引脚判定。
        if (pc->comp->name == "INPUT" || pc->comp->name == "OUTPUT")
        {
            if (wxRect(pc->x, pc->y, m_portSize + 1, m_portSize + 1).Contains(point))
                return PinReference{ pc->comp, pc->comp->name == "INPUT" ? PinType::Output : PinType::Input, 0 };
        }
        for (const auto type : { PinType::Input, PinType::Output })
        {
            const auto& pins = type == PinType::Input ? pc->comp->inputs : pc->comp->outputs;
            for (std::size_t i = 0; i < pins.size(); ++i)
            {
                PinReference pin{ pc->comp, type, i };
                const wxPoint delta = point - GetPinPosition(pin);
                if (delta.x * delta.x + delta.y * delta.y <= tolerance * tolerance) return pin;
            }
        }
    }
    return std::nullopt;
}

Wire::Path Canvas::GetWirePath(const WireConnection& connection) const
{
    return Wire::Route(GetPinPosition(connection.output), GetPinPosition(connection.input), m_gridSize);
}

wxRect Canvas::GetComponentUpdateRect(const PlacedComponent& pc) const
{
    wxRect bounds = GetComponentRect(pc);
    for (const auto& connection : m_circuit.GetConnections())
        if (connection.output.component == pc.comp || connection.input.component == pc.comp)
            bounds.Union(Wire::GetBounds(GetWirePath(connection)));
    return bounds;
}

int Canvas::HitTestWire(const wxPoint& point) const
{
    const auto& connections = m_circuit.GetConnections();
    for (int i = static_cast<int>(connections.size()) - 1; i >= 0; --i)
        if (Wire::HitTest(GetWirePath(connections[i]), point, FromDIP(6))) return i;
    return -1;
}

void Canvas::HandleWireClick(const wxPoint& point)
{
    const auto pin = HitTestPin(point);
    if (!pin)
    {
        if (!m_pendingWire)
        {
            const int wire = HitTestWire(point);
            if (wire >= 0) { SelectWire(wire); return; }
        }
        ReportStatus(wxT("未命中连接点"));
        return;
    }
    if (!m_pendingWire)
    {
        m_selectedIndex = m_selectedTextIndex = m_selectedWireIndex = -1;
        m_pendingWire = *pin;
        m_wirePreview = GetPinPosition(*pin);
        if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
        ReportStatus(wxT("已选择连线起点"));
        Refresh(false);
        return;
    }
    if (*m_pendingWire == *pin)
    {
        CancelPendingWire();
        return;
    }
    auto before = ExportDocument();
    const auto result = m_circuit.Connect(*m_pendingWire, *pin);
    switch (result)
    {
    case ConnectionResult::Added:
        RecordUndo(std::move(before));
        m_pendingWire.reset();
        MarkModified();
        RecomputeCircuit();
        SelectWire(static_cast<int>(m_circuit.GetConnections().size()) - 1);
        ReportStatus(wxT("连线完成"));
        break;
    case ConnectionResult::Duplicate:
        CancelPendingWire();
        ReportStatus(wxT("这两个引脚已连接"));
        break;
    case ConnectionResult::SameDirection:
        ReportStatus(wxT("只能连接输出与输入引脚"));
        break;
    case ConnectionResult::InputOccupied:
        ReportStatus(wxT("该输入已有导线"));
        break;
    case ConnectionResult::Cycle:
        ReportStatus(wxT("不支持组合逻辑反馈环路"));
        break;
    default:
        CancelPendingWire();
        ReportStatus(wxT("引脚已失效"));
        break;
    }
}

void Canvas::CancelPendingWire()
{
    if (!m_pendingWire) return;
    m_pendingWire.reset();
    ReportStatus(wxT("已取消连线"));
    Refresh(false);
}

void Canvas::SelectWire(int index)
{
    m_selectedIndex = m_selectedTextIndex = -1;
    m_selectedWireIndex = index;
    if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
    if (m_wireSelectionCallback)
        m_wireSelectionCallback(&m_circuit.GetConnections().at(index));
    Refresh(false);
}

void Canvas::DeleteWire(int index)
{
    if (index < 0 || index >= static_cast<int>(m_circuit.GetConnections().size())) return;
    RecordUndo();
    m_circuit.RemoveConnection(index);
    MarkModified();
    m_selectedWireIndex = -1;
    CancelPendingWire();
    RecomputeCircuit();
    if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
    ReportStatus(wxT("已删除导线并更新电路"));
}

void Canvas::RecomputeCircuit()
{
    std::vector<Component*> components;
    for (const auto& pc : m_components) components.push_back(pc.comp);
    m_circuit.Evaluate(components);
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_components.size()) && m_selectionCallback)
    {
        const auto& pc = m_components[m_selectedIndex];
        m_selectionCallback(pc.comp, pc.x, pc.y);
    }
    if (m_selectedWireIndex >= 0 && m_selectedWireIndex < static_cast<int>(m_circuit.GetConnections().size()) &&
        m_wireSelectionCallback)
        m_wireSelectionCallback(&m_circuit.GetConnections()[m_selectedWireIndex]);
    Refresh(false);
}

void Canvas::DrawWires(wxDC& dc, const wxRect& updateRect)
{
    const auto& connections = m_circuit.GetConnections();
    for (std::size_t i = 0; i < connections.size(); ++i)
    {
        const auto path = GetWirePath(connections[i]);
        if (Wire::GetBounds(path).Intersects(updateRect))
            Wire::Draw(dc, path, connections[i].GetValue(), static_cast<int>(i) == m_selectedWireIndex);
    }
    if (m_pendingWire && m_pendingWire->IsValid())
    {
        const wxPoint start = GetPinPosition(*m_pendingWire);
        const auto path = m_pendingWire->type == PinType::Output ?
            Wire::Route(start, m_wirePreview, m_gridSize) : Wire::Route(m_wirePreview, start, m_gridSize);
        Wire::Draw(dc, path, LogicValue::Low, false, true);
    }
}
