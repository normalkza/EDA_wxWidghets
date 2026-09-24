#pragma once

#include "LogicValue.h"
#include <string>

enum class PinType
{
    Input,
    Output
};//定义引脚是输入还是输出

class Pin //一个引脚类，包含引脚的名称、类型和逻辑值
{
public:
    Pin(const std::string &name, PinType type)
        : name(name), type(type), value(LogicValue::Low)
    {
    }

    std::string name;
    PinType type;
    LogicValue value;
}; 