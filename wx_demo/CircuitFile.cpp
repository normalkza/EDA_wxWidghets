#include <wx/wx.h>
#include "CircuitFile.h"
#include "AndGate.h"
#include "Circuit.h"
#include "IOComponent.h"
#include "NandGate.h"
#include "NorGate.h"
#include "NotGate.h"
#include "OrGate.h"
#include "XorGate.h"

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
#include <algorithm>
#include <initializer_list>
#include <queue>
#include <unordered_map>
#include <utility>

namespace
{
// 限制原始文件和解析后的对象，避免损坏文件制造大量窗口或超大坐标。
constexpr std::size_t MaxFileBytes = 8 * 1024 * 1024;
constexpr std::size_t MaxComponents = 1000;
constexpr std::size_t MaxWires = 4000;
constexpr std::size_t MaxTexts = 1000;
constexpr std::size_t MaxTextLength = 65536;
constexpr std::size_t MaxTotalTextLength = 262144;
constexpr int MaxCoordinate = 1000000;
constexpr int MaxDimension = 1000000;
constexpr int MaxXmlDepth = 8;
constexpr std::size_t MaxXmlNodes = 16000;

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

bool IsCoordinate(int value)
{
    return value >= -MaxCoordinate && value <= MaxCoordinate;
}

bool IsValidXmlText(const wxString& text)
{
    for (auto character : text)
    {
        const auto value = character.GetValue();
        if (value != 9 && value != 10 && value != 13 &&
            !(value >= 0x20 && value <= 0xd7ff) &&
            !(value >= 0xe000 && value <= 0xfffd) &&
            !(value >= 0x10000 && value <= 0x10ffff))
            return false;
    }
    return true;
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
                     std::initializer_list<const char*> required, wxString& error)
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
        if (!CheckAttributes(*node, { "id", "type", "x", "y", "inputCount", "high" }, error) ||
            !HasNoDataChildren(*node, error)) return false;
        CircuitComponentData data;
        data.type = node->GetAttribute(wxS("type")).ToStdString(wxConvUTF8);
        if (!ReadInt(*node, "id", data.id, error) || !ReadInt(*node, "x", data.x, error) ||
            !ReadInt(*node, "y", data.y, error) ||
            !ReadInt(*node, "inputCount", data.inputCount, error)) return false;
        const wxString high = node->GetAttribute(wxS("high"));
        if (high != wxS("0") && high != wxS("1"))
            return Fail(error, wxS("元件的 high 属性只能是 0 或 1。"));
        data.high = high == wxS("1");
        document.components.push_back(std::move(data));
    }
    return true;
}

bool ParseWires(const wxXmlNode& section, CircuitDocument& document, wxString& error)
{
    if (!CheckAttributes(section, {}, error)) return false;
    for (auto* node = section.GetChildren(); node; node = node->GetNext())
    {
        if (IsFormattingNode(*node)) continue;
        if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != wxS("wire"))
            return Fail(error, wxS("wires 中只能包含 wire 连线。"));
        if (document.wires.size() >= MaxWires)
            return Fail(error, wxS("连线数量超过上限（4000）。"));
        if (!CheckAttributes(*node, { "sourceId", "sourcePin", "targetId", "targetPin" }, error) ||
            !HasNoDataChildren(*node, error)) return false;
        CircuitWireData data;
        if (!ReadInt(*node, "sourceId", data.sourceId, error) ||
            !ReadInt(*node, "sourcePin", data.sourcePin, error) ||
            !ReadInt(*node, "targetId", data.targetId, error) ||
            !ReadInt(*node, "targetPin", data.targetPin, error)) return false;
        document.wires.push_back(data);
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
    if (data.type == "INPUT")
    {
        if (data.inputCount != 0) return nullptr;
        auto component = std::make_unique<InputComponent>();
        component->outputs[0].value = data.high ? LogicValue::High : LogicValue::Low;
        return component;
    }
    if (data.high) return nullptr;
    if (data.type == "OUTPUT")
        return data.inputCount == 1 ? std::make_unique<OutputComponent>() : nullptr;
    if (data.type == "NOT")
        return data.inputCount == 1 ? std::make_unique<NotGate>() : nullptr;
    if (data.inputCount < MultiInputGate::MinInputCount || data.inputCount > MultiInputGate::MaxInputCount)
        return nullptr;
    if (data.type == "AND") return std::make_unique<AndGate>(data.inputCount);
    if (data.type == "OR") return std::make_unique<OrGate>(data.inputCount);
    if (data.type == "NAND") return std::make_unique<NandGate>(data.inputCount);
    if (data.type == "NOR") return std::make_unique<NorGate>(data.inputCount);
    if (data.type == "XOR") return std::make_unique<XorGate>(data.inputCount);
    return nullptr;
}

