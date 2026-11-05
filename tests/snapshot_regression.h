#pragma once

#include "SimulationSnapshot.h"
#include <stdexcept>
#include <string>
#include <utility>

namespace SnapshotRegression
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(std::string("simulation snapshot: ") + message);
}

inline CircuitDocument Design()
{
    CircuitDocument document;
    // Intentionally put the output before its dependencies.
    document.components = {{99, "OUTPUT", 300, 50, 1, false},
        {50, "NOT", 200, 50, 1, false}, {7, "INPUT", 50, 50, 0, true}};
    document.wires = {{7, 0, 50, 0}, {50, 0, 99, 0}};
    return document;
}

inline void Run()
{
    SimulationSnapshot snapshot;
    wxString error;
    Require(!snapshot.Evaluate() && !snapshot.SetInput(7, LogicValue::High), "uncompiled state rejects use");
    Require(snapshot.GetResult().values.empty(), "uncompiled snapshot has no signals");
    {
        auto source = Design();
        Require(snapshot.Compile(source, 41, error), "compile independent circuit");
        Require(error.empty() && snapshot.GetRevision() == 41, "compiled revision");
        Require(snapshot.GetValue(99, PinType::Input, 0) == LogicValue::Low, "initial propagation");
        Require(snapshot.SetInput(7, LogicValue::Low) && snapshot.Evaluate(), "runtime stimulus propagates");
        Require(source.components.back().high, "runtime stimulus does not mutate design initial value");
        source.components.clear();
        source.wires.clear();
    }
    Require(snapshot.GetValue(99, PinType::Input, 0) == LogicValue::High, "source design can be destroyed");
    Require(snapshot.SetInput(7, LogicValue::High) && snapshot.Evaluate(), "runtime continues after source destruction");
    Require(snapshot.GetValue(99, PinType::Input, 0) == LogicValue::Low, "owned network still connected");
    Require(!snapshot.SetInput(50, LogicValue::High) && !snapshot.SetInput(123, LogicValue::Low),
        "only external input stimuli are mutable");
    Require(!snapshot.SetInput(7, static_cast<LogicValue>(123)), "reject invalid logic enum");
    Require(!snapshot.GetValue(7, PinType::Input, 0).has_value() &&
        !snapshot.GetValue(7, PinType::Output, 5).has_value(), "reject absent pins");

    const auto savedResult = snapshot.GetResult();
    Require(savedResult.revision == 41 && savedResult.values.size() == 4, "value result carries revision and pins");
    Require(savedResult.values.front().componentId == 7 && savedResult.values.back().componentId == 99,
        "result order uses stable component IDs");
    Require(snapshot.GetConnectivity().size() == 2, "legacy wires compile to independent electrical nets");
    for (const auto& net : snapshot.GetConnectivity())
        Require(net.pins.size() == 2, "net includes both pin endpoints");

    auto invalid = Design();
    invalid.wires.push_back({999, 0, 50, 0});
    const auto* originalInput = snapshot.FindComponent(7);
    Require(!snapshot.Compile(invalid, 42, error), "reject invalid replacement");
    Require(!error.empty() && snapshot.GetRevision() == 41 && snapshot.FindComponent(7) == originalInput,
        "failed compilation retains objects and revision");
    Require(snapshot.GetValue(99, PinType::Input, 0) == LogicValue::Low, "failed compilation retains signals");

    SimulationSnapshot moved(std::move(snapshot));
    Require(moved.FindComponent(7) == originalInput && moved.GetRevision() == 41,
        "move keeps ownership and graph pointers together");
    Require(!snapshot.IsCompiled() && snapshot.GetRevision() == 0 && !snapshot.Evaluate(),
        "moved-from snapshot is empty");
    Require(moved.SetInput(7, LogicValue::Low) && moved.Evaluate(), "moved circuit remains usable");
    Require(savedResult.values.front().value == LogicValue::High, "old result is an immutable value copy");
    Require(moved.GetValue(99, PinType::Input, 0) == LogicValue::High, "move retained connections");

    SimulationSnapshot assigned;
    Require(assigned.Compile(Design(), 100, error), "prepare assignment destination");
    assigned = std::move(moved);
    Require(assigned.FindComponent(7) == originalInput && assigned.Evaluate(), "move assignment preserves graph");
    Require(!moved.IsCompiled() && moved.GetConnectivity().empty(), "move assignment empties source");
    assigned = std::move(assigned);
    Require(assigned.GetRevision() == 41 && assigned.Evaluate(), "self move does not lose circuit");

    CircuitDocument empty;
    Require(assigned.Compile(empty, 43, error) && assigned.Evaluate(), "empty design compiles");
    Require(assigned.GetResult().values.empty() && assigned.GetConnectivity().empty(), "empty design clears old data");
}
}
