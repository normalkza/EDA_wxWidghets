#pragma once

#include "Component.h"

class ComparatorComponent : public Component
{
public:
    ComparatorComponent() : Component("COMPARATOR")
    {
        inputs.emplace_back("A", PinType::Input);
        inputs.emplace_back("B", PinType::Input);
        outputs.emplace_back("A>B", PinType::Output);
        outputs.emplace_back("A=B", PinType::Output);
        outputs.emplace_back("A<B", PinType::Output);
    }

    void Evaluate() override
    {
        const bool a = inputs[0].value == LogicValue::High;
        const bool b = inputs[1].value == LogicValue::High;
        outputs[0].value = a && !b ? LogicValue::High : LogicValue::Low;
        outputs[1].value = a == b ? LogicValue::High : LogicValue::Low;
        outputs[2].value = !a && b ? LogicValue::High : LogicValue::Low;
    }
};
