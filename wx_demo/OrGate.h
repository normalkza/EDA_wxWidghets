#pragma once

#include "Component.h"
#include <algorithm>

class OrGate : public MultiInputGate
{
public:
    explicit OrGate(int inputCount = 2) : MultiInputGate("OR", inputCount) {}

    void Evaluate() override
    {
        const auto high = [](const Pin& pin) { return pin.value == LogicValue::High; };
        outputs[0].value = (std::any_of(inputs.begin(), inputs.end(), high)) ? LogicValue::High : LogicValue::Low;
    }
};
