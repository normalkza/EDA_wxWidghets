#include <wx/wx.h>
#include "CircuitFile.h"
#include "CircuitLimits.h"
#include "ComponentRegistry.h"
#include "CircuitValidation.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/xml/xml.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#else
#include <cstdio>
#endif
#include <initializer_list>
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <limits>

namespace
{
using namespace CircuitLimits;

class PendingCircuitFile
{
public:
    ~PendingCircuitFile()
    {
        m_file.Close();
        if (!m_temporary.empty()) wxRemoveFile(m_temporary);
    }

    bool Open(const wxString& path)
    {
        wxFileName target(path);
        if (!target.MakeAbsolute()) return false;
        m_target = target.GetFullPath();
        m_temporary = wxFileName::CreateTempFileName(m_target, &m_file);
        return !m_temporary.empty() && m_file.IsOpened();
    }

    bool WriteAndClose(const std::vector<char>& bytes)
    {
        return m_file.Write(bytes.data(), bytes.size()) == bytes.size() &&
            m_file.Flush() && m_file.Close();
    }

    bool Commit()
    {
        // wxTempFile::Commit 最终可能回退为复制，复制中断会破坏旧文件。
        // 此处只允许同目录内的原子重命名，绝不回退为复制或先删除旧文件。
#ifdef __WXMSW__
        const bool replaced = ::MoveFileExW(m_temporary.wc_str(), m_target.wc_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        const bool replaced = ::rename(m_temporary.fn_str(), m_target.fn_str()) == 0;
#endif
        if (replaced) m_temporary.clear();
        return replaced;
    }

private:
    wxFile m_file;
    wxString m_target;
    wxString m_temporary;
};

bool Fail(wxString& error, const wxString& message)
{
    error = message;
    return false;
}

bool CheckXmlDeclaration(const std::string& declaration, wxString& error)
{
    for (const auto* name : { "version", "encoding" })
    {
        auto position = declaration.find(name);
        if (position == std::string::npos) continue;
        position += std::char_traits<char>::length(name);
        position = declaration.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos || declaration[position] != '=')
            return Fail(error, wxS("XML 声明格式错误。"));
        position = declaration.find_first_not_of(" \t\r\n", position + 1);
        if (position == std::string::npos ||
            (declaration[position] != '\'' && declaration[position] != '"'))
            return Fail(error, wxS("XML 声明属性必须使用引号。"));
        const auto close = declaration.find(declaration[position], position + 1);
        if (close == std::string::npos)
            return Fail(error, wxS("XML 声明格式错误。"));
        const auto value = declaration.substr(position + 1, close - position - 1);
        if ((std::string(name) == "version" && value != "1.0") ||
            (std::string(name) == "encoding" &&
                wxString::FromUTF8(value).CmpNoCase(wxS("UTF-8")) != 0))
            return Fail(error, wxS("仅支持 XML 1.0、UTF-8 编码的电路文件。"));
    }
    return true;
}

// wxXmlDocument 没有禁用 DTD 的 Load 开关，必须在调用解析器前拒绝声明。
// 同时限制嵌套深度；否则即使之后拒绝未知节点，深层树的销毁也可能耗尽栈。
bool CheckXmlInput(const std::string& bytes, wxString& error)
{
    if (bytes.find('\0') != std::string::npos ||
        wxString::FromUTF8(bytes.data(), bytes.size()).empty())
        return Fail(error, wxS("文件必须是有效的 UTF-8 XML，不能包含空字节。"));
    int depth = 0;
    std::size_t nodes = 0;
    for (std::size_t position = 0; position < bytes.size();)
    {
        position = bytes.find('<', position);
        if (position == std::string::npos) break;
        if (++nodes > MaxXmlNodes)
            return Fail(error, wxS("XML 节点过多，无法打开。"));
        if (bytes.compare(position, 4, "<!--") == 0 ||
            bytes.compare(position, 9, "<![CDATA[") == 0 ||
            bytes.compare(position, 2, "<?") == 0)
        {
            const bool comment = bytes.compare(position, 4, "<!--") == 0;
            const bool cdata = bytes.compare(position, 9, "<![CDATA[") == 0;
            const char* end = comment ? "-->" : (cdata ? "]]>" : "?>");
            const auto close = bytes.find(end, position + (comment ? 4 : (cdata ? 9 : 2)));
            if (close == std::string::npos)
                return Fail(error, wxS("XML 注释、文本或声明没有正确结束。"));
            if (!comment && !cdata && bytes.compare(position, 5, "<?xml") == 0 &&
                position + 5 < bytes.size() &&
                std::string(" \t\r\n").find(bytes[position + 5]) != std::string::npos &&
                !CheckXmlDeclaration(bytes.substr(position + 5, close - position - 5), error))
                return false;
            position = close + (comment || cdata ? 3 : 2);
            continue;
        }
        if (bytes.compare(position, 2, "<!") == 0)
            return Fail(error, wxS("电路文件不允许 DTD、实体声明或其他外部声明。"));
        const bool closing = bytes.compare(position, 2, "</") == 0;
        char quote = 0;
        std::size_t close = position + 1;
        for (; close < bytes.size(); ++close)
        {
            const char character = bytes[close];
            if (quote)
            {
                if (character == quote) quote = 0;
            }
            else if (character == '\'' || character == '"') quote = character;
            else if (character == '>') break;
        }
        if (close == bytes.size())
            return Fail(error, wxS("XML 标签没有正确结束。"));
        if (closing) --depth;
        else if (close == 0 || bytes[close - 1] != '/') ++depth;
        if (depth < 0 || depth > MaxXmlDepth)
            return Fail(error, wxS("XML 层级无效或嵌套过深。"));
        position = close + 1;
    }
    return true;
}

bool IsFormattingNode(const wxXmlNode& node)
{
    return node.GetType() == wxXML_COMMENT_NODE ||
        (node.GetType() == wxXML_TEXT_NODE && node.IsWhitespaceOnly());
}

bool CheckAttributes(const wxXmlNode& node,
                     std::initializer_list<const char*> required, wxString& error,
                     std::initializer_list<const char*> optional = {})
{
    for (const auto* name : required)
        if (!node.HasAttribute(wxString::FromUTF8(name)))
            return Fail(error, wxString::Format(wxS("元素 <%s> 缺少必需属性 %s。"),
                node.GetName(), wxString::FromUTF8(name)));
    for (auto* attribute = node.GetAttributes(); attribute; attribute = attribute->GetNext())
    {
        bool known = false;
        for (const auto* name : required)
            known = known || attribute->GetName() == wxString::FromUTF8(name);
        for (const auto* name : optional)
            known = known || attribute->GetName() == wxString::FromUTF8(name);
        if (!known)
            return Fail(error, wxString::Format(wxS("元素 <%s> 包含未知属性 %s。"),
                node.GetName(), attribute->GetName()));
    }
    return true;
}

bool ReadInt(const wxXmlNode& node, const char* name, int& value, wxString& error)
{
    const auto attributeName = wxString::FromUTF8(name);
    const wxString text = node.GetAttribute(attributeName);
    std::size_t start = !text.empty() && text[0] == '-' ? 1 : 0;
    bool valid = start < text.length();
    for (std::size_t index = start; index < text.length(); ++index)
        valid = valid && text[index] >= '0' && text[index] <= '9';
    if (!valid || !text.ToInt(&value))
        return Fail(error, wxString::Format(wxS("元素 <%s> 的属性 %s 必须是有效整数。"),
            node.GetName(), attributeName));
    return true;
}

bool ReadInt64(const wxXmlNode& node, const char* name, std::int64_t& value, wxString& error)
{
    const auto attributeName = wxString::FromUTF8(name);
    const auto text = node.GetAttribute(attributeName);
    const std::size_t start = !text.empty() && text[0] == '-' ? 1 : 0;
    bool valid = start < text.length();
    for (std::size_t index = start; index < text.length(); ++index)
        valid = valid && text[index] >= '0' && text[index] <= '9';
    wxLongLong_t parsed = 0;
    if (!valid || !text.ToLongLong(&parsed))
        return Fail(error, wxString::Format(wxS("元素 <%s> 的属性 %s 必须是有效的 64 位整数。"), node.GetName(), attributeName));
    value = static_cast<std::int64_t>(parsed);
    return true;
}

bool HasNoDataChildren(const wxXmlNode& node, wxString& error);

bool ParseSimulation(const wxXmlNode& node, SimulationConfig& config, wxString& error)
{
    if (!CheckAttributes(node, {"stopTimeNs", "maxEvents", "maxDeltaCycles", "maxTraceSamples"}, error) ||
        !HasNoDataChildren(node, error)) return false;
    std::int64_t events = 0, deltas = 0, samples = 0;
    if (!ReadInt64(node, "stopTimeNs", config.stopTimeNs, error) ||
        !ReadInt64(node, "maxEvents", events, error) || !ReadInt64(node, "maxDeltaCycles", deltas, error) ||
        !ReadInt64(node, "maxTraceSamples", samples, error)) return false;
    if (events < 1 || events > 10000000 || deltas < 1 || deltas > 100000 || samples < 1 || samples > 10000000)
        return Fail(error, wxS("仿真事件、delta 或采样数量超出支持范围。"));
    config.maxEvents = static_cast<std::size_t>(events);
    config.maxDeltaCycles = static_cast<std::size_t>(deltas);
    config.maxTraceSamples = static_cast<std::size_t>(samples);
    return true;
}

bool HasNoDataChildren(const wxXmlNode& node, wxString& error)
{
    for (auto* child = node.GetChildren(); child; child = child->GetNext())
        if (!IsFormattingNode(*child))
            return Fail(error, wxString::Format(wxS("元素 <%s> 不允许子元素或文本。"), node.GetName()));
    return true;
}

void AddInt(wxXmlNode& node, const char* name, int value)
{
    node.AddAttribute(wxString::FromUTF8(name), wxString::Format(wxS("%d"), value));
}

void AddInt64(wxXmlNode& node, const char* name, std::int64_t value)
{
    node.AddAttribute(wxString::FromUTF8(name), wxString::FromUTF8(std::to_string(value)));
}

wxXmlNode* AddElement(wxXmlNode& parent, const wxString& name)
{
    // 带 parent 的 wxXmlNode 构造函数会前插；这里追加以保留画布绘制顺序。
    auto* node = new wxXmlNode(wxXML_ELEMENT_NODE, name);
    parent.AddChild(node);
    return node;
}

bool ParseComponents(const wxXmlNode& section, CircuitDocument& document, wxString& error)
{
    if (!CheckAttributes(section, {}, error)) return false;
    for (auto* node = section.GetChildren(); node; node = node->GetNext())
    {
        if (IsFormattingNode(*node)) continue;
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != wxS("component"))
            return Fail(error, wxS("components 中只能包含 component 元件。"));
        if (document.components.size() >= MaxComponents)
            return Fail(error, wxS("元件数量超过上限（1000）。"));
        CircuitComponentData data;
        data.type = node->GetAttribute(wxS("type")).ToStdString(wxConvUTF8);
        if (document.formatVersion < 3 && (data.type == "CLOCK" || data.type == "DFF" || data.type == "PROBE" ||
            data.type == "BUTTON" || data.type == "LED" || data.type == "CONSTANT"))
            return Fail(error, wxS("此元件需要 version=3 数字时序文件格式。"));
        if (document.formatVersion >= 3 && data.type == "CLOCK")
        {
            if (!CheckAttributes(*node, {"id", "type", "x", "y", "inputCount", "high", "clockPeriodNs", "clockHighNs", "clockStartNs"}, error, {"rotation"}) ||
                !ReadInt64(*node, "clockPeriodNs", data.clockPeriodNs, error) ||
                !ReadInt64(*node, "clockHighNs", data.clockHighNs, error) ||
                !ReadInt64(*node, "clockStartNs", data.clockStartNs, error)) return false;
        }
        else if (document.formatVersion >= 3 && data.type == "PROBE")
        {
            if (!CheckAttributes(*node, {"id", "type", "x", "y", "inputCount", "high"}, error, {"rotation", "probeLabel"})) return false;
            data.probeLabel = node->GetAttribute(wxS("probeLabel")).ToStdString(wxConvUTF8);
        }
        else if (!CheckAttributes(*node, { "id", "type", "x", "y", "inputCount", "high" }, error, { "rotation" })) return false;
        if (!HasNoDataChildren(*node, error)) return false;
        if (!ReadInt(*node, "id", data.id, error) || !ReadInt(*node, "x", data.x, error) ||
            !ReadInt(*node, "y", data.y, error) ||
            !ReadInt(*node, "inputCount", data.inputCount, error)) return false;
        if (node->HasAttribute(wxS("rotation")) && !ReadInt(*node, "rotation", data.rotation, error))
            return false;
        const wxString high = node->GetAttribute(wxS("high"));
        if (high != wxS("0") && high != wxS("1"))
            return Fail(error, wxS("元件的 high 属性只能是 0 或 1。"));
        data.high = high == wxS("1");
        document.components.push_back(std::move(data));
    }
    return true;
}

