#include "SimulationSnapshot.h"
#include "CircuitValidation.h"
#include "ComponentRegistry.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

SimulationSnapshot::SimulationSnapshot(SimulationSnapshot&& other) noexcept
{
    Swap(other);
}

SimulationSnapshot& SimulationSnapshot::operator=(SimulationSnapshot&& other) noexcept
{
    if (this != &other)
    {
        SimulationSnapshot replacement(std::move(other));
        Swap(replacement);
    }
    return *this;
}

void SimulationSnapshot::Swap(SimulationSnapshot& other) noexcept
{
    m_components.swap(other.m_components);
    m_componentIds.swap(other.m_componentIds);
    m_nodes.swap(other.m_nodes);
    m_byId.swap(other.m_byId);
    std::swap(m_circuit, other.m_circuit);
    m_nets.swap(other.m_nets);
    std::swap(m_revision, other.m_revision);
    std::swap(m_compiled, other.m_compiled);
}

bool SimulationSnapshot::Compile(const CircuitDocument& document, std::uint64_t revision, wxString& error)
{
    error.clear();
    if (!CircuitValidation::Validate(document, error)) return false;

    SimulationSnapshot candidate;
    try
    {
        // Persistent IDs make result order independent of placement order.
        std::vector<const CircuitComponentData*> ordered;
        ordered.reserve(document.components.size());
        for (const auto& data : document.components) ordered.push_back(&data);
        std::sort(ordered.begin(), ordered.end(), [](const auto* first, const auto* second) {
            return first->id < second->id;
        });

        candidate.m_components.reserve(ordered.size());
        candidate.m_componentIds.reserve(ordered.size());
        candidate.m_nodes.reserve(ordered.size());
        std::vector<NetPin> pins;
        for (const auto* data : ordered)
        {
            auto component = ComponentRegistry::Create(data->type, data->inputCount, data->high);
            if (!component) throw std::runtime_error("Unsupported component in simulation snapshot");
            auto* pointer = component.get();
            for (std::size_t index = 0; index < component->inputs.size(); ++index)
                pins.push_back({data->id, PinType::Input, index});
            for (std::size_t index = 0; index < component->outputs.size(); ++index)
                pins.push_back({data->id, PinType::Output, index});
            candidate.m_byId.emplace(data->id, pointer);
            candidate.m_componentIds.push_back(data->id);
            candidate.m_nodes.push_back(pointer);
            candidate.m_components.push_back(std::move(component));
        }

        std::vector<NetLink> links;
        links.reserve(document.wires.size());
        std::vector<WireConnection> connections;
        connections.reserve(document.wires.size());
        for (const auto& wire : document.wires)
        {
            const auto sourceIndex = static_cast<std::size_t>(wire.sourcePin);
            const auto targetIndex = static_cast<std::size_t>(wire.targetPin);
            connections.push_back({
                {candidate.m_byId.at(wire.sourceId), PinType::Output, sourceIndex},
                {candidate.m_byId.at(wire.targetId), PinType::Input, targetIndex}});
            // Routes are design geometry and are deliberately not retained by
            // the solver. The undirected net is independent of that geometry.
            links.push_back({{wire.sourceId, PinType::Output, sourceIndex},
                {wire.targetId, PinType::Input, targetIndex}});
        }
        if (candidate.m_circuit.ReplaceConnections(candidate.m_nodes, connections) != ConnectionResult::Added)
        {
            error = wxS("无法编译仿真快照：连线不符合电路拓扑规则。");
            return false;
        }
        std::string connectivityError;
        if (!ConnectivityBuilder::Build(pins, links, candidate.m_nets, connectivityError))
        {
            error = wxS("无法编译电气网络：") + wxString::FromUTF8(connectivityError);
            return false;
        }
        if (!candidate.m_circuit.Evaluate(candidate.m_nodes))
        {
            error = wxS("无法编译仿真快照：当前组合求值器不支持此电路。");
            return false;
        }
        candidate.m_revision = revision;
        candidate.m_compiled = true;
    }
    catch (const std::exception& exception)
    {
        error = wxS("无法编译仿真快照：") + wxString::FromUTF8(exception.what());
        return false;
    }

    Swap(candidate);
    return true;
}

bool SimulationSnapshot::Evaluate()
{
    return m_compiled && m_circuit.Evaluate(m_nodes);
}

bool SimulationSnapshot::SetInput(int componentId, LogicValue value)
{
    if (!m_compiled || (value != LogicValue::Low && value != LogicValue::High)) return false;
    const auto found = m_byId.find(componentId);
    if (found == m_byId.end()) return false;
    auto* component = found->second;
    if (component->name != "INPUT" || component->outputs.size() != 1) return false;
    component->outputs[0].value = value;
    return true;
}

const Component* SimulationSnapshot::FindComponent(int componentId) const
{
    if (!m_compiled) return nullptr;
    const auto found = m_byId.find(componentId);
    return found == m_byId.end() ? nullptr : found->second;
}

std::optional<LogicValue> SimulationSnapshot::GetValue(int componentId, PinType type, std::size_t index) const
{
    const auto* component = FindComponent(componentId);
    if (!component || (type != PinType::Input && type != PinType::Output)) return std::nullopt;
    const auto& pins = type == PinType::Input ? component->inputs : component->outputs;
    if (index >= pins.size()) return std::nullopt;
    return pins[index].value;
}

SimulationResult SimulationSnapshot::GetResult() const
{
    SimulationResult result;
    result.revision = m_revision;
    if (!m_compiled) return result;
    for (std::size_t index = 0; index < m_components.size(); ++index)
    {
        const auto& component = m_components[index];
        for (std::size_t pin = 0; pin < component->inputs.size(); ++pin)
            result.values.push_back({m_componentIds[index], PinType::Input, pin, component->inputs[pin].value});
        for (std::size_t pin = 0; pin < component->outputs.size(); ++pin)
            result.values.push_back({m_componentIds[index], PinType::Output, pin, component->outputs[pin].value});
    }
    return result;
}
