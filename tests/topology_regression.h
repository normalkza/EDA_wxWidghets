#pragma once

#include "Circuit.h"
#include "Connectivity.h"
#include "IOComponent.h"
#include "NotGate.h"
#include "TopologyRules.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace topology_regression
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(std::string("topology: ") + message);
}

// A topology-only stand-in, not a DFF implementation. Its D pin crosses a state
// boundary; its asynchronous reset still immediately influences Q.
class StateBoundary final : public Component
{
public:
    StateBoundary() : Component("TEST_STATE")
    {
        inputs.emplace_back("D", PinType::Input);
        inputs.emplace_back("RESET", PinType::Input);
        outputs.emplace_back("Q", PinType::Output);
    }
    void Evaluate() override { ++evaluations; }
    bool HasCombinationalPath(std::size_t input, std::size_t output) const override
    {
        return input == 1 && output == 0;
    }
    int evaluations = 0;
};

inline void TestSharedRules()
{
    NotGate a, b;
    const std::vector<Component*> nodes{&a, &b};
    const WireConnection ab{{&a, PinType::Output, 0}, {&b, PinType::Input, 0}};
    const WireConnection ba{{&b, PinType::Output, 0}, {&a, PinType::Input, 0}};
    Require(TopologyRules::Validate(nodes, {ab}) == ConnectionResult::Added, "valid directed topology");
    Require(TopologyRules::Validate(nodes, {ab, ba}) == ConnectionResult::Cycle, "full validation detects cycle");
    Require(TopologyRules::CheckConnection({ab}, ba.output, ba.input) == ConnectionResult::Cycle,
        "interactive and full cycle rules agree");
    Require(TopologyRules::Validate(nodes, {ab, ab}) == ConnectionResult::Duplicate, "reject duplicate wire");
    const WireConnection competing{{&b, PinType::Output, 0}, {&b, PinType::Input, 0}};
    Require(TopologyRules::Validate(nodes, {ab, competing}) == ConnectionResult::InputOccupied,
        "detect multiple drivers before evaluation");
    Require(TopologyRules::Validate({&a}, {ab}) == ConnectionResult::InvalidPin,
        "reject endpoints outside supplied owner set");
    Require(!PinReference{&a, static_cast<PinType>(99), 0}.IsValid(), "reject unknown pin direction");
    std::vector<Component*> order;
    Require(TopologyRules::EvaluationOrder({&b, &a}, {ab}, order) && order == nodes,
        "evaluation order follows dependencies, not placement order");

    StateBoundary state;
    NotGate inverse;
    Circuit feedback;
    Require(feedback.Connect({&state, PinType::Output, 0}, {&inverse, PinType::Input, 0}) ==
        ConnectionResult::Added, "connect state output");
    Require(feedback.Connect({&inverse, PinType::Output, 0}, {&state, PinType::Input, 0}) ==
        ConnectionResult::Added, "state boundary permits registered feedback");
    const std::vector<Component*> stateNodes{&state, &inverse};
    Require(feedback.ValidateTopology(stateNodes) == ConnectionResult::Added,
        "full validation permits same registered feedback");
    Require(feedback.ValidateConnection({&state, PinType::Output, 0}, {&state, PinType::Input, 1}) ==
        ConnectionResult::Cycle, "asynchronous reset remains a combinational path");
    state.inputs[0].value = LogicValue::High;
    state.outputs[0].value = LogicValue::High;
    Require(!feedback.Evaluate(stateNodes), "legacy combinational evaluator refuses state boundaries");
    Require(state.evaluations == 0 && state.inputs[0].value == LogicValue::High &&
        state.outputs[0].value == LogicValue::High, "unsupported evaluation leaves pin state untouched");
}

inline void TestUndirectedNets()
{
    const NetPin first{1, PinType::Output, 0}, branch{2, PinType::Input, 0};
    const NetPin sameDirection{3, PinType::Input, 0}, isolated{4, PinType::Output, 0};
    std::vector<NetPin> pins{isolated, sameDirection, branch, first};
    std::vector<NetLink> links{{branch, first}, {sameDirection, branch}};
    std::vector<ElectricalNet> nets;
    std::string error;
    Require(ConnectivityBuilder::Build(pins, links, nets, error), "build physical nets independently of direction");
    Require(error.empty() && nets.size() == 2 && nets[0].id == 1 && nets[1].id == 2,
        "include isolated pins with deterministic net numbering");
    Require(nets[0].pins == std::vector<NetPin>{first, branch, sameDirection} &&
        nets[1].pins == std::vector<NetPin>{isolated}, "transitive connectivity and sorted endpoint keys");
    std::reverse(pins.begin(), pins.end());
    std::reverse(links.begin(), links.end());
    for (auto& link : links) std::swap(link.first, link.second);
    std::vector<ElectricalNet> reversed;
    Require(ConnectivityBuilder::Build(pins, links, reversed, error) && reversed.size() == nets.size() &&
        reversed[0].pins == nets[0].pins && reversed[1].pins == nets[1].pins,
        "orientation and insertion order cannot change physical nets");
    Require(!ConnectivityBuilder::Build(pins, {{first, {99, PinType::Input, 0}}}, nets, error) &&
        !error.empty() && nets.size() == 2 && nets[0].pins == reversed[0].pins,
        "unknown endpoint rejects atomically");
    pins.push_back(first);
    Require(!ConnectivityBuilder::Build(pins, {}, nets, error), "reject duplicate endpoint declarations");
    Require(!ConnectivityBuilder::Build({{0, PinType::Input, 0}}, {}, nets, error), "reject invalid object identity");
    Require(ConnectivityBuilder::Build({}, {}, nets, error) && nets.empty() && error.empty(),
        "empty design has no nets");
}

inline void TestBulkReplacement()
{
    InputComponent source;
    NotGate inverse;
    OutputComponent sink;
    const std::vector<Component*> nodes{&source, &inverse, &sink};
    Circuit circuit;
    const WireConnection original{{&source, PinType::Output, 0}, {&sink, PinType::Input, 0}};
    Require(circuit.ReplaceConnections(nodes, {original}) == ConnectionResult::Added,
        "bulk import accepts valid graph");
    const std::vector<WireConnection> replacement{
        {{&source, PinType::Output, 0}, {&inverse, PinType::Input, 0}, {{0, 0}, {20, 0}}},
        {{&inverse, PinType::Output, 0}, {&sink, PinType::Input, 0}}};
    Require(circuit.ReplaceConnections(nodes, replacement) == ConnectionResult::Added &&
        circuit.GetConnections().size() == 2 && circuit.GetConnections()[0].route == replacement[0].route,
        "bulk replacement commits all wires and retains supplied route data");
    source.outputs[0].value = LogicValue::High;
    Require(circuit.Evaluate(nodes) && sink.inputs[0].value == LogicValue::Low,
        "bulk imported connections participate in evaluation");
    const WireConnection self{{&inverse, PinType::Output, 0}, {&inverse, PinType::Input, 0}};
    Require(circuit.ReplaceConnections(nodes, {self}) == ConnectionResult::Cycle,
        "bulk import reports the same cycle rule");
    Require(circuit.ReplaceConnections(nodes, {original, original}) == ConnectionResult::Duplicate,
        "bulk import reports duplicate connections");
    const WireConnection missing{{&source, PinType::Output, 0}, {&sink, PinType::Input, 99}};
    Require(circuit.ReplaceConnections(nodes, {missing}) == ConnectionResult::InvalidPin,
        "bulk import rejects invalid endpoints");
    const auto& retained = circuit.GetConnections();
    Require(retained.size() == replacement.size() &&
        retained[0].output == replacement[0].output && retained[0].input == replacement[0].input &&
        retained[0].route == replacement[0].route &&
        retained[1].output == replacement[1].output && retained[1].input == replacement[1].input,
        "failed bulk replacements leave the previous graph and routes intact");
}
}

inline void RunTopologyRegression()
{
    topology_regression::TestSharedRules();
    topology_regression::TestUndirectedNets();
    topology_regression::TestBulkReplacement();
}
