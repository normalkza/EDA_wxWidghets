#pragma once

#include "Component.h"
#include <algorithm>

class XorGate : public MultiInputGate
{
public:
    explicit XorGate(int inputCount = 2) : MultiInputGate("XOR", inputCount) {}

    void Evaluate() override
    {
        const auto high = [](const Pin& pin) { return pin.value == LogicValue::High; };
        outputs[0].value = ((std::count_if(inputs.begin(), inputs.end(), high) % 2) != 0) ? LogicValue::High : LogicValue::Low;
    }
};