using ComponentsById = std::unordered_map<int, std::unique_ptr<Component>>;

// A v2 pin name is resolved against the registered component definition, never
// against XML ordering or a temporary canvas object. Build each definition once.
bool BuildComponentIndex(const CircuitDocument& document, ComponentsById& components, wxString& error)
{
    components.reserve(document.components.size());
    for (const auto& data : document.components)
    {
        if (data.id <= 0 || components.contains(data.id))
            return Fail(error, wxS("元件 id 必须为正整数且不能重复。"));
        auto component = CircuitFile::CreateComponent(data);
        if (!component)
            return Fail(error, wxString::Format(wxS("元件 %d 的类型、输入数量或电平无效。"), data.id));
        components.emplace(data.id, std::move(component));
    }
    return true;
}

bool ResolvePinId(const ComponentsById& components, int componentId, PinType direction,
    const wxString& pinId, int& index, wxString& error)
{
    const auto found = components.find(componentId);
    if (found == components.end()) return Fail(error, wxS("连线引用了不存在的元件。"));
    if (pinId.empty()) return Fail(error, wxS("v2 连线的引脚标识不能为空。"));
    const auto& pins = direction == PinType::Output ? found->second->outputs : found->second->inputs;
    int resolved = -1;
    for (std::size_t pin = 0; pin < pins.size(); ++pin)
    {
        if (wxString::FromUTF8(pins[pin].name) != pinId) continue;
        if (resolved >= 0)
            return Fail(error, wxString::Format(wxS("元件 %d 的引脚标识 %s 存在歧义。"), componentId, pinId));
        resolved = static_cast<int>(pin);
    }
    if (resolved < 0)
        return Fail(error, wxString::Format(wxS("元件 %d 不存在对应方向的引脚 %s。"), componentId, pinId));
    index = resolved;
    return true;
}

