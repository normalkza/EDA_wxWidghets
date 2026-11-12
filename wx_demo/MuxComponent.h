#pragma once

#include "Component.h"

class MuxComponent : public Component
{
public:
    MuxComponent() : Component("MUX")
    {
        inputs.emplace_back("A", PinType::Input);
        inputs.emplace_back("B", PinType::Input);
        inputs.emplace_back("S", PinType::Input);
        outputs.emplace_back("OUT", PinType::Output);
    }

    void Evaluate() override
    {
        outputs[0].value = inputs[inputs[2].value == LogicValue::High ? 1 : 0].value;
    }
};