bool CircuitFile::Validate(const CircuitDocument& document, wxString& error)
{
    error.clear();
    if (document.components.size() > MaxComponents || document.wires.size() > MaxWires ||
        document.texts.size() > MaxTexts)
        return Fail(error, wxS("电路过大：最多允许 1000 个元件、4000 条连线、1000 个文本框。"));
    std::unordered_map<int, std::size_t> indices;
    std::vector<std::unique_ptr<Component>> components;
    components.reserve(document.components.size());
    for (const auto& data : document.components)
    {
        if (data.id <= 0 || !indices.emplace(data.id, components.size()).second)
            return Fail(error, wxS("元件 id 必须为正整数且不能重复。"));
        if (!IsCoordinate(data.x) || !IsCoordinate(data.y))
            return Fail(error, wxS("元件坐标必须在 -1000000 至 1000000 之间。"));
        auto component = CreateComponent(data);
        if (!component)
            return Fail(error, wxString::Format(wxS("元件 %d 的类型、输入数量或电平无效。"), data.id));
        components.push_back(std::move(component));
    }
    std::size_t totalLength = 0;
    for (const auto& data : document.texts)
    {
        if (!IsCoordinate(data.rect.x) || !IsCoordinate(data.rect.y) ||
            data.rect.width <= 0 || data.rect.width > MaxDimension ||
            data.rect.height <= 0 || data.rect.height > MaxDimension)
            return Fail(error, wxS("文本框坐标或尺寸无效（坐标范围 ±1000000，宽高范围 1 至 1000000）。"));
        if (data.pointSize < 6 || data.pointSize > 96)
            return Fail(error, wxS("文本字号必须在 6 至 96 之间。"));
        totalLength += data.text.length();
        if (data.text.length() > MaxTextLength || totalLength > MaxTotalTextLength)
            return Fail(error, wxS("文本过长：单框最多 65536 个字符，全部文本最多 262144 个字符。"));
        if (!IsValidXmlText(data.text))
            return Fail(error, wxS("文本包含 XML 1.0 不支持的控制字符或无效 Unicode 字符。"));
    }
    std::vector<std::vector<std::size_t>> outgoing(components.size());
    std::vector<std::size_t> indegree(components.size(), 0);
    for (const auto& wire : document.wires)
    {
        const auto source = indices.find(wire.sourceId), target = indices.find(wire.targetId);
        if (source == indices.end() || target == indices.end())
            return Fail(error, wxS("连线引用了不存在的元件。"));
        if (wire.sourcePin < 0 || wire.targetPin < 0 ||
            static_cast<std::size_t>(wire.sourcePin) >= components[source->second]->outputs.size() ||
            static_cast<std::size_t>(wire.targetPin) >= components[target->second]->inputs.size())
            return Fail(error, wxS("连线引脚越界，源端必须是输出引脚，目标端必须是输入引脚。"));
        outgoing[source->second].push_back(target->second);
        ++indegree[target->second];
    }
    std::queue<std::size_t> ready;
    for (std::size_t index = 0; index < indegree.size(); ++index)
        if (indegree[index] == 0) ready.push(index);
    std::vector<std::size_t> order(components.size());
    std::size_t visited = 0;
    while (!ready.empty())
    {
        const auto source = ready.front();
        ready.pop();
        order[source] = visited++;
        for (const auto target : outgoing[source])
            if (--indegree[target] == 0) ready.push(target);
    }
    if (visited != components.size())
        return Fail(error, wxS("电路包含反馈环路，当前组合逻辑电路不支持环路。"));
    std::vector<std::size_t> wireOrder(document.wires.size());
    for (std::size_t index = 0; index < wireOrder.size(); ++index) wireOrder[index] = index;
    // 先连接拓扑靠前的源端，Connect 的环路检查不必反复遍历整条链。
    std::stable_sort(wireOrder.begin(), wireOrder.end(), [&](std::size_t left, std::size_t right) {
        return order[indices.at(document.wires[left].sourceId)] <
            order[indices.at(document.wires[right].sourceId)];
    });
    Circuit circuit;
    for (const auto index : wireOrder)
    {
        const auto& wire = document.wires[index];
        const auto result = circuit.Connect(
            { components[indices.at(wire.sourceId)].get(), PinType::Output, static_cast<std::size_t>(wire.sourcePin) },
            { components[indices.at(wire.targetId)].get(), PinType::Input, static_cast<std::size_t>(wire.targetPin) });
        if (result == ConnectionResult::Duplicate)
            return Fail(error, wxS("电路包含重复连线。"));
        if (result == ConnectionResult::InputOccupied)
            return Fail(error, wxS("同一个输入引脚不能连接多个信号源。"));
        if (result != ConnectionResult::Added)
            return Fail(error, wxS("电路包含无效连线或反馈环路。"));
    }
    return true;
}

