#pragma once

#include "Component.h"

class XorGate : public Component
{
public:
    XorGate()
        : Component("XOR")
    {
        inputs.emplace_back("A", PinType::Input);
        inputs.emplace_back("B", PinType::Input);
        outputs.emplace_back("OUT", PinType::Output);
    }

    void Evaluate() override
    {
        if (inputs[0].value != inputs[1].value)
        {
            outputs[0].value = LogicValue::High;
        }
        else
        {
            outputs[0].value = LogicValue::Low;
        }
    }
};