bool ParseWires(const wxXmlNode& section, CircuitDocument& document, wxString& error)
{
    if (!CheckAttributes(section, {}, error)) return false;
    ComponentsById components;
    if (document.formatVersion >= 2 && !BuildComponentIndex(document, components, error)) return false;
    std::size_t totalRoutePoints = 0;
    for (auto* node = section.GetChildren(); node; node = node->GetNext())
    {
        if (IsFormattingNode(*node)) continue;
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != wxS("wire"))
            return Fail(error, wxS("wires 中只能包含 wire 连线。"));
        if (document.wires.size() >= MaxWires)
            return Fail(error, wxS("连线数量超过上限（4000）。"));
        CircuitWireData data;
        if (document.formatVersion == 1)
        {
            if (!CheckAttributes(*node, { "sourceId", "sourcePin", "targetId", "targetPin" }, error) ||
                !ReadInt(*node, "sourceId", data.sourceId, error) ||
                !ReadInt(*node, "sourcePin", data.sourcePin, error) ||
                !ReadInt(*node, "targetId", data.targetId, error) ||
                !ReadInt(*node, "targetPin", data.targetPin, error)) return false;
        }
        else
        {
            // Strict per-version schemas reject numeric/name mixtures rather
            // than silently choosing whichever attribute happens to be read.
            if (!CheckAttributes(*node, { "sourceId", "sourcePinId", "targetId", "targetPinId" }, error) ||
                !ReadInt(*node, "sourceId", data.sourceId, error) ||
                !ReadInt(*node, "targetId", data.targetId, error) ||
                !ResolvePinId(components, data.sourceId, PinType::Output,
                    node->GetAttribute(wxS("sourcePinId")), data.sourcePin, error) ||
                !ResolvePinId(components, data.targetId, PinType::Input,
                    node->GetAttribute(wxS("targetPinId")), data.targetPin, error)) return false;
        }
        for (auto* point = node->GetChildren(); point; point = point->GetNext())
        {
            if (IsFormattingNode(*point)) continue;
            if (point->GetType() != wxXML_ELEMENT_NODE || point->GetName() != wxS("point"))
                return Fail(error, wxS("wire 中只能包含 point 路径点。"));
            if (data.route.size() >= MaxRoutePoints || totalRoutePoints >= MaxTotalRoutePoints)
                return Fail(error, wxS("连线路径点过多：每条最多 256 个，全部连线最多 4000 个。"));
            wxPoint position;
            if (!CheckAttributes(*point, { "x", "y" }, error) || !HasNoDataChildren(*point, error) ||
                !ReadInt(*point, "x", position.x, error) || !ReadInt(*point, "y", position.y, error))
                return false;
            data.route.push_back(position);
            ++totalRoutePoints;
        }
        if (!CircuitValidation::ValidateRoute(data.route, error)) return false;
        document.wires.push_back(std::move(data));
    }
    return true;
}

