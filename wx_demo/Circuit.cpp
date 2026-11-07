#include "Circuit.h"
#include "TopologyRules.h"
#include "CircuitLimits.h"
#include "CircuitValidation.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <fstream>


bool PinReference::IsValid() const
{
    if (!component || (type != PinType::Input && type != PinType::Output)) return false;
    return index < (type == PinType::Input ? component->inputs : component->outputs).size();
}

Pin& PinReference::Get() const
{
    if (!IsValid()) throw std::out_of_range("Invalid circuit pin");
    return (type == PinType::Input ? component->inputs : component->outputs)[index];
}

bool PinReference::operator==(const PinReference& other) const
{
    return component == other.component && type == other.type && index == other.index;
}

LogicValue WireConnection::GetValue() const
{
    return output.IsValid() ? output.Get().value : LogicValue::Low;
}

ConnectionResult Circuit::Connect(PinReference first, PinReference second)
{
    PruneInvalidConnections();
    const auto result = ValidateConnection(first, second);
    if (result != ConnectionResult::Added) return result;
    if (first.type == PinType::Input) std::swap(first, second);
    m_connections.push_back({ first, second });
    return ConnectionResult::Added;
}

ConnectionResult Circuit::ValidateConnection(PinReference first, PinReference second) const
{
    return TopologyRules::CheckConnection(m_connections, first, second);
}

ConnectionResult Circuit::ReplaceConnections(const std::vector<Component*>& components,
    const std::vector<WireConnection>& connections)
{
    const auto result = TopologyRules::Validate(components, connections);
    if (result != ConnectionResult::Added) return result;
    // Stage the copy too, so an allocation failure cannot partially replace the
    // old wires. Geometry validation belongs to the document/import boundary.
    auto replacement = connections;
    m_connections.swap(replacement);
    return ConnectionResult::Added;
}

ConnectionResult Circuit::ValidateTopology(const std::vector<Component*>& components) const
{
    return TopologyRules::Validate(components, m_connections);
}

void Circuit::RemoveConnection(std::size_t index)
{
    if (index < m_connections.size()) m_connections.erase(m_connections.begin() + index);
}

bool Circuit::SetConnectionRoute(std::size_t index, const std::vector<wxPoint>& route)
{
    wxString error;
    if (index >= m_connections.size() || !CircuitValidation::ValidateRoute(route, error)) return false;
    std::size_t total = route.size();
    for (std::size_t i = 0; i < m_connections.size(); ++i)
        if (i != index) total += m_connections[i].route.size();
    if (total > CircuitLimits::MaxTotalRoutePoints) return false;
    m_connections[index].route = route;
    return true;
}

void Circuit::RemoveComponent(const Component* component)
{
    std::erase_if(m_connections, [component](const WireConnection& connection) {
        return connection.output.component == component || connection.input.component == component;
    });
}

void Circuit::PruneInvalidConnections()
{
    std::erase_if(m_connections, [](const WireConnection& connection) {
        return !connection.output.IsValid() || !connection.input.IsValid();
    });
}

bool Circuit::Evaluate(const std::vector<Component*>& components)
{
    std::unordered_set<Component*> nodes;
    for (auto* component : components)
        if (component) nodes.insert(component);
    // 先核对节点归属，避免访问已不在画布中的元件。
    std::erase_if(m_connections, [&nodes](const WireConnection& connection) {
        return !nodes.contains(connection.output.component) || !nodes.contains(connection.input.component) ||
            !connection.output.IsValid() || !connection.input.IsValid();
    });
    std::vector<Component*> order;
    if (!TopologyRules::EvaluationOrder(components, m_connections, order)) return false;
    std::unordered_map<Component*, std::vector<std::size_t>> outgoing;
    for (std::size_t i = 0; i < m_connections.size(); ++i)
        outgoing[m_connections[i].output.component].push_back(i);
    for (auto* component : order)
        for (auto& pin : component->inputs) pin.value = LogicValue::Low;
    for (auto* component : order)
    {
        component->Evaluate();
        const auto wires = outgoing.find(component);
        if (wires == outgoing.end()) continue;
        for (const auto wireIndex : wires->second)
        {
            const auto& connection = m_connections[wireIndex];
            connection.input.Get().value = connection.output.Get().value;
        }
    }
    return true;
}

bool Circuit::ExportPcb(const std::string& filename, const std::vector<Component*>& components, const std::vector<std::pair<Component*, std::pair<int, int>>>& layout)
{
    std::ofstream out(filename);
    if (!out.is_open()) return false;

    // 1. 写入 KiCad PCB 标准文件头
    out << "(kicad_pcb (version 20211014) (generator pcbnew)\n";
    out << "  (general (thickness 1.6))\n";
    out << "  (paper \"A4\")\n";
    out << "  (layers\n";
    out << "    (0 \"F.Cu\" signal)\n";
    out << "    (31 \"B.Cu\" signal)\n";
    out << "    (44 \"Edge.Cuts\" user)\n";
    out << "    (35 \"F.SilkS\" user)\n";
    out << "  )\n";

    // 把画布像素坐标转换为 PCB 毫米坐标 (假设 1像素 = 0.5mm)
    std::unordered_map<Component*, int> compX, compY;
    std::unordered_map<Component*, int> compIndex;

    int index = 1;
    for (const auto& [comp, coords] : layout) {
        compIndex[comp] = index++;
        compX[comp] = static_cast<int>(coords.first * 0.5);
        compY[comp] = static_cast<int>(coords.second * 0.5);
    }

    // 2. 放置元件封装
    for (const auto& [comp, idx] : compIndex) {
        out << "  (footprint \"Package_DIP:DIP-14_W7.62mm\" (layer \"F.Cu\") (at "
            << compX[comp] << " " << compY[comp] << ")\n";
        out << "    (fp_text reference \"U" << idx << "\" (at 0 0) (layer \"F.SilkS\")\n";
        out << "      (effects (font (size 1 1) (thickness 0.15)))\n";
        out << "    )\n";
        out << "    (fp_rect (start -5 -5) (end 5 5) (stroke (width 0.12)) (fill none) (layer \"F.SilkS\"))\n";
        out << "  )\n";
    }

    // 3. 生成飞线
    int netId = 1;
    for (const auto& conn : m_connections) {
        if (!conn.output.component || !conn.input.component) continue;
        if (compX.find(conn.output.component) == compX.end()) continue;

        double x1 = compX[conn.output.component] + 5;
        double y1 = compY[conn.output.component];
        double x2 = compX[conn.input.component] - 5;
        double y2 = compY[conn.input.component];

        out << "  (segment (start " << x1 << " " << y1 << ") (end " << x2 << " " << y2
            << ") (width 0.25) (layer \"F.Cu\") (net " << netId << "))\n";
        netId++;
    }

    out << "  (gr_rect (start 0 0) (end 300 300) (stroke (width 0.15)) (fill none) (layer \"Edge.Cuts\"))\n";
    out << ")\n";
    out.close();
    return true;
}
