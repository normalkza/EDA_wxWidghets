#pragma once

#include "Component.h"
#include <algorithm>

class NorGate : public MultiInputGate
{
public:
    explicit NorGate(int inputCount = 2) : MultiInputGate("NOR", inputCount) {}

    void Evaluate() override
    {
        const auto high = [](const Pin& pin) { return pin.value == LogicValue::High; };
        outputs[0].value = (!std::any_of(inputs.begin(), inputs.end(), high)) ? LogicValue::High : LogicValue::Low;
    }
};