bool ParseTexts(const wxXmlNode& section, CircuitDocument& document, wxString& error)
{
    if (!CheckAttributes(section, {}, error)) return false;
    std::size_t totalLength = 0;
    for (auto* node = section.GetChildren(); node; node = node->GetNext())
    {
        if (IsFormattingNode(*node)) continue;
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != wxS("text"))
            return Fail(error, wxS("texts 中只能包含 text 文本框。"));
        if (document.texts.size() >= MaxTexts)
            return Fail(error, wxS("文本框数量超过上限（1000）。"));
        if (!CheckAttributes(*node, { "x", "y", "width", "height", "pointSize" }, error)) return false;
        int x = 0, y = 0, width = 0, height = 0;
        CircuitTextData data;
        if (!ReadInt(*node, "x", x, error) || !ReadInt(*node, "y", y, error) ||
            !ReadInt(*node, "width", width, error) || !ReadInt(*node, "height", height, error) ||
            !ReadInt(*node, "pointSize", data.pointSize, error)) return false;
        data.rect = wxRect(x, y, width, height);
        for (auto* child = node->GetChildren(); child; child = child->GetNext())
        {
            if (child->GetType() == wxXML_COMMENT_NODE) continue;
            if (child->GetType() != wxXML_TEXT_NODE && child->GetType() != wxXML_CDATA_SECTION_NODE)
                return Fail(error, wxS("文本框中只能包含文字，不能嵌套 XML 元素。"));
            if (child->GetContent().length() > MaxTextLength - data.text.length())
                return Fail(error, wxS("单个文本框超过 65536 个字符。"));
            data.text += child->GetContent();
        }
        totalLength += data.text.length();
        if (totalLength > MaxTotalTextLength)
            return Fail(error, wxS("文本总长度超过 262144 个字符。"));
        document.texts.push_back(std::move(data));
    }
    return true;
}
}

