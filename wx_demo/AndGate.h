#pragma once

#include "Component.h"

class AndGate : public Component
{
public:
    AndGate()
        : Component("AND")
    {
        inputs.emplace_back("A", PinType::Input);
        inputs.emplace_back("B", PinType::Input);
        outputs.emplace_back("OUT", PinType::Output);
    }

    void Evaluate() override
    {
        if (inputs[0].value == LogicValue::High &&
            inputs[1].value == LogicValue::High)
        {
            outputs[0].value = LogicValue::High;
        }
        else
        {
            outputs[0].value = LogicValue::Low;
        }
    }
};