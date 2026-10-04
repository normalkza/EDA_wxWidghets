#pragma once

#include "Component.h"

// An external circuit input provides a signal to the circuit.
class InputComponent : public Component
{
public:
    InputComponent() : Component("INPUT")
    {
        outputs.emplace_back("OUT", PinType::Output);
    }

    // Keep the externally supplied value; it defaults to Low.
    void Evaluate() override {}
};

// An external circuit output receives a signal from the circuit.
class OutputComponent : public Component
{
public:
    OutputComponent() : Component("OUTPUT")
    {
        inputs.emplace_back("IN", PinType::Input);
    }

    // The received value is stored directly in the input pin.
    void Evaluate() override {}
};