std::unique_ptr<Component> CircuitFile::CreateComponent(const CircuitComponentData& data)
{
    if (data.inputCount < 0) return nullptr;
    return ComponentRegistry::Create(data.type, data.inputCount, data.high);
}

bool CircuitFile::Validate(const CircuitDocument& document, wxString& error)
{
    return CircuitValidation::Validate(document, error);
}

bool CircuitFile::Save(const wxString& path, const CircuitDocument& document, wxString& error)
{
    error.clear();
    if (path.empty()) return Fail(error, wxS("保存路径不能为空。"));
    if (!Validate(document, error)) return false;
    ComponentsById definitions;
    if (document.formatVersion < 3)
    {
        if (!(document.simulation == SimulationConfig{}))
            return Fail(error, wxS("保存仿真配置需要 version=3，请将文档升级为新格式。"));
        for (const auto& data : document.components)
            if (data.type == "CLOCK" || data.type == "DFF" || data.type == "PROBE" || data.type == "BUTTON" ||
                data.type == "LED" || data.type == "CONSTANT" || data.clockPeriodNs != 20 ||
                data.clockHighNs != 10 || data.clockStartNs != 0 || !data.probeLabel.empty())
                return Fail(error, wxS("保存时序元件及参数需要 version=3，请将文档升级为新格式。"));
    }
    if (document.formatVersion >= 2 && !BuildComponentIndex(document, definitions, error)) return false;
    wxXmlDocument xml;
    xml.SetVersion(wxS("1.0"));
    xml.SetFileEncoding(wxS("UTF-8"));
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, wxS("circuit"));
    AddInt(*root, "version", document.formatVersion);
    xml.SetRoot(root);
    auto* components = AddElement(*root, wxS("components"));
    for (const auto& data : document.components)
    {
        auto* node = AddElement(*components, wxS("component"));
        AddInt(*node, "id", data.id);
        node->AddAttribute(wxS("type"), wxString::FromUTF8(data.type));
        AddInt(*node, "x", data.x);
        AddInt(*node, "y", data.y);
        AddInt(*node, "inputCount", data.inputCount);
        AddInt(*node, "high", data.high ? 1 : 0);
        if (data.rotation != 0) AddInt(*node, "rotation", data.rotation);
        if (document.formatVersion >= 3 && data.type == "CLOCK")
        {
            AddInt64(*node, "clockPeriodNs", data.clockPeriodNs);
            AddInt64(*node, "clockHighNs", data.clockHighNs);
            AddInt64(*node, "clockStartNs", data.clockStartNs);
        }
        if (document.formatVersion >= 3 && data.type == "PROBE" && !data.probeLabel.empty())
            node->AddAttribute(wxS("probeLabel"), wxString::FromUTF8(data.probeLabel));
    }
    auto* wires = AddElement(*root, wxS("wires"));
    for (const auto& data : document.wires)
    {
        auto* node = AddElement(*wires, wxS("wire"));
        AddInt(*node, "sourceId", data.sourceId);
        AddInt(*node, "targetId", data.targetId);
        if (document.formatVersion == 1)
        {
            AddInt(*node, "sourcePin", data.sourcePin);
            AddInt(*node, "targetPin", data.targetPin);
        }
        else
        {
            const auto sourcePinId = wxString::FromUTF8(definitions.at(data.sourceId)->outputs[data.sourcePin].name);
            const auto targetPinId = wxString::FromUTF8(definitions.at(data.targetId)->inputs[data.targetPin].name);
            int resolvedSource = -1, resolvedTarget = -1;
            if (!ResolvePinId(definitions, data.sourceId, PinType::Output, sourcePinId, resolvedSource, error) ||
                !ResolvePinId(definitions, data.targetId, PinType::Input, targetPinId, resolvedTarget, error))
                return false;
            node->AddAttribute(wxS("sourcePinId"), sourcePinId);
            node->AddAttribute(wxS("targetPinId"), targetPinId);
        }
        for (const auto& position : data.route)
        {
            auto* point = AddElement(*node, wxS("point"));
            AddInt(*point, "x", position.x);
            AddInt(*point, "y", position.y);
        }
    }
    auto* texts = AddElement(*root, wxS("texts"));
    for (const auto& data : document.texts)
    {
        auto* node = AddElement(*texts, wxS("text"));
        AddInt(*node, "x", data.rect.x);
        AddInt(*node, "y", data.rect.y);
        AddInt(*node, "width", data.rect.width);
        AddInt(*node, "height", data.rect.height);
        AddInt(*node, "pointSize", data.pointSize);
        new wxXmlNode(node, wxXML_TEXT_NODE, wxEmptyString, data.text);
    }
    if (document.formatVersion >= 3)
    {
        auto* simulation = AddElement(*root, wxS("simulation"));
        AddInt64(*simulation, "stopTimeNs", document.simulation.stopTimeNs);
        AddInt64(*simulation, "maxEvents", static_cast<std::int64_t>(document.simulation.maxEvents));
        AddInt64(*simulation, "maxDeltaCycles", static_cast<std::int64_t>(document.simulation.maxDeltaCycles));
        AddInt64(*simulation, "maxTraceSamples", static_cast<std::int64_t>(document.simulation.maxTraceSamples));
    }
    // 先完成序列化再打开临时文件，任何验证/编码错误都不会触及旧文件。
    wxMemoryOutputStream memory;
    if (!xml.Save(memory) || !memory.IsOk() || memory.GetSize() > MaxFileBytes)
        return Fail(error, wxS("无法生成 XML，或保存文件超过 8 MiB 上限。"));
    std::vector<char> bytes(memory.GetSize());
    if (memory.CopyTo(bytes.data(), bytes.size()) != bytes.size())
        return Fail(error, wxS("生成 XML 数据失败。"));
    wxLogNull quiet;
    PendingCircuitFile output;
    if (!output.Open(path))
        return Fail(error, wxS("无法创建保存文件，请检查路径和文件夹写入权限。"));
    if (!output.WriteAndClose(bytes))
        return Fail(error, wxS("写入或关闭文件失败，请检查磁盘空间和写入权限；原文件已保留。"));
    if (!output.Commit())
        return Fail(error, wxS("无法替换目标文件，请检查文件是否被占用或设置为只读。"));
    return true;
}

