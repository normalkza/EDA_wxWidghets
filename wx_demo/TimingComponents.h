#pragma once

#include "Component.h"

class DigitalSourceComponent : public Component
{
public:
    explicit DigitalSourceComponent(const std::string& type) : Component(type)
    {
        outputs.emplace_back("OUT", PinType::Output);
    }
    void Evaluate() override {}
};

class ButtonComponent : public DigitalSourceComponent
{
public:
    ButtonComponent() : DigitalSourceComponent("BUTTON") {}
};

class ConstantComponent : public DigitalSourceComponent
{
public:
    ConstantComponent() : DigitalSourceComponent("CONSTANT") {}
};

class ClockComponent : public DigitalSourceComponent
{
public:
    ClockComponent() : DigitalSourceComponent("CLOCK") {}
};

class DigitalIndicatorComponent : public Component
{
public:
    explicit DigitalIndicatorComponent(const std::string& type) : Component(type)
    {
        inputs.emplace_back("IN", PinType::Input);
    }
    void Evaluate() override {}
};

class LedComponent : public DigitalIndicatorComponent
{
public:
    LedComponent() : DigitalIndicatorComponent("LED") {}
};

class ProbeComponent : public DigitalIndicatorComponent
{
public:
    ProbeComponent() : DigitalIndicatorComponent("PROBE") {}
};

class DffComponent : public Component
{
public:
    DffComponent() : Component("DFF")
    {
        inputs.emplace_back("D", PinType::Input);
        inputs.emplace_back("CLK", PinType::Input);
        inputs.emplace_back("RESET", PinType::Input);
        outputs.emplace_back("Q", PinType::Output);
    }
    // Sampling and simultaneous state commit belong to DigitalSimulator.
    void Evaluate() override
    {
        if (inputs[2].value == LogicValue::High) outputs[0].value = LogicValue::Low;
    }
    bool HasCombinationalPath(std::size_t inputIndex, std::size_t outputIndex) const override
    {
        return inputIndex == 2 && outputIndex == 0;
    }
};
