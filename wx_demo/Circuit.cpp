#include "Circuit.h"
#include "TopologyRules.h"
#include "CircuitLimits.h"
#include "CircuitValidation.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

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
