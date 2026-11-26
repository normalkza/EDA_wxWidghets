#pragma once

#include "Component.h"
#include <algorithm>
#include <cstddef>

// 4-bit up/down counter modelled after Logisim's memory counter.
// The four Q pins and CARRY are outputs; all control/data pins are inputs.
// Sampling and state commits belong to DigitalSimulator, like the DFF.
class CounterComponent : public Component
{
public:
    static constexpr std::size_t PinClk = 0;
    static constexpr std::size_t PinEnable = 1;
    static constexpr std::size_t PinLoad = 2;
    static constexpr std::size_t PinDirection = 3;
    static constexpr std::size_t PinD0 = 4;
    static constexpr std::size_t PinD3 = 7;
    static constexpr std::size_t PinClear = 8;
    static constexpr std::size_t QBitCount = 4;
    static constexpr std::size_t PinCarry = QBitCount;
    static constexpr int DefaultMaximum = 15;

    CounterComponent() : Component("COUNTER")
    {
        inputs.emplace_back("CLK", PinType::Input);
        inputs.emplace_back("EN", PinType::Input);
        inputs.emplace_back("LOAD", PinType::Input);
        inputs.emplace_back("DIR", PinType::Input);
        for (std::size_t bit = 0; bit < QBitCount; ++bit)
            inputs.emplace_back("D" + std::to_string(bit), PinType::Input);
        inputs.emplace_back("CLR", PinType::Input);
        for (std::size_t bit = 0; bit < QBitCount; ++bit)
            outputs.emplace_back("Q" + std::to_string(bit), PinType::Output);
        outputs.emplace_back("CARRY", PinType::Output);
        Evaluate();
    }

    static constexpr std::size_t DataPin(std::size_t bit) { return PinD0 + bit; }

    // Only the combinational carry is computed here. Q bits are state and are
    // committed by the simulator so every counter samples the same snapshot.
    void Evaluate() override
    {
        const bool clear = inputs[PinClear].value == LogicValue::High;
        const bool load = inputs[PinLoad].value == LogicValue::High;
        const bool enabled = inputs[PinEnable].value == LogicValue::High;
        const bool up = inputs[PinDirection].value == LogicValue::High;
        const int value = Value();
        const bool carry = !clear && !load && enabled &&
            ((up && value == m_maximum) || (!up && value == 0));
        outputs[PinCarry].value = carry ? LogicValue::High : LogicValue::Low;
    }

    bool HasCombinationalPath(std::size_t inputIndex, std::size_t outputIndex) const override
    {
        if (inputIndex >= inputs.size() || outputIndex >= outputs.size()) return false;
        if (inputIndex == PinClear) return true;
        if (inputIndex == PinEnable || inputIndex == PinLoad || inputIndex == PinDirection)
            return outputIndex == PinCarry;
        // CLK and D bits are sampled only on an active edge.
        return false;
    }

    void Configure(int maximum, int initial)
    {
        m_maximum = std::clamp(maximum, 1, static_cast<int>(1U << QBitCount) - 1);
        SetState(initial);
    }

    int Maximum() const { return m_maximum; }

    int Value() const
    {
        int value = 0;
        for (std::size_t bit = 0; bit < QBitCount; ++bit)
            if (outputs[bit].value == LogicValue::High) value |= 1 << bit;
        return value;
    }

    void SetState(int value)
    {
        const int bounded = std::clamp(value, 0, m_maximum);
        for (std::size_t bit = 0; bit < QBitCount; ++bit)
            outputs[bit].value = (bounded & (1 << bit)) ? LogicValue::High : LogicValue::Low;
        Evaluate();
    }

private:
    int m_maximum = DefaultMaximum;
};
