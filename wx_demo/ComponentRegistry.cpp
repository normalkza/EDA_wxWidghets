#include "ComponentRegistry.h"
#include "AndGate.h"
#include "OrGate.h"
#include "NotGate.h"
#include "NandGate.h"
#include "NorGate.h"
#include "XorGate.h"
#include "IOComponent.h"
#include "TimingComponents.h"
#include "MuxComponent.h"
#include "ComparatorComponent.h"

namespace
{
    template<class T>
    std::unique_ptr<Component> FixedComponent(int)
    {
        return std::make_unique<T>();
    }

    template<class T>
    std::unique_ptr<Component> VariableComponent(int count)
    {
        return std::make_unique<T>(count);
    }
}

const std::vector<ComponentDefinition>& ComponentRegistry::All()
{
    static const std::vector<ComponentDefinition> definitions{
        {"INPUT", "输入引脚", "输入 / 输出", "input", 0, 0, 0, 1, FixedComponent<InputComponent>},
        {"OUTPUT", "输出引脚", "输入 / 输出", "output", 1, 1, 1, 0, FixedComponent<OutputComponent>},
        {"BUTTON", "按钮", "输入 / 输出", "button", 0, 0, 0, 1, FixedComponent<ButtonComponent>},
        {"LED", "数字指示灯", "输入 / 输出", "led", 1, 1, 1, 0, FixedComponent<LedComponent>},
        {"AND", "与门", "逻辑门", "and", 2, MultiInputGate::MinInputCount, MultiInputGate::MaxInputCount, 1, VariableComponent<AndGate>},
        {"OR", "或门", "逻辑门", "or", 2, MultiInputGate::MinInputCount, MultiInputGate::MaxInputCount, 1, VariableComponent<OrGate>},
        {"NOT", "非门", "逻辑门", "not", 1, 1, 1, 1, FixedComponent<NotGate>},
        {"NAND", "与非门", "逻辑门", "nand", 2, MultiInputGate::MinInputCount, MultiInputGate::MaxInputCount, 1, VariableComponent<NandGate>},
        {"NOR", "或非门", "逻辑门", "nor", 2, MultiInputGate::MinInputCount, MultiInputGate::MaxInputCount, 1, VariableComponent<NorGate>},
        {"XOR", "异或门", "逻辑门", "xor", 2, MultiInputGate::MinInputCount, MultiInputGate::MaxInputCount, 1, VariableComponent<XorGate>},
        {"SPLITTER", "分线器", "线路", "splitter", 1, 1, 1, 1},
        {"PROBE", "探针", "线路", "probe", 1, 1, 1, 0, FixedComponent<ProbeComponent>},
        {"CLOCK", "时钟", "线路", "clock", 0, 0, 0, 1, FixedComponent<ClockComponent>},
        {"CONSTANT", "常量", "线路", "constant", 0, 0, 0, 1, FixedComponent<ConstantComponent>},
        {"MUX", "数据选择器", "复用器", "mux", 3, 3, 3, 1, FixedComponent<MuxComponent>},
        {"DEMUX", "解复用器", "复用器", "demux", 2, 2, 2, 2},
        {"ADDER", "加法器", "运算器", "adder", 2, 2, 2, 1},
        {"SUBTRACTOR", "减法器", "运算器", "subtractor", 2, 2, 2, 1},
        {"COMPARATOR", "比较器", "运算器", "comparator", 2, 2, 2, 3, FixedComponent<ComparatorComponent>},
        {"DFF", "D 触发器", "存储", "dff", 3, 3, 3, 1, FixedComponent<DffComponent>},
        {"REGISTER", "寄存器", "存储", "register", 2, 2, 2, 1},
        {"COUNTER", "计数器", "存储", "counter", 1, 1, 1, 1}
    };
    return definitions;
}

const ComponentDefinition* ComponentRegistry::Find(std::string_view type)
{
    for (const auto& definition : All())
        if (definition.type == type) return &definition;
    return nullptr;
}

std::unique_ptr<Component> ComponentRegistry::Create(std::string_view type, int inputCount, bool high)
{
    const auto* definition = Find(type);
    if (!definition || !definition->IsAvailable() || (high && !SupportsInitialLevel(type))) return nullptr;
    if (inputCount == -1) inputCount = definition->defaultInputCount;
    if (!definition->AcceptsInputCount(inputCount)) return nullptr;
    auto component = definition->factory(inputCount);
    if (SupportsInitialLevel(type))
        component->outputs[0].value = high ? LogicValue::High : LogicValue::Low;
    return component;
}

bool ComponentRegistry::SupportsInitialLevel(std::string_view type)
{
    return type == "INPUT" || type == "BUTTON" || type == "CONSTANT" || type == "CLOCK" || type == "DFF";
}
