#pragma once

#include "Component.h"
#include <wx/gdicmn.h>
#include <wx/string.h>
#include <memory>
#include <string>
#include <vector>

// 文件中的 id 仅用于建立连线，不依赖内存地址或画布下标。
struct CircuitComponentData
{
    int id = 0;
    std::string type;
    int x = 0;
    int y = 0;
    int inputCount = 0;
    bool high = false;
};

struct CircuitWireData
{
    int sourceId = 0;
    int sourcePin = 0;
    int targetId = 0;
    int targetPin = 0;
};

struct CircuitTextData
{
    wxRect rect;
    int pointSize = 14;
    wxString text;
};

struct CircuitDocument
{
    std::vector<CircuitComponentData> components;
    std::vector<CircuitWireData> wires;
    std::vector<CircuitTextData> texts;
};

// UTF-8 XML，<circuit version="1">。Load 失败不改变 document；
// Save 先写同目录临时文件，验证及写入成功后再替换目标文件。
class CircuitFile
{
public:
    static bool Save(const wxString& path, const CircuitDocument& document, wxString& error);
    static bool Load(const wxString& path, CircuitDocument& document, wxString& error);
    static bool Validate(const CircuitDocument& document, wxString& error);
    static std::unique_ptr<Component> CreateComponent(const CircuitComponentData& data);
};