bool CircuitFile::Load(const wxString& path, CircuitDocument& document, wxString& error)
{
    error.clear();
    if (path.empty()) return Fail(error, wxS("打开路径不能为空。"));
    wxLogNull quiet;
    wxFile file(path, wxFile::read);
    if (!file.IsOpened())
        return Fail(error, wxS("无法打开文件，请检查文件是否存在及读取权限。"));
    const auto length = file.Length();
    if (length <= 0 || length > static_cast<wxFileOffset>(MaxFileBytes))
        return Fail(error, wxS("文件为空、无法读取，或大小超过 8 MiB 上限。"));
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (file.Read(bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size()))
        return Fail(error, wxS("文件读取不完整，无法打开。"));
    if (!CheckXmlInput(bytes, error)) return false;
    wxMemoryInputStream stream(bytes.data(), bytes.size());
    wxXmlDocument xml;
    wxXmlParseError parseError;
    // 必须保留纯空白文本节点，否则只含空格/换行的文本框会丢失内容。
    if (!xml.Load(stream, wxXMLDOC_KEEP_WHITESPACE_NODES, &parseError))
        return Fail(error, wxString::Format(wxS("XML 格式错误（第 %d 行，第 %d 列）：%s"),
            parseError.line, parseError.column + 1, parseError.message));
    if (xml.GetVersion() != wxS("1.0") || xml.GetFileEncoding().CmpNoCase(wxS("UTF-8")) != 0)
        return Fail(error, wxS("仅支持 XML 1.0、UTF-8 编码的电路文件。"));
    if (!xml.GetDoctype().GetRootName().empty())
        return Fail(error, wxS("电路文件不允许 DTD 或外部实体。"));
    const auto* root = xml.GetRoot();
    if (!root || root->GetName() != wxS("circuit"))
        return Fail(error, wxS("这不是受支持的电路文件：缺少 circuit 根元素。"));
    if (!CheckAttributes(*root, { "version" }, error)) return false;
    const auto version = root->GetAttribute(wxS("version"));
    if (version != wxS("1") && version != wxS("2") && version != wxS("3"))
        return Fail(error, wxS("不支持此电路文件版本，目前支持 version=1、2 或 3。"));
    for (auto* node = xml.GetDocumentNode()->GetChildren(); node; node = node->GetNext())
        if (node != root && !IsFormattingNode(*node))
            return Fail(error, wxS("电路文件包含不支持的顶层节点。"));
    const wxXmlNode* components = nullptr;
    const wxXmlNode* wires = nullptr;
    const wxXmlNode* texts = nullptr;
    const wxXmlNode* simulation = nullptr;
    for (auto* node = root->GetChildren(); node; node = node->GetNext())
    {
        if (IsFormattingNode(*node)) continue;
        if (node->GetType() != wxXML_ELEMENT_NODE)
            return Fail(error, wxS("circuit 中包含不支持的内容。"));
        const wxXmlNode** section = nullptr;
        if (node->GetName() == wxS("components")) section = &components;
        else if (node->GetName() == wxS("wires")) section = &wires;
        else if (node->GetName() == wxS("texts")) section = &texts;
        else if (node->GetName() == wxS("simulation") && version == wxS("3")) section = &simulation;
        else return Fail(error, wxString::Format(wxS("未知的电路元素 <%s>。"), node->GetName()));
        if (*section)
            return Fail(error, wxString::Format(wxS("电路元素 <%s> 不能重复。"), node->GetName()));
        *section = node;
    }
    if (!components || !wires || !texts)
        return Fail(error, wxS("电路文件必须包含 components、wires、texts 三个区域。"));
    if (version == wxS("3") && !simulation)
        return Fail(error, wxS("version=3 电路必须包含 simulation 配置。"));
    CircuitDocument loaded;
    loaded.formatVersion = version == wxS("1") ? 1 : (version == wxS("2") ? 2 : 3);
    if (simulation && !ParseSimulation(*simulation, loaded.simulation, error)) return false;
    if (!ParseComponents(*components, loaded, error) || !ParseWires(*wires, loaded, error) ||
        !ParseTexts(*texts, loaded, error) || !Validate(loaded, error)) return false;
    document = std::move(loaded);
    return true;
}
