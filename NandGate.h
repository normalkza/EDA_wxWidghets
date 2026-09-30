#pragma once

#include "Component.h"

class NandGate : public Component
{
public:
    NandGate()
        : Component("NAND")
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
            outputs[0].value = LogicValue::Low;
        }
        else
        {
            outputs[0].value = LogicValue::High;
        }
    }
};
