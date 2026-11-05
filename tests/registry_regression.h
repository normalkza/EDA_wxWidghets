#pragma once

#include "ComponentRegistry.h"
#include <stdexcept>
#include <string>
#include <unordered_set>

inline void TestComponentRegistry()
{
    const auto require = [](bool value, const std::string& message) {
        if (!value) throw std::runtime_error("registry: " + message);
    };
    std::unordered_set<std::string> types;
    for (const auto& definition : ComponentRegistry::All())
    {
        const std::string type(definition.type);
        require(!type.empty() && types.insert(type).second, "type IDs are nonempty and unique");
        require(ComponentRegistry::Find(type) == &definition, "lookup returns stable definition");
        require(!definition.displayName.empty() && !definition.category.empty() && !definition.icon.empty(),
            type + " has complete presentation metadata");
        if (!definition.IsAvailable())
        {
            require(!ComponentRegistry::Create(type), type + " placeholder has no runtime model");
            continue;
        }
        require(definition.AcceptsInputCount(definition.defaultInputCount), type + " default count is valid");
        for (const int count : { definition.minInputCount, definition.defaultInputCount, definition.maxInputCount })
        {
            auto component = ComponentRegistry::Create(type, count);
            require(component && component->name == type, type + " creates correct runtime type");
            require(component->inputs.size() == static_cast<std::size_t>(count) &&
                component->outputs.size() == static_cast<std::size_t>(definition.outputCount),
                type + " pin counts agree with capability metadata");
            require(component->SupportsVariableInputs() == definition.SupportsVariableInputs(),
                type + " editing capability agrees with runtime model");
        }
        auto defaults = ComponentRegistry::Create(type);
        require(defaults && defaults->inputs.size() == static_cast<std::size_t>(definition.defaultInputCount),
            type + " omitted input count uses its own default");
        require(!ComponentRegistry::Create(type, -2), type + " rejects negative explicit count");
        if (definition.minInputCount > 0)
            require(!ComponentRegistry::Create(type, definition.minInputCount - 1), type + " rejects undersized count");
        require(!ComponentRegistry::Create(type, definition.maxInputCount + 1), type + " rejects oversized count");
        if (!ComponentRegistry::SupportsInitialLevel(type))
            require(!ComponentRegistry::Create(type, definition.defaultInputCount, true),
                type + " does not accept source-only initial level");
    }
    for (const auto* type : { "INPUT", "OUTPUT", "AND", "OR", "NOT", "NAND", "NOR", "XOR" })
        require(ComponentRegistry::Create(type) != nullptr, std::string(type) + " existing component remains supported");
    auto source = ComponentRegistry::Create("INPUT", 0, true);
    require(source && source->outputs[0].value == LogicValue::High, "INPUT restores configured initial level");
    source = ComponentRegistry::Create("INPUT", 0, false);
    require(source && source->outputs[0].value == LogicValue::Low, "INPUT defaults to low");
    for (const auto* type : {"BUTTON", "CONSTANT", "CLOCK", "DFF"})
    {
        auto initialized = ComponentRegistry::Create(type, -1, true);
        require(initialized && initialized->outputs[0].value == LogicValue::High,
            std::string(type) + " restores configured initial level");
    }
    auto dff = ComponentRegistry::Create("DFF");
    require(dff && dff->inputs.size() == 3 && dff->inputs[0].name == "D" &&
        dff->inputs[1].name == "CLK" && dff->inputs[2].name == "RESET" && dff->outputs[0].name == "Q",
        "DFF has stable named data, clock, asynchronous reset and Q pins");
    require(!dff->HasCombinationalPath(0, 0) && !dff->HasCombinationalPath(1, 0) &&
        dff->HasCombinationalPath(2, 0), "only asynchronous reset has an immediate DFF dependency");
    dff->inputs[0].value = LogicValue::High;
    dff->inputs[1].value = LogicValue::High;
    dff->Evaluate();
    require(dff->outputs[0].value == LogicValue::Low, "legacy Evaluate never samples a DFF clock");
    dff->outputs[0].value = LogicValue::High;
    dff->inputs[2].value = LogicValue::High;
    dff->Evaluate();
    require(dff->outputs[0].value == LogicValue::Low, "DFF asynchronous reset is high active");
    require(!ComponentRegistry::Create("INPUT", 1) && !ComponentRegistry::Create("NOT", 2),
        "fixed-count components reject malformed serialized pin counts");
    require(!ComponentRegistry::Find("UNKNOWN") && !ComponentRegistry::Create("UNKNOWN"),
        "unknown type cannot be constructed");
    for (const auto* tool : { "", "TEXT", "DELETE", "WIRE" })
        require(!ComponentRegistry::Find(tool), "editing tools are not circuit components");
}
