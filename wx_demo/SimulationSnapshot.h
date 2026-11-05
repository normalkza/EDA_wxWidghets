#pragma once

#include "Circuit.h"
#include "CircuitDocument.h"
#include "Connectivity.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

struct SimulationPinValue
{
    int componentId = 0;
    PinType type = PinType::Input;
    std::size_t index = 0;
    LogicValue value = LogicValue::Low;
};

// Results cross the model/view boundary by value. Revision identifies the exact
// design that produced them; no pointer into a canvas or snapshot is exposed.
struct SimulationResult
{
    std::uint64_t revision = 0;
    std::vector<SimulationPinValue> values;
};

class SimulationSnapshot
{
public:
    SimulationSnapshot() = default;
    SimulationSnapshot(const SimulationSnapshot&) = delete;
    SimulationSnapshot& operator=(const SimulationSnapshot&) = delete;
    SimulationSnapshot(SimulationSnapshot&& other) noexcept;
    SimulationSnapshot& operator=(SimulationSnapshot&& other) noexcept;

    // Compile into independent owned objects. On failure this snapshot, its
    // revision and its current signals remain unchanged.
    bool Compile(const CircuitDocument& document, std::uint64_t revision, wxString& error);
    bool Evaluate();
    // Changes the runtime stimulus only. Call Evaluate to propagate it.
    bool SetInput(int componentId, LogicValue value);
    const Component* FindComponent(int componentId) const;
    std::optional<LogicValue> GetValue(int componentId, PinType type, std::size_t index) const;
    SimulationResult GetResult() const;

    bool IsCompiled() const { return m_compiled; }
    std::uint64_t GetRevision() const { return m_revision; }
    // Net IDs are derived for this revision. Persistent probes should identify
    // a network through a stable NetPin rather than retain its generated ID.
    const std::vector<ElectricalNet>& GetConnectivity() const { return m_nets; }

private:
    void Swap(SimulationSnapshot& other) noexcept;

    std::vector<std::unique_ptr<Component>> m_components;
    std::vector<int> m_componentIds;
    std::vector<Component*> m_nodes;
    std::unordered_map<int, Component*> m_byId;
    Circuit m_circuit;
    std::vector<ElectricalNet> m_nets;
    std::uint64_t m_revision = 0;
    bool m_compiled = false;
};
