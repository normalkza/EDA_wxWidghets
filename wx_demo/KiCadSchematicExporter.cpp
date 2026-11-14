
#include "KiCadSchematicExporter.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <string>
#include <vector>
#include <iterator>
#include <algorithm>
#include <map>
#include <stdexcept>
#include <cmath>

#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/geometry.h>


static std::string GenerateUUID()
{
    static std::mt19937_64 generator(
        static_cast<unsigned int>(std::random_device{}())
    );

    static const char* hex = "0123456789abcdef";

    std::string uuid;
    uuid.reserve(36);

    for (int i = 0; i < 36; ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
            uuid += '-';
        else
            uuid += hex[generator() % 16];
    }

    uuid[14] = '4';
    uuid[19] = "89ab"[generator() % 4];

    return uuid;
}



static std::string EscapeKiCadString(const std::string& value)
{
    std::string result;

    for (char c : value)
    {
        if (c == '\\' || c == '"')
            result += '\\';

        if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            continue;
        else
            result += c;
    }

    return result;
}


static bool ReadWholeFile(
    const std::string& filename,
    std::string& content)
{
    std::ifstream file(filename);

    if (!file.is_open())
        return false;

    content.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );

    return true;
}



static bool ExtractSymbolDefinition(
    const std::string& libraryContent,
    const std::string& symbolName,
    std::string& result)
{
    const std::string marker =
        "(symbol \"" + symbolName + "\"";

    size_t start = libraryContent.find(marker);

    if (start == std::string::npos)
        return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = start; i < libraryContent.size(); ++i)
    {
        char c = libraryContent[i];

        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;

            continue;
        }

        if (c == '"')
            inString = true;
        else if (c == '(')
            ++depth;
        else if (c == ')')
        {
            --depth;

            if (depth == 0)
            {
                result = libraryContent.substr(
                    start,
                    i - start + 1
                );

                return true;
            }
        }
    }

    return false;
}


static bool LoadLogicGateSymbols(
    const std::string& symbolLibraryPath,
    std::string& xorSymbol,
    std::string& andSymbol,
    std::string& orSymbol,
    std::string& error)
{
    std::string libraryContent;

    if (!ReadWholeFile(symbolLibraryPath, libraryContent))
    {
        error = "无法读取 KiCad 符号库文件。";
        return false;
    }

    if (!ExtractSymbolDefinition(
        libraryContent, "74LS86", xorSymbol))
    {
        error = "符号库中找不到 74LS86。";
        return false;
    }

    if (!ExtractSymbolDefinition(
        libraryContent, "74LS09", andSymbol))
    {
        error = "符号库中找不到 74LS09。";
        return false;
    }

    if (!ExtractSymbolDefinition(
        libraryContent, "74LS32", orSymbol))
    {
        error = "符号库中找不到 74LS32。";
        return false;
    }

    return true;
}


static bool SetLibrarySymbolName(
    std::string& symbol,
    const std::string& symbolName)
{
    const std::string oldName =
        "(symbol \"" + symbolName + "\"";

    const std::string newName =
        "(symbol \"74xx:" + symbolName + "\"";

    const size_t pos = symbol.find(oldName);

    if (pos == std::string::npos)
        return false;

    symbol.replace(pos, oldName.size(), newName);

    return true;
}


static bool PrepareLogicGateSymbols(
    const std::string& symbolLibraryPath,
    std::string& xorSymbol,
    std::string& andSymbol,
    std::string& orSymbol,
    std::string& error)
{
    if (!LoadLogicGateSymbols(
        symbolLibraryPath,
        xorSymbol,
        andSymbol,
        orSymbol,
        error))
    {
        return false;
    }

    if (!SetLibrarySymbolName(xorSymbol, "74LS86") ||
        !SetLibrarySymbolName(andSymbol, "74LS09") ||
        !SetLibrarySymbolName(orSymbol, "74LS32"))
    {
        error = "无法设置 KiCad 符号库名称。";
        return false;
    }

    return true;
}



static std::string WxStringToStdString(const wxString& value)
{
    wxScopedCharBuffer buffer = value.utf8_str();

    if (!buffer)
        return "";

    return std::string(buffer.data());
}


static bool IsValidSymbolLibrary(const wxString& path)
{
    if (!wxFileExists(path))
        return false;

    wxFileName fileName(path);

    return fileName.GetFullName().IsSameAs(
        "74xx.kicad_sym", false
    );
}


