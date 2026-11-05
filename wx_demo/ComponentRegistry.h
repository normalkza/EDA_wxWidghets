#pragma once

#include "Component.h"
#include <memory>
#include <string_view>
#include <vector>

// 元件能力的唯一入口；类型标识不依赖界面显示名或内存地址。
// 名称、分类及图标名均为 UTF-8，界面负责转换为 wxString。
struct ComponentDefinition
{
    using Factory = std::unique_ptr<Component> (*)(int inputCount);

    std::string_view type;
    std::string_view displayName;
    std::string_view category;
    std::string_view icon;
    int defaultInputCount;
    int minInputCount;
    int maxInputCount;
    int outputCount;
    Factory factory = nullptr;

    bool IsAvailable() const { return factory != nullptr; }
    bool SupportsVariableInputs() const { return minInputCount != maxInputCount; }
    bool AcceptsInputCount(int count) const
    {
        return count >= minInputCount && count <= maxInputCount;
    }
};

class ComponentRegistry
{
public:
    static const std::vector<ComponentDefinition>& All();
    static const ComponentDefinition* Find(std::string_view type);
    static bool SupportsInitialLevel(std::string_view type);

    // 仅 -1 表示使用默认输入数；文件与剪贴板传入的显式数量严格校验。
    // high is the configured initial source level or initial DFF Q.
    static std::unique_ptr<Component> Create(std::string_view type,
        int inputCount = -1, bool high = false);
};
