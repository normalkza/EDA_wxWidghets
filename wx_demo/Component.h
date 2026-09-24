#pragma once

#include "Pin.h"
#include <string>
#include <vector>

class Component
{
public:
    Component(const std::string& name)
        : name(name)
    {
    }

    virtual ~Component() = default;

    virtual void Evaluate() = 0;

    std::string name;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
};