static wxString FindKiCadSymbolLibrary()
{
    std::vector<wxString> candidates;

    candidates.push_back(
        "C:\\Program Files\\KiCad\\10.0\\share\\kicad\\symbols\\74xx.kicad_sym"
    );

    candidates.push_back(
        "C:\\Program Files\\KiCad\\9.0\\share\\kicad\\symbols\\74xx.kicad_sym"
    );

    candidates.push_back(
        "C:\\Program Files\\KiCad\\8.0\\share\\kicad\\symbols\\74xx.kicad_sym"
    );

    candidates.push_back(
        "C:\\Program Files (x86)\\KiCad\\10.0\\share\\kicad\\symbols\\74xx.kicad_sym"
    );

    const wxString roots[] =
    {
        "C:\\Program Files\\KiCad",
        "C:\\Program Files (x86)\\KiCad"
    };

    for (const wxString& root : roots)
    {
        wxDir dir(root);

        if (!dir.IsOpened())
            continue;

        wxString folder;

        bool found = dir.GetFirst(
            &folder,
            wxEmptyString,
            wxDIR_DIRS
        );

        while (found)
        {
            wxString path =
                root + "\\" + folder +
                "\\share\\kicad\\symbols\\74xx.kicad_sym";

            candidates.push_back(path);

            found = dir.GetNext(&folder);
        }
    }

    for (const wxString& path : candidates)
    {
        if (IsValidSymbolLibrary(path))
            return path;
    }

    return wxEmptyString;
}


