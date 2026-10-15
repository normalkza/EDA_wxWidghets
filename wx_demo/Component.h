#pragma once

#include "Pin.h"
#include <string>
#include <vector>
#include <stdexcept>

class Component
{
public:
    Component(const std::string& name)
        : name(name)
    {
    }

    virtual ~Component() = default;

    virtual void Evaluate() = 0;
    virtual bool SupportsVariableInputs() const { return false; }
    virtual bool SetInputCount(int count) { return false; }

    std::string name;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
};

// 可变输入门共享参数校验和引脚管理；调整数量时保留已有引脚的电平。
class MultiInputGate : public Component
{
public:
    static constexpr int MinInputCount = 2;
    static constexpr int MaxInputCount = 16;

    MultiInputGate(const std::string& type, int count = 2) : Component(type)
    {
        if (count < MinInputCount || count > MaxInputCount)
            throw std::invalid_argument("Gate input count must be between 2 and 16");
        ResizeInputs(count);
        outputs.emplace_back("OUT", PinType::Output);
    }

    bool SupportsVariableInputs() const override { return true; }
    bool SetInputCount(int count) override
    {
        if (count < MinInputCount || count > MaxInputCount) return false;
        ResizeInputs(count);
        Evaluate();
        return true;
    }

private:
    void ResizeInputs(int count)
    {
        if (count < static_cast<int>(inputs.size()))
            inputs.erase(inputs.begin() + count, inputs.end());
        while (static_cast<int>(inputs.size()) < count)
            inputs.emplace_back(std::string(1, static_cast<char>('A' + inputs.size())), PinType::Input);
    }
};