bool CircuitFile::Save(const wxString& path, const CircuitDocument& document, wxString& error)
{
    error.clear();
    if (path.empty()) return Fail(error, wxS("保存路径不能为空。"));
    if (!Validate(document, error)) return false;
    wxXmlDocument xml;
    xml.SetVersion(wxS("1.0"));
    xml.SetFileEncoding(wxS("UTF-8"));
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, wxS("circuit"));
    root->AddAttribute(wxS("version"), wxS("1"));
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
    }
    auto* wires = AddElement(*root, wxS("wires"));
    for (const auto& data : document.wires)
    {
        auto* node = AddElement(*wires, wxS("wire"));
        AddInt(*node, "sourceId", data.sourceId);
        AddInt(*node, "sourcePin", data.sourcePin);
        AddInt(*node, "targetId", data.targetId);
        AddInt(*node, "targetPin", data.targetPin);
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
    if (root->GetAttribute(wxS("version")) != wxS("1"))
        return Fail(error, wxS("不支持此电路文件版本，目前仅支持 version=1。"));
    for (auto* node = xml.GetDocumentNode()->GetChildren(); node; node = node->GetNext())
        if (node != root && !IsFormattingNode(*node))
            return Fail(error, wxS("电路文件包含不支持的顶层节点。"));
    const wxXmlNode* components = nullptr;
    const wxXmlNode* wires = nullptr;
    const wxXmlNode* texts = nullptr;
    for (auto* node = root->GetChildren(); node; node = node->GetNext())
    {
        if (IsFormattingNode(*node)) continue;
        if (node->GetType() != wxXML_ELEMENT_NODE)
            return Fail(error, wxS("circuit 中包含不支持的内容。"));
        const wxXmlNode** section = nullptr;
        if (node->GetName() == wxS("components")) section = &components;
        else if (node->GetName() == wxS("wires")) section = &wires;
        else if (node->GetName() == wxS("texts")) section = &texts;
        else return Fail(error, wxString::Format(wxS("未知的电路元素 <%s>。"), node->GetName()));
        if (*section)
            return Fail(error, wxString::Format(wxS("电路元素 <%s> 不能重复。"), node->GetName()));
        *section = node;
    }
    if (!components || !wires || !texts)
        return Fail(error, wxS("电路文件必须包含 components、wires、texts 三个区域。"));
    CircuitDocument loaded;
    if (!ParseComponents(*components, loaded, error) || !ParseWires(*wires, loaded, error) ||
        !ParseTexts(*texts, loaded, error) || !Validate(loaded, error)) return false;
    document = std::move(loaded);
    return true;
}