static bool ResolveSymbolLibraryPath(
    const std::string& inputPath,
    std::string& outputPath,
    std::string& error,
    wxWindow* parent)
{
    error.clear();
    outputPath.clear();

    wxString path = wxString::FromUTF8(inputPath.c_str());

    if (!path.empty() && wxDirExists(path))
    {
        path = wxFileName(
            path,
            "74xx.kicad_sym"
        ).GetFullPath();
    }

    // 优先使用传入路径
    if (IsValidSymbolLibrary(path))
    {
        outputPath = WxStringToStdString(path);
        return true;
    }

    // 自动搜索
    path = FindKiCadSymbolLibrary();

    if (!path.empty())
    {
        outputPath = WxStringToStdString(path);
        return true;
    }

    wxFileDialog dialog(
        parent,
        wxString::FromUTF8("请选择 KiCad 的 74xx.kicad_sym 文件"),
        wxEmptyString,
        wxT("74xx.kicad_sym"),
        wxT("KiCad 符号库 (*.kicad_sym)|*.kicad_sym"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );

    if (dialog.ShowModal() != wxID_OK)
    {
        error = "未选择 KiCad 符号库，导出已取消。";
        return false;
    }

    path = dialog.GetPath();

    if (!IsValidSymbolLibrary(path))
    {
        error = "选择的文件不是 74xx.kicad_sym。";
        return false;
    }

    outputPath = WxStringToStdString(path);

    return true;
}



static std::vector<std::string> GeneratePinUUIDs()
{
    std::vector<std::string> uuids;

    for (int i = 0; i < 14; ++i)
        uuids.push_back(GenerateUUID());

    return uuids;
}



static void WriteLogicGateInstance(
    std::ofstream& file,
    const std::string& libraryId,
    const std::string& value,
    const std::string& reference,
    int unit,
    double x,
    double y,
    const std::string& sheetUuid,
    const std::vector<std::string>& pinUuids)
{
    file << "    (symbol\n";

    file << "        (lib_id \"" << libraryId << "\")\n";
    file << "        (at " << x << " " << y << " 0)\n";
    file << "        (unit " << unit << ")\n";
    file << "        (body_style 1)\n";
    file << "        (exclude_from_sim no)\n";
    file << "        (in_bom yes)\n";
    file << "        (on_board yes)\n";
    file << "        (in_pos_files yes)\n";
    file << "        (dnp no)\n";
    file << "        (fields_autoplaced yes)\n";

    file << "        (uuid \"" << GenerateUUID() << "\")\n";

    file << "        (property \"Reference\" \"" << reference << "\"\n";
    file << "            (at " << x << " " << y - 8.89 << " 0)\n";
    file << "            (effects (font (size 1.27 1.27)))\n";
    file << "        )\n";

    file << "        (property \"Value\" \"" << value << "\"\n";
    file << "            (at " << x << " " << y - 6.35 << " 0)\n";
    file << "            (effects (font (size 1.27 1.27)))\n";
    file << "        )\n";

    file << "        (property \"Footprint\" "
        << "\"Package_DIP:DIP-14_W7.62mm\"\n";
    file << "            (at " << x << " " << y << " 0)\n";
    file << "            (hide yes)\n";
    file << "            (effects (font (size 1.27 1.27)))\n";
    file << "        )\n";

    file << "        (property \"Datasheet\" \"\"\n";
    file << "            (at " << x << " " << y << " 0)\n";
    file << "            (hide yes)\n";
    file << "            (effects (font (size 1.27 1.27)))\n";
    file << "        )\n";

    for (int i = 0; i < 14; ++i)
    {
        file << "        (pin \"" << i + 1 << "\"\n";
        file << "            (uuid \"" << pinUuids[i] << "\")\n";
        file << "        )\n";
    }

    file << "        (instances\n";
    file << "            (project \"FullAdder\"\n";
    file << "                (path \"/" << sheetUuid << "\"\n";
    file << "                    (reference \"" << reference << "\")\n";
    file << "                    (unit " << unit << ")\n";
    file << "                )\n";
    file << "            )\n";
    file << "        )\n";

    file << "    )\n";
}


struct GatePinMapping
{
    std::string reference;

    int unit = 1;

    int inputA = 0;
    int inputB = 0;
    int output = 0;

    double x = 0.0;
    double y = 0.0;
};

struct PortPinMapping
{
    double x = 0.0;
    double y = 0.0;
    std::string label;
    bool isInput = false;
};

static GatePinMapping CreateGatePinMapping(
    const std::string& reference,
    int unit,
    double x,
    double y)
{
    GatePinMapping mapping;

    mapping.reference = reference;
    mapping.unit = unit;
    mapping.x = x;
    mapping.y = y;

    switch (unit)
    {
    case 1:
        mapping.inputA = 1;
        mapping.inputB = 2;
        mapping.output = 3;
        break;

    case 2:
        mapping.inputA = 4;
        mapping.inputB = 5;
        mapping.output = 6;
        break;

    case 3:
        mapping.inputA = 9;
        mapping.inputB = 10;
        mapping.output = 8;
        break;

    case 4:
        mapping.inputA = 12;
        mapping.inputB = 13;
        mapping.output = 11;
        break;

    default:
        throw std::runtime_error("无效的芯片单元编号");
    }

    return mapping;
}



struct KiCadPinPosition
{
    int pinNumber;
    double x;
    double y;
};


static KiCadPinPosition Get74LS86PinPosition(
    int unit,
    int pinNumber)
{
    static const std::map<
        int,
        std::map<int, std::pair<double, double>>
    > pins =
    {
        {
            1,
            {
                {1, {-7.62,  2.54}},
                {2, {-7.62, -2.54}},
                {3, { 7.62,  0.00}}
            }
        },
        {
            2,
            {
                {4, {-7.62,  2.54}},
                {5, {-7.62, -2.54}},
                {6, { 7.62,  0.00}}
            }
        },
        {
            3,
            {
                {9,  {-7.62,  2.54}},
                {10, {-7.62, -2.54}},
                {8,  { 7.62,  0.00}}
            }
        },
        {
            4,
            {
                {12, {-7.62,  2.54}},
                {13, {-7.62, -2.54}},
                {11, { 7.62,  0.00}}
            }
        }
    };

    auto unitIt = pins.find(unit);

    if (unitIt == pins.end())
        throw std::runtime_error("无效的 74LS86 Unit");

    auto pinIt = unitIt->second.find(pinNumber);

    if (pinIt == unitIt->second.end())
        throw std::runtime_error("无效的 74LS86 引脚编号");

    return {
        pinNumber,
        pinIt->second.first,
        pinIt->second.second
    };
}


// 库坐标转换为原理图坐标
static wxPoint2DDouble TransformKiCadPinPosition(
    double symbolX,
    double symbolY,
    double pinX,
    double pinY)
{
    return wxPoint2DDouble(
        symbolX + pinX,
        symbolY - pinY
    );
}


static wxPoint2DDouble GetGatePinWorldPosition(
    const GatePinMapping& mapping,
    int pinNumber)
{
    KiCadPinPosition pin =
        Get74LS86PinPosition(mapping.unit, pinNumber);

    return TransformKiCadPinPosition(
        mapping.x,
        mapping.y,
        pin.x,
        pin.y
    );
}



static int GetGatePhysicalPin(
    const GatePinMapping& mapping,
    int logicalPin,
    bool isOutput)
{
    if (isOutput)
    {
        if (logicalPin != 0)
            throw std::runtime_error("输出引脚索引无效");

        return mapping.output;
    }

    if (logicalPin == 0)
        return mapping.inputA;

    if (logicalPin == 1)
        return mapping.inputB;

    throw std::runtime_error("目前只支持双输入逻辑门");
}



static void WriteKiCadWire(
    std::ofstream& file,
    const wxPoint2DDouble& start,
    const wxPoint2DDouble& end)
{
    const std::string uuid = GenerateUUID();

    file << "    (wire\n";
    file << "        (pts\n";

    file << "            (xy "
        << start.m_x << " " << start.m_y << ")\n";

    file << "            (xy "
        << end.m_x << " " << end.m_y << ")\n";

    file << "        )\n";
    file << "        (stroke (width 0) (type default))\n";
    file << "        (uuid \"" << uuid << "\")\n";
    file << "    )\n";
}


static void WriteKiCadLabel(
    std::ofstream& file,
    const std::string& label,
    double x,
    double y)
{
    file << "    (label \"" << EscapeKiCadString(label) << "\"\n";
    file << "        (at " << x << " " << y << " 0)\n";
    file << "        (effects (font (size 1.27 1.27)) "
        << "(justify left bottom))\n";
    file << "        (uuid \"" << GenerateUUID() << "\")\n";
    file << "    )\n";
}


static void CreatePortMappings(
    const CircuitDocument& document,
    const std::map<int, GatePinMapping>& pinMappings,
    std::map<int, PortPinMapping>& portMappings)
{
    const double inputOffset = 2.54;
    const double outputOffset = 2.54;

    for (const auto& component : document.components)
    {
        if (component.type != "INPUT" &&
            component.type != "OUTPUT")
        {
            continue;
        }

        PortPinMapping mapping;
        mapping.isInput = (component.type == "INPUT");

        bool foundConnection = false;

        for (const auto& wire : document.wires)
        {
            // INPUT -> 逻辑门
            if (mapping.isInput &&
                wire.sourceId == component.id)
            {
                auto gateIt = pinMappings.find(wire.targetId);

                if (gateIt == pinMappings.end())
                    continue;

                int physicalPin = GetGatePhysicalPin(
                    gateIt->second,
                    wire.targetPin,
                    false
                );

                wxPoint2DDouble gatePosition =
                    GetGatePinWorldPosition(
                        gateIt->second,
                        physicalPin
                    );

                mapping.x = gatePosition.m_x - inputOffset;
                mapping.y = gatePosition.m_y;
                mapping.label =
                    "INPUT_" + std::to_string(component.id);

                foundConnection = true;
                break;
            }

            // 逻辑门 -> OUTPUT
            if (!mapping.isInput &&
                wire.targetId == component.id)
            {
                auto gateIt = pinMappings.find(wire.sourceId);

                if (gateIt == pinMappings.end())
                    continue;

                int physicalPin = GetGatePhysicalPin(
                    gateIt->second,
                    wire.sourcePin,
                    true
                );

                wxPoint2DDouble gatePosition =
                    GetGatePinWorldPosition(
                        gateIt->second,
                        physicalPin
                    );

                mapping.x = gatePosition.m_x + outputOffset;
                mapping.y = gatePosition.m_y;
                mapping.label =
                    "OUTPUT_" + std::to_string(component.id);

                foundConnection = true;
                break;
            }
        }

        // 未连接的端口放在页面左侧或右侧
        if (!foundConnection)
        {
            mapping.x = mapping.isInput ? 40.0 : 220.0;
            mapping.y = 40.0 + component.id % 10 * 10.0;

            mapping.label =
                (mapping.isInput ? "INPUT_" : "OUTPUT_") +
                std::to_string(component.id);
        }

        portMappings[component.id] = mapping;
    }
}

// 写入所有端口标签
static void WritePortLabels(
    std::ofstream& file,
    const std::map<int, PortPinMapping>& portMappings)
{
    for (const auto& item : portMappings)
    {
        const PortPinMapping& mapping = item.second;

        WriteKiCadLabel(
            file,
            mapping.label,
            mapping.x,
            mapping.y
        );
    }
}


static bool WriteDocumentGateWires(
    std::ofstream& file,
    const CircuitDocument& document,
    const std::map<int, GatePinMapping>& pinMappings,
    const std::map<int, PortPinMapping>& portMappings,
    std::string& error)
{
    for (const auto& wire : document.wires)
    {
        try
        {
            wxPoint2DDouble start;
            wxPoint2DDouble end;

            // 查找源端和目标端
            auto sourceGate = pinMappings.find(wire.sourceId);
            auto targetGate = pinMappings.find(wire.targetId);

            auto sourcePort = portMappings.find(wire.sourceId);
            auto targetPort = portMappings.find(wire.targetId);

            // 情况一：INPUT -> 逻辑门
            if (sourcePort != portMappings.end() &&
                targetGate != pinMappings.end())
            {
                const PortPinMapping& source =
                    sourcePort->second;

                if (!source.isInput)
                {
                    error = "连线方向错误：OUTPUT 不能作为连线源端。";
                    return false;
                }

                const GatePinMapping& target =
                    targetGate->second;

                int targetPin = GetGatePhysicalPin(
                    target,
                    wire.targetPin,
                    false
                );

                start = wxPoint2DDouble(
                    source.x,
                    source.y
                );

                end = GetGatePinWorldPosition(
                    target,
                    targetPin
                );

                WriteKiCadWire(file, start, end);
            }

            // 情况二：逻辑门 -> OUTPUT
            else if (sourceGate != pinMappings.end() &&
                targetPort != portMappings.end())
            {
                const PortPinMapping& target =
                    targetPort->second;

                if (target.isInput)
                {
                    error = "连线方向错误：INPUT 不能作为连线目标端。";
                    return false;
                }

                const GatePinMapping& source =
                    sourceGate->second;

                int sourcePin = GetGatePhysicalPin(
                    source,
                    wire.sourcePin,
                    true
                );

                start = GetGatePinWorldPosition(
                    source,
                    sourcePin
                );

                end = wxPoint2DDouble(
                    target.x,
                    target.y
                );

                WriteKiCadWire(file, start, end);
            }

            // 情况三：逻辑门 -> 逻辑门
            else if (sourceGate != pinMappings.end() &&
                targetGate != pinMappings.end())
            {
                const GatePinMapping& source =
                    sourceGate->second;

                const GatePinMapping& target =
                    targetGate->second;

                int sourcePin = GetGatePhysicalPin(
                    source,
                    wire.sourcePin,
                    true
                );

                int targetPin = GetGatePhysicalPin(
                    target,
                    wire.targetPin,
                    false
                );

                start = GetGatePinWorldPosition(
                    source,
                    sourcePin
                );

                end = GetGatePinWorldPosition(
                    target,
                    targetPin
                );

                WriteKiCadWire(file, start, end);
            }
            else
            {
                error =
                    "发现暂不支持的连线类型，元件 ID：" +
                    std::to_string(wire.sourceId) + " -> " +
                    std::to_string(wire.targetId);

                return false;
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    return true;
}



static void WriteGateGroup(
    std::ofstream& file,
    std::vector<const CircuitComponentData*>& gates,
    const std::string& libraryId,
    const std::string& value,
    int& nextReference,
    double baseX,
    const std::string& sheetUuid,
    std::map<int, GatePinMapping>& pinMappings)
{
    std::sort(
        gates.begin(),
        gates.end(),
        [](const CircuitComponentData* a,
            const CircuitComponentData* b)
        {
            return a->id < b->id;
        }
    );

    std::string currentReference;
    std::vector<std::string> currentPinUuids;

    for (size_t i = 0; i < gates.size(); ++i)
    {
        // 每片芯片最多4个逻辑门
        int unit = static_cast<int>(i % 4) + 1;

        int chipIndex = static_cast<int>(i / 4);

        double x = baseX + chipIndex * 45.0;
        double y = 60.0 + (unit - 1) * 30.0;

        if (unit == 1)
        {
            currentReference =
                "U" + std::to_string(nextReference++);

            currentPinUuids = GeneratePinUUIDs();
        }

        pinMappings[gates[i]->id] =
            CreateGatePinMapping(
                currentReference,
                unit,
                x,
                y
            );

        WriteLogicGateInstance(
            file,
            libraryId,
            value,
            currentReference,
            unit,
            x,
            y,
            sheetUuid,
            currentPinUuids
        );
    }
}


bool KiCadSchematicExporter::ExportSchematic(
    const CircuitDocument& document,
    const std::string& filename,
    const std::string& symbolLibraryPath,
    std::string& error,
    wxWindow* parent)
{
    error.clear();

    // 1. 检查符号库路径
    std::string actualSymbolLibraryPath;

    if (!ResolveSymbolLibraryPath(
        symbolLibraryPath,
        actualSymbolLibraryPath,
        error,
        parent))
    {
        return false;
    }

    // 2. 读取逻辑门符号
    std::string xorSymbol;
    std::string andSymbol;
    std::string orSymbol;

    if (!PrepareLogicGateSymbols(
        actualSymbolLibraryPath,
        xorSymbol,
        andSymbol,
        orSymbol,
        error))
    {
        return false;
    }

    // 3. 检查逻辑门输入数量
    for (const auto& component : document.components)
    {
        if (component.type == "XOR" ||
            component.type == "AND" ||
            component.type == "OR")
        {
            if (component.inputCount != 2)
            {
                error =
                    "目前只支持双输入逻辑门，元件 ID：" +
                    std::to_string(component.id);

                return false;
            }
        }
    }

    // 4. 创建原理图文件
    std::ofstream file(filename, std::ios::binary);

    if (!file.is_open())
    {
        error = "无法创建 KiCad 原理图文件。";
        return false;
    }

    file << std::fixed << std::setprecision(4);

    // 5. 生成原理图 UUID
    const std::string sheetUuid = GenerateUUID();

    // 6. 写入原理图文件头
    file << "(kicad_sch\n";
    file << "    (version 20260306)\n";
    file << "    (generator \"eeschema\")\n";
    file << "    (generator_version \"10.0\")\n";
    file << "    (uuid \"" << sheetUuid << "\")\n";
    file << "    (paper \"A4\")\n";

    // 7. 写入符号库定义
    file << "    (lib_symbols\n";

    file << xorSymbol << "\n";
    file << andSymbol << "\n";
    file << orSymbol << "\n";

    file << "    )\n";

    // 8. 收集逻辑门
    std::vector<const CircuitComponentData*> xorGates;
    std::vector<const CircuitComponentData*> andGates;
    std::vector<const CircuitComponentData*> orGates;

    for (const auto& component : document.components)
    {
        if (component.type == "XOR")
            xorGates.push_back(&component);
        else if (component.type == "AND")
            andGates.push_back(&component);
        else if (component.type == "OR")
            orGates.push_back(&component);
    }

    // 9. 分配芯片和逻辑门引脚映射
    int nextReference = 1;

    std::map<int, GatePinMapping> pinMappings;
    std::map<int, PortPinMapping> portMappings;

    try
    {
        WriteGateGroup(
            file,
            xorGates,
            "74xx:74LS86",
            "74LS86",
            nextReference,
            80.0,
            sheetUuid,
            pinMappings
        );

        WriteGateGroup(
            file,
            andGates,
            "74xx:74LS09",
            "74LS09",
            nextReference,
            130.0,
            sheetUuid,
            pinMappings
        );

        WriteGateGroup(
            file,
            orGates,
            "74xx:74LS32",
            "74LS32",
            nextReference,
            180.0,
            sheetUuid,
            pinMappings
        );
    }
    catch (const std::exception& e)
    {
        file.close();
        error = e.what();
        return false;
    }

    // 10. 根据逻辑门实际引脚位置计算端口位置
    CreatePortMappings(
        document,
        pinMappings,
        portMappings
    );

    // 11. 写入 INPUT、OUTPUT 标签
    WritePortLabels(file, portMappings);

    // 12. 写入所有支持的导线
    if (!WriteDocumentGateWires(
        file,
        document,
        pinMappings,
        portMappings,
        error))
    {
        file.close();
        return false;
    }

    // 13. 页面信息
    file << "    (sheet_instances\n";
    file << "        (path \"/\"\n";
    file << "            (page \"1\")\n";
    file << "        )\n";
    file << "    )\n";

    file << "    (embedded_fonts no)\n";

    // 14. 结束原理图
    file << ")\n";

    // 15. 关闭文件并检查
    file.close();

    if (!file)
    {
        error = "写入 KiCad 原理图文件失败。";
        return false;
    }

    return true;
}
