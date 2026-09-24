#pragma once

#include "Component.h"

class NotGate : public Component
{
public:
    NotGate()
        : Component("Not")
    {
        inputs.emplace_back("IN", PinType::Input);
        outputs.emplace_back("OUT", PinType::Output);
    }

    void Evaluate() override
    {
        if (inputs[0].value == LogicValue::High)
        {
            outputs[0].value = LogicValue::Low;
        }
        else
        {
            outputs[0].value = LogicValue::High;
        }
    }
};