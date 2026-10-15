#pragma once

#include "Component.h"
#include <algorithm>

class AndGate : public MultiInputGate
{
public:
    explicit AndGate(int inputCount = 2) : MultiInputGate("AND", inputCount) {}

    void Evaluate() override
    {
        const auto high = [](const Pin& pin) { return pin.value == LogicValue::High; };
        outputs[0].value = (std::all_of(inputs.begin(), inputs.end(), high)) ? LogicValue::High : LogicValue::Low;
    }
};
