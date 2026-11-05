#include "TopologyRules.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
struct PinKey
{
    const Component* component;
    PinType type;
    std::size_t index;
    bool operator==(const PinKey&) const = default;
};

struct PinKeyHash
{
    std::size_t operator()(const PinKey& key) const
    {
        const auto address = std::hash<const Component*>{}(key.component);
        return address ^ (key.index * 131u) ^ (static_cast<std::size_t>(key.type) << 1u);
    }
};

PinKey Key(const PinReference& pin)
{
    return {pin.component, pin.type, pin.index};
}

bool IsDirectedWire(const WireConnection& connection)
{
    return connection.output.type == PinType::Output && connection.input.type == PinType::Input;
}

// Starting at the prospective destination input, follow only immediate internal
// dependencies and existing wires. Reaching the source output closes a cycle.
bool WouldCreateCycle(const std::vector<WireConnection>& connections,
    const PinReference& output, const PinReference& input)
{
    std::vector<PinReference> pending{input};
    std::unordered_set<PinKey, PinKeyHash> visited;
    while (!pending.empty())
    {
        const PinReference current = pending.back();
        pending.pop_back();
        if (current == output) return true;
        if (!visited.insert(Key(current)).second) continue;
        if (current.type == PinType::Input)
        {
            for (std::size_t pin = 0; pin < current.component->outputs.size(); ++pin)
                if (current.component->HasCombinationalPath(current.index, pin))
                    pending.push_back({current.component, PinType::Output, pin});
        }
        else
        {
            for (const auto& connection : connections)
                if (connection.output == current) pending.push_back(connection.input);
        }
    }
    return false;
}
}

ConnectionResult TopologyRules::CheckConnection(const std::vector<WireConnection>& connections,
    PinReference first, PinReference second)
{
    if (!first.IsValid() || !second.IsValid()) return ConnectionResult::InvalidPin;
    if (first.type == second.type) return ConnectionResult::SameDirection;
    if (first.type == PinType::Input) std::swap(first, second);
    for (const auto& connection : connections)
    {
        // Callers must keep live components until removing their connections.
        if (!IsDirectedWire(connection) || !connection.output.IsValid() || !connection.input.IsValid())
            return ConnectionResult::InvalidPin;
        if (connection.output == first && connection.input == second) return ConnectionResult::Duplicate;
        if (connection.input == second) return ConnectionResult::InputOccupied;
    }
    if (WouldCreateCycle(connections, first, second)) return ConnectionResult::Cycle;
    return ConnectionResult::Added;
}

ConnectionResult TopologyRules::Validate(const std::vector<Component*>& components,
    const std::vector<WireConnection>& connections)
{
    std::unordered_map<const Component*, std::size_t> offsets;
    std::size_t pinCount = 0;
    for (const auto* component : components)
    {
        if (!component || offsets.contains(component)) continue;
        offsets.emplace(component, pinCount);
        pinCount += component->inputs.size() + component->outputs.size();
    }
    auto node = [&offsets](const PinReference& pin) {
        return offsets.at(pin.component) + pin.index +
            (pin.type == PinType::Output ? pin.component->inputs.size() : 0);
    };
    std::vector<std::vector<std::size_t>> outgoing(pinCount);
    std::vector<std::size_t> indegree(pinCount, 0);
    for (const auto& [component, offset] : offsets)
    {
        for (std::size_t input = 0; input < component->inputs.size(); ++input)
            for (std::size_t output = 0; output < component->outputs.size(); ++output)
                if (component->HasCombinationalPath(input, output))
                {
                    const auto destination = offset + component->inputs.size() + output;
                    outgoing[offset + input].push_back(destination);
                    ++indegree[destination];
                }
    }
    std::unordered_map<PinKey, PinKey, PinKeyHash> drivers;
    for (const auto& connection : connections)
    {
        // Check membership before dereferencing: input files and retained old
        // results must never make us touch a no-longer-owned component pointer.
        if (!offsets.contains(connection.output.component) || !offsets.contains(connection.input.component) ||
            !IsDirectedWire(connection) || !connection.output.IsValid() || !connection.input.IsValid())
            return ConnectionResult::InvalidPin;
        const auto [existing, inserted] = drivers.emplace(Key(connection.input), Key(connection.output));
        if (!inserted)
            return existing->second == Key(connection.output) ? ConnectionResult::Duplicate :
                ConnectionResult::InputOccupied;
        const auto destination = node(connection.input);
        outgoing[node(connection.output)].push_back(destination);
        ++indegree[destination];
    }
    std::queue<std::size_t> ready;
    for (std::size_t pin = 0; pin < pinCount; ++pin)
        if (indegree[pin] == 0) ready.push(pin);
    std::size_t visited = 0;
    while (!ready.empty())
    {
        const auto current = ready.front();
        ready.pop();
        ++visited;
        for (const auto target : outgoing[current])
            if (--indegree[target] == 0) ready.push(target);
    }
    return visited == pinCount ? ConnectionResult::Added : ConnectionResult::Cycle;
}

bool TopologyRules::EvaluationOrder(const std::vector<Component*>& components,
    const std::vector<WireConnection>& connections, std::vector<Component*>& order)
{
    if (Validate(components, connections) != ConnectionResult::Added) return false;
    std::vector<Component*> unique;
    std::unordered_map<Component*, std::size_t> indices;
    for (auto* component : components)
    {
        if (!component || indices.contains(component)) continue;
        for (std::size_t input = 0; input < component->inputs.size(); ++input)
            for (std::size_t output = 0; output < component->outputs.size(); ++output)
                if (!component->HasCombinationalPath(input, output)) return false;
        indices.emplace(component, unique.size());
        unique.push_back(component);
    }
    std::vector<std::vector<std::size_t>> outgoing(unique.size());
    std::vector<std::size_t> indegree(unique.size(), 0);
    for (const auto& connection : connections)
    {
        const auto target = indices.at(connection.input.component);
        outgoing[indices.at(connection.output.component)].push_back(target);
        ++indegree[target];
    }
    std::queue<std::size_t> ready;
    for (std::size_t index = 0; index < unique.size(); ++index)
        if (indegree[index] == 0) ready.push(index);
    std::vector<Component*> staged;
    staged.reserve(unique.size());
    while (!ready.empty())
    {
        const auto current = ready.front();
        ready.pop();
        staged.push_back(unique[current]);
        for (const auto target : outgoing[current])
            if (--indegree[target] == 0) ready.push(target);
    }
    if (staged.size() != unique.size()) return false;
    order = std::move(staged);
    return true;
}
