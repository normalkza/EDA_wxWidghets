#pragma once

#include "Component.h"
#include "CircuitDocument.h"
#include "CircuitLimits.h"
#include <memory>

// UTF-8 XML。v1 使用引脚下标，v2 使用稳定引脚名，v3 增加数字时序参数和配置。
// 读取后统一为 CircuitDocument；保留原版本，保存时不会自动升级旧文件。
// Load 失败不改变 document；
// Save 先写同目录临时文件，验证及写入成功后再替换目标文件。
class CircuitFile
{
public:
    static constexpr int CurrentVersion = CircuitLimits::CurrentFormatVersion;
    static bool Save(const wxString& path, const CircuitDocument& document, wxString& error);
    static bool Load(const wxString& path, CircuitDocument& document, wxString& error);
    static bool Validate(const CircuitDocument& document, wxString& error);
    static std::unique_ptr<Component> CreateComponent(const CircuitComponentData& data);
};
