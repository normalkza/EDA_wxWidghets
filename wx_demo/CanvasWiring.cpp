#include "Canvas.h"
#include "DigitalSimulator.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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
        if (pc.comp->name == "INPUT") return ComponentToWorld(pc, wxPoint(m_portSize, m_portSize / 2));
        if (pc.comp->name == "OUTPUT") return ComponentToWorld(pc, wxPoint(0, m_portSize / 2));
        const auto geometry = GetGateGeometry(*pc.comp);
        const wxPoint offset = pin.type == PinType::Input ? geometry.inputs.at(pin.index) :
            (geometry.outputs.empty() ? wxPoint(geometry.outputX, geometry.height / 2) : geometry.outputs.at(pin.index));
        return ComponentToWorld(pc, offset);
    }
    return wxPoint();
}

wxPoint Canvas::GetPinDirection(const PinReference& pin) const
{
    const wxPoint direction = pin.type == PinType::Output ? wxPoint(1, 0) : wxPoint(-1, 0);
    for (const auto& pc : m_components)
        if (pc.comp == pin.component && pin.IsValid())
            return ComponentToWorld(pc, direction) - ComponentToWorld(pc, wxPoint());
    return direction;
}

std::optional<PinReference> Canvas::HitTestPin(const wxPoint& point) const
{
    const int tolerance = FromDIP(8);
    for (auto pc = m_components.rbegin(); pc != m_components.rend(); ++pc)
    {
        // 端口只有一个连接点，连线工具可点击整个方框；门电路仍逐引脚判定。
        if (pc->comp->name == "INPUT" || pc->comp->name == "OUTPUT")
        {
            if (wxRect(0, 0, m_portSize + 1, m_portSize + 1).Contains(WorldToComponent(*pc, point)))
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
    const wxPoint output = GetPinPosition(connection.output), input = GetPinPosition(connection.input);
    const wxPoint outDirection = GetPinDirection(connection.output), inDirection = GetPinDirection(connection.input);
    return connection.route.empty() ? Wire::Route(output, input, m_gridSize, outDirection, inDirection) :
        Wire::Reanchor(connection.route, output, input, m_gridSize, outDirection, inDirection);
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

bool Canvas::HasSelectedWire() const
{
    return m_selectedWireIndex >= 0 &&
        m_selectedWireIndex < static_cast<int>(m_circuit.GetConnections().size());
}

bool Canvas::CanResetWireRoute() const
{
    return HasSelectedWire() && !m_circuit.GetConnections()[m_selectedWireIndex].route.empty();
}

bool Canvas::ResetSelectedWireRoute()
{
    if (IsDraggingWire()) FinishWireDrag(true);
    if (!CanResetWireRoute()) return false;
    auto before = ExportDocument();
    if (!m_circuit.SetConnectionRoute(static_cast<std::size_t>(m_selectedWireIndex), {})) return false;
    RecordUndo(std::move(before));
    MarkModified();
    if (m_wireSelectionCallback)
        m_wireSelectionCallback(&m_circuit.GetConnections()[m_selectedWireIndex]);
    ReportStatus(wxT("已恢复自动走线"));
    Refresh(false);
    return true;
}

bool Canvas::BeginWireDrag(const wxPoint& point)
{
    if (IsDraggingWire() || m_pendingWire) return false;
    const int index = HitTestWire(point);
    if (index < 0) return false;
    const auto& connection = m_circuit.GetConnections()[index];
    const auto path = GetWirePath(connection);
    const int segment = Wire::HitTestSegment(path, point, FromDIP(6));
    if (segment < 0) return false;
    FinishTextEditing();
    m_wireDragUndoDocument = ExportDocument();
    m_wireDragIndex = index;
    m_wireDragSegment = segment;
    m_wireDragStart = point;
    m_wireDragPath = path;
    m_wireDragOriginalRoute = connection.route;
    m_wireDragMoved = false;
    SelectWire(index);
    const bool horizontal = path[segment].y == path[segment + 1].y;
    SetCursor(wxCursor(horizontal ? wxCURSOR_SIZENS : wxCURSOR_SIZEWE));
    if (!HasCapture()) CaptureMouse();
    return true;
}

void Canvas::UpdateWireDrag(const wxPoint& point)
{
    if (!IsDraggingWire() || m_wireDragIndex >= static_cast<int>(m_circuit.GetConnections().size())) return;
    auto path = Wire::MoveSegment(m_wireDragPath, static_cast<std::size_t>(m_wireDragSegment),
        point - m_wireDragStart, m_snapEnabled ? m_gridSize : 1);
    const auto& connection = m_circuit.GetConnections()[m_wireDragIndex];
    path = Wire::Reanchor(path, GetPinPosition(connection.output), GetPinPosition(connection.input),
        m_gridSize, GetPinDirection(connection.output), GetPinDirection(connection.input));
    const auto& route = path == m_wireDragPath ? m_wireDragOriginalRoute : path;
    if (route == connection.route) return;
    const auto previousRoute = connection.route;
    if (!m_circuit.SetConnectionRoute(static_cast<std::size_t>(m_wireDragIndex), route)) return;
    if (!CanPersistWireGeometry())
    {
        m_circuit.SetConnectionRoute(static_cast<std::size_t>(m_wireDragIndex), previousRoute);
        ReportStatus(wxT("连线路径点已达到上限，无法调整导线"));
        return;
    }
    m_wireDragMoved = route != m_wireDragOriginalRoute;
    Refresh(false);
}

void Canvas::FinishWireDrag(bool cancel)
{
    if (!IsDraggingWire()) return;
    const int index = m_wireDragIndex;
    const bool valid = index < static_cast<int>(m_circuit.GetConnections().size());
    const bool moved = m_wireDragMoved;
    m_wireDragIndex = m_wireDragSegment = -1;
    if (cancel && valid)
        m_circuit.SetConnectionRoute(static_cast<std::size_t>(index), m_wireDragOriginalRoute);
    if (!cancel && valid && moved && m_wireDragUndoDocument)
    {
        RecordUndo(std::move(*m_wireDragUndoDocument));
        MarkModified();
        ReportStatus(wxT("导线布局已更新"));
    }
    else if (cancel && moved) ReportStatus(wxT("已取消导线调整"));
    m_wireDragUndoDocument.reset();
    m_wireDragPath.clear();
    m_wireDragOriginalRoute.clear();
    m_wireDragMoved = false;
    if (HasCapture()) ReleaseMouse();
    SetCursor(wxCursor(wxCURSOR_ARROW));
    if (HasSelectedWire() && m_wireSelectionCallback)
        m_wireSelectionCallback(&m_circuit.GetConnections()[m_selectedWireIndex]);
    Refresh(false);
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
        if (m_wireCreatedCallback)
        {
            CircuitDocument document = ExportDocument();

            if (!document.wires.empty())
            {
                m_wireCreatedCallback(document.wires.back());
            }
        }
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
    if (IsDraggingWire()) FinishWireDrag(true);
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

std::unique_ptr<SimulationSnapshot> Canvas::CreateSimulationSnapshot(wxString& error) const
{
    auto snapshot = std::make_unique<SimulationSnapshot>();
    if (!snapshot->Compile(ExportDocument(), m_documentRevision, error)) return nullptr;
    return snapshot;
}

bool Canvas::ApplySimulationResult(const SimulationResult& result)
{
    if (result.revision != m_documentRevision) return false;
    const auto document = ExportDocument();
    std::unordered_map<int, Component*> byId;
    std::size_t pinCount = 0;
    for (std::size_t index = 0; index < m_components.size(); ++index)
    {
        auto* component = m_components[index].comp;
        byId.emplace(document.components[index].id, component);
        pinCount += component->inputs.size() + component->outputs.size();
    }
    if (result.values.size() != pinCount) return false;
    std::unordered_set<Pin*> seen;
    std::vector<std::pair<Pin*, LogicValue>> updates;
    updates.reserve(pinCount);
    // Validate the entire packet first. A missing/duplicate/unknown pin cannot
    // leave the view showing half of a result from an invalid worker message.
    for (const auto& value : result.values)
    {
        const auto component = byId.find(value.componentId);
        if (component == byId.end() ||
            (value.type != PinType::Input && value.type != PinType::Output) ||
            (value.value != LogicValue::Low && value.value != LogicValue::High)) return false;
        auto& pins = value.type == PinType::Input ? component->second->inputs : component->second->outputs;
        if (value.index >= pins.size() || !seen.insert(&pins[value.index]).second) return false;
        updates.emplace_back(&pins[value.index], value.value);
    }
    // Freeze any legacy initial value before applying runtime display values.
    for (std::size_t index = 0; index < m_components.size(); ++index)
        if (m_components[index].comp->name == "INPUT")
            m_components[index].initialHigh = document.components[index].high;
    for (const auto& [pin, value] : updates) pin->value = value;
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_components.size()) && m_selectionCallback)
    {
        const auto& pc = m_components[m_selectedIndex];
        m_selectionCallback(pc.comp, pc.x, pc.y);
    }
    if (m_selectedWireIndex >= 0 && m_selectedWireIndex < static_cast<int>(m_circuit.GetConnections().size()) &&
        m_wireSelectionCallback)
        m_wireSelectionCallback(&m_circuit.GetConnections()[m_selectedWireIndex]);
    Refresh(false);
    return true;
}

bool Canvas::RecomputeCircuit()
{
    m_circuit.PruneInvalidConnections();
    wxString error;
    const auto document = ExportDocument();
    if (std::any_of(document.components.begin(), document.components.end(),
        [](const auto& data) { return data.type == "DFF"; }))
    {
        DigitalSimulator simulator;
        if (!simulator.Compile(document, m_documentRevision, error))
        {
            ReportStatus(error);
            return false;
        }
        return ApplySimulationResult(simulator.GetResult());
    }
    auto snapshot = CreateSimulationSnapshot(error);
    if (!snapshot)
    {
        ReportStatus(error);
        return false;
    }
    return ApplySimulationResult(snapshot->GetResult());
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
        const auto target = HitTestPin(m_wirePreview);
        const wxPoint outputDirection = m_pendingWire->type == PinType::Output ? GetPinDirection(*m_pendingWire) :
            (target && target->type == PinType::Output ? GetPinDirection(*target) : wxPoint(1, 0));
        const wxPoint inputDirection = m_pendingWire->type == PinType::Input ? GetPinDirection(*m_pendingWire) :
            (target && target->type == PinType::Input ? GetPinDirection(*target) : wxPoint(-1, 0));
        const auto path = m_pendingWire->type == PinType::Output ?
            Wire::Route(start, m_wirePreview, m_gridSize, outputDirection, inputDirection) :
            Wire::Route(m_wirePreview, start, m_gridSize, outputDirection, inputDirection);
        Wire::Draw(dc, path, LogicValue::Low, false, true);
    }
}
