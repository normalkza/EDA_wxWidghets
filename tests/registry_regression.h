#pragma once

#include "ComponentRegistry.h"
#include "CounterComponent.h"
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
                component->outputs.size() == static_cast<std::size_t>(
                    type == "DFF" && count == 3 ? 1 : definition.outputCount),
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
    auto counter = ComponentRegistry::Create("COUNTER");
    require(counter && counter->inputs.size() == 9 && counter->outputs.size() == 5 &&
        counter->inputs[0].name == "CLK" && counter->inputs[1].name == "EN" &&
        counter->inputs[2].name == "LOAD" && counter->inputs[3].name == "DIR" &&
        counter->inputs[4].name == "D0" && counter->inputs[8].name == "CLR" &&
        counter->outputs[0].name == "Q0" && counter->outputs[4].name == "CARRY",
        "counter exposes stable Logisim-style control, data and status pins");
    require(!counter->HasCombinationalPath(0, 0) && !counter->HasCombinationalPath(4, 0) &&
        counter->HasCombinationalPath(8, 0) && counter->HasCombinationalPath(1, 4),
        "counter samples CLK/data but clears asynchronously and derives carry from control pins");
    auto dff = ComponentRegistry::Create("DFF");
    auto mux = ComponentRegistry::Create("MUX");
    require(mux && mux->inputs[0].name == "A" && mux->inputs[1].name == "B" &&
        mux->inputs[2].name == "S" && mux->outputs[0].name == "OUT",
        "MUX has stable data and select pins");
    auto comparator = ComponentRegistry::Create("COMPARATOR");
    require(comparator && comparator->inputs[0].name == "A" && comparator->inputs[1].name == "B" &&
        comparator->outputs.size() == 3 && comparator->outputs[0].name == "A>B" &&
        comparator->outputs[1].name == "A=B" && comparator->outputs[2].name == "A<B",
        "comparator has stable comparison outputs");
    require(dff && dff->inputs.size() == 2 && dff->inputs[0].name == "D" &&
        dff->inputs[1].name == "CLK" && dff->outputs[0].name == "Q" &&
        dff->outputs[1].name == "QN" && dff->outputs[1].value == LogicValue::High,
        "ideal DFF exposes only data, clock and complementary outputs");
    require(!dff->HasCombinationalPath(0, 0) && !dff->HasCombinationalPath(1, 0) &&
        !dff->HasCombinationalPath(0, 1) && !dff->HasCombinationalPath(1, 1),
        "ideal DFF has no immediate data or clock path");
    dff->inputs[0].value = LogicValue::High;
    dff->inputs[1].value = LogicValue::High;
    dff->Evaluate();
    require(dff->outputs[0].value == LogicValue::Low && dff->outputs[1].value == LogicValue::High,
        "Evaluate never samples a DFF clock");
    auto historicalDff = ComponentRegistry::Create("DFF", 4);
    require(historicalDff && historicalDff->inputs[2].name == "SET" &&
        historicalDff->inputs[3].name == "CLR" && historicalDff->outputs.size() == 2,
        "previous four-input DFF files retain their pin layout");
    auto legacyDff = ComponentRegistry::Create("DFF", 3, true);
    require(legacyDff && legacyDff->inputs[2].name == "RESET" && legacyDff->outputs.size() == 1,
        "saved three-input DFF retains its original pin layout");
    legacyDff->inputs[2].value = LogicValue::High;
    legacyDff->Evaluate();
    require(legacyDff->outputs[0].value == LogicValue::Low,
        "saved three-input DFF retains its high-active reset");
    require(!ComponentRegistry::Create("INPUT", 1) && !ComponentRegistry::Create("NOT", 2),
        "fixed-count components reject malformed serialized pin counts");
    require(!ComponentRegistry::Find("UNKNOWN") && !ComponentRegistry::Create("UNKNOWN"),
        "unknown type cannot be constructed");
    for (const auto* tool : { "", "TEXT", "DELETE", "WIRE" })
        require(!ComponentRegistry::Find(tool), "editing tools are not circuit components");
}
