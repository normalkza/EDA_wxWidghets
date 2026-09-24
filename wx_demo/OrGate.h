#pragma once

#include "Component.h"

class OrGate : public Component
{
public:
    OrGate()
        : Component("Or")
    {
        inputs.emplace_back("A", PinType::Input);
        inputs.emplace_back("B", PinType::Input);
        outputs.emplace_back("OUT", PinType::Output);
    }

    void Evaluate() override
    {
        if (inputs[0].value == LogicValue::Low &&
            inputs[1].value == LogicValue::Low)
        {
            outputs[0].value = LogicValue::Low;
        }
        else
        {
            outputs[0].value = LogicValue::High;
        }
    }
};