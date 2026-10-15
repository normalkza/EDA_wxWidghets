#include "Circuit.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <stdexcept>

bool PinReference::IsValid() const
{
    if (!component) return false;
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
    if (!first.IsValid() || !second.IsValid()) return ConnectionResult::InvalidPin;
    if (first.type == second.type) return ConnectionResult::SameDirection;
    if (first.type == PinType::Input) std::swap(first, second);
    for (const auto& connection : m_connections)
    {
        if (connection.output == first && connection.input == second) return ConnectionResult::Duplicate;
        if (connection.input == second) return ConnectionResult::InputOccupied;
    }
    if (WouldCreateCycle(first.component, second.component)) return ConnectionResult::Cycle;
    m_connections.push_back({ first, second });
    return ConnectionResult::Added;
}

bool Circuit::WouldCreateCycle(Component* output, Component* input) const
{
    std::vector<Component*> pending{ input };
    std::unordered_set<Component*> visited;
    while (!pending.empty())
    {
        Component* current = pending.back();
        pending.pop_back();
        if (current == output) return true;
        if (!visited.insert(current).second) continue;
        for (const auto& connection : m_connections)
            if (connection.output.component == current) pending.push_back(connection.input.component);
    }
    return false;
}

void Circuit::RemoveConnection(std::size_t index)
{
    if (index < m_connections.size()) m_connections.erase(m_connections.begin() + index);
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
    std::unordered_map<Component*, std::size_t> nodes;
    for (auto* component : components)
        if (component) nodes.emplace(component, nodes.size());
    // 先核对节点归属，避免访问已不在画布中的元件。
    std::erase_if(m_connections, [&nodes](const WireConnection& connection) {
        return !nodes.contains(connection.output.component) || !nodes.contains(connection.input.component) ||
            !connection.output.IsValid() || !connection.input.IsValid();
    });
    std::vector<int> indegree(nodes.size(), 0);
    std::vector<std::vector<std::size_t>> outgoing(nodes.size());
    for (std::size_t i = 0; i < m_connections.size(); ++i)
    {
        const auto& connection = m_connections[i];
        ++indegree[nodes.at(connection.input.component)];
        outgoing[nodes.at(connection.output.component)].push_back(i);
    }
    std::queue<Component*> ready;
    for (const auto& [component, index] : nodes)
    {
        for (auto& pin : component->inputs) pin.value = LogicValue::Low;
        if (indegree[index] == 0) ready.push(component);
    }
    std::size_t evaluated = 0;
    while (!ready.empty())
    {
        Component* component = ready.front();
        ready.pop();
        component->Evaluate();
        ++evaluated;
        for (const auto wireIndex : outgoing[nodes.at(component)])
        {
            const auto& connection = m_connections[wireIndex];
            connection.input.Get().value = connection.output.Get().value;
            if (--indegree[nodes.at(connection.input.component)] == 0)
                ready.push(connection.input.component);
        }
    }
    return evaluated == nodes.size();
}
