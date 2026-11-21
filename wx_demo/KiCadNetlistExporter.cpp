#include "KiCadNetlistExporter.h"

#include "Connectivity.h"
#include "CircuitValidation.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <wx/file.h>
#include <wx/filename.h>

namespace
{
    struct GateMapping
    {
        std::string reference;
        int inputA = 0;
        int inputB = 0;
        int output = 0;
        std::string value;
        std::string library;
        std::string description;
        std::string footprint;
    };

    std::string EscapeSExpr(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (char ch : value)
        {
            if (ch == '\\' || ch == '"') result.push_back('\\');
            if (ch == '\n') result += "\\n";
            else if (ch == '\r') result += "\\r";
            else result.push_back(ch);
        }
        return result;
    }

    std::string MakeUuid(int id, int salt)
    {
        // 生成稳定且符合 UUID 外观的标识；同一元件重复导出时保持一致。
        const unsigned int a = 0x6a09e667u ^ static_cast<unsigned int>(id * 2654435761u);
        const unsigned int b = 0xbb67ae85u ^ static_cast<unsigned int>(salt * 2246822519u);
        const unsigned int c = 0x3c6ef372u ^ static_cast<unsigned int>((id + salt) * 3266489917u);
        const unsigned int d = 0xa54ff53au ^ static_cast<unsigned int>((id * 31 + salt) * 668265263u);
        char buffer[37] = {};
        std::snprintf(buffer, sizeof(buffer), "%08x-%04x-%04x-%04x-%08x%04x",
            a, (b >> 16) & 0xffff, b & 0xffff, (c >> 16) & 0xffff,
            c & 0xffffffffu, d & 0xffff);
        return buffer;
    }

    bool WriteDocumentAsKiCadNetlist(
        const CircuitDocument& document,
        const std::string& filename,
        std::string& error)
    {
        std::vector<const CircuitComponentData*> inputs;
        std::vector<const CircuitComponentData*> outputs;
        std::vector<const CircuitComponentData*> xors;
        std::vector<const CircuitComponentData*> ands;
        std::vector<const CircuitComponentData*> ors;

        for (const auto& component : document.components)
        {
            if (component.type == "INPUT") inputs.push_back(&component);
            else if (component.type == "OUTPUT") outputs.push_back(&component);
            else if (component.type == "XOR") xors.push_back(&component);
            else if (component.type == "AND") ands.push_back(&component);
            else if (component.type == "OR") ors.push_back(&component);
            else
            {
                error = "KiCad 网表目前只支持全加器所需的 INPUT、OUTPUT、XOR、AND、OR 元件；发现不支持的元件：" + component.type;
                return false;
            }
        }

        auto sortById = [](auto& items)
        {
            std::sort(items.begin(), items.end(), [](const auto* a, const auto* b)
            {
                return a->id < b->id;
            });
        };
        sortById(inputs);
        sortById(outputs);
        sortById(xors);
        sortById(ands);
        sortById(ors);

        if (inputs.size() != 3 || outputs.size() != 2 ||
            xors.size() != 2 || ands.size() != 2 || ors.size() != 1)
        {
            error = "当前自定义 KiCad 网表导出针对一位全加器：需要 3 个输入、2 个输出、2 个 XOR、2 个 AND 和 1 个 OR。";
            return false;
        }

        // 逻辑门映射到 74 系列芯片的物理引脚。
        std::map<int, GateMapping> mapping;
        mapping[xors[0]->id] = { "U1", 1, 2, 3, "74LS86", "74xx", "Quad 2-input XOR", "Package_DIP:DIP-14_W7.62mm" };
        mapping[xors[1]->id] = { "U1", 4, 5, 6, "74LS86", "74xx", "Quad 2-input XOR", "Package_DIP:DIP-14_W7.62mm" };
        mapping[ands[0]->id] = { "U2", 1, 2, 3, "74LS09", "74xx", "Quad 2-input AND Open Collector", "Package_DIP:DIP-14_W7.62mm" };
        mapping[ands[1]->id] = { "U2", 4, 5, 6, "74LS09", "74xx", "Quad 2-input AND Open Collector", "Package_DIP:DIP-14_W7.62mm" };
        mapping[ors[0]->id] = { "U3", 1, 2, 3, "74LS32", "74xx", "Quad 2-input OR", "Package_DIP:DIP-14_W7.62mm" };

        std::vector<NetPin> pins;
        std::vector<NetLink> links;
        std::vector<ElectricalNet> nets;

        for (const auto& component : document.components)
        {
            if (component.type == "INPUT")
                pins.push_back({ component.id, PinType::Output, 0 });
            else if (component.type == "OUTPUT")
                pins.push_back({ component.id, PinType::Input, 0 });
            else
            {
                pins.push_back({ component.id, PinType::Input, 0 });
                pins.push_back({ component.id, PinType::Input, 1 });
                pins.push_back({ component.id, PinType::Output, 0 });
            }
        }

        for (const auto& wire : document.wires)
        {
            if (wire.sourcePin < 0 || wire.targetPin < 0)
            {
                error = "连线包含无效的引脚编号。";
                return false;
            }
            links.push_back({
                { wire.sourceId, PinType::Output, static_cast<std::size_t>(wire.sourcePin) },
                { wire.targetId, PinType::Input, static_cast<std::size_t>(wire.targetPin) }
            });
        }

        if (!ConnectivityBuilder::Build(pins, links, nets, error))
            return false;

        std::map<int, std::string> portNames;
        portNames[inputs[0]->id] = "inputA";
        portNames[inputs[1]->id] = "inputB";
        portNames[inputs[2]->id] = "Cin";
        portNames[outputs[0]->id] = "Sum";
        portNames[outputs[1]->id] = "Cout";

        std::ostringstream out;
        out << "(export\n";
        out << "  (version \"E\")\n";
        out << "  (design\n";
        out << "    (source \"DigitalCircuitEditor\")\n";
        out << "    (date \"\")\n";
        out << "    (tool \"DigitalCircuitEditor\")\n";
        out << "  )\n";
        out << "  (components\n";

        const struct Chip { const char* ref; const char* value; const char* description; } chips[] = {
            { "U1", "74LS86", "Quad 2-input XOR" },
            { "U2", "74LS09", "Quad 2-input AND Open Collector" },
            { "U3", "74LS32", "Quad 2-input OR" }
        };

        for (int i = 0; i < 3; ++i)
        {
            const std::string uuid = MakeUuid(i + 1, 0);
            out << "    (comp\n";
            out << "      (ref \"" << chips[i].ref << "\")\n";
            out << "      (value \"" << chips[i].value << "\")\n";
            out << "      (footprint \"Package_DIP:DIP-14_W7.62mm\")\n";
            out << "      (datasheet \"\")\n";
            out << "      (description \"" << chips[i].description << "\")\n";
            out << "      (fields\n";
            out << "        (field (name \"Footprint\") \"Package_DIP:DIP-14_W7.62mm\")\n";
            out << "      )\n";
            out << "      (libsource (lib \"74xx\") (part \"" << chips[i].value << "\") (description \"" << chips[i].description << "\"))\n";
            out << "      (property (name \"Sheetname\") (value \"/\"))\n";
            out << "      (property (name \"Sheetfile\") (value \"DigitalCircuitEditor.kicad_sch\"))\n";
            out << "      (sheetpath (names \"/\") (tstamps \"/\"))\n";
            out << "      (tstamps \"" << uuid << "\")\n";
            out << "    )\n";
        }

        out << "  )\n";

        // 为每种芯片提供完整的 14 引脚库定义。PCB Editor 需要用它将
        // DIP-14 封装焊盘编号与逻辑符号引脚编号对应起来。
        struct LibraryPin
        {
            const char* number;
            const char* name;
            const char* type;
        };
        const LibraryPin commonPins[] = {
            { "1", "1A", "input" },
            { "2", "1B", "input" },
            { "3", "1Y", "output" },
            { "4", "2A", "input" },
            { "5", "2B", "input" },
            { "6", "2Y", "output" },
            { "7", "GND", "power_in" },
            { "8", "3Y", "output" },
            { "9", "3A", "input" },
            { "10", "3B", "input" },
            { "11", "4Y", "output" },
            { "12", "4A", "input" },
            { "13", "4B", "input" },
            { "14", "VCC", "power_in" }
        };

        out << "  (libparts\n";
        for (const char* part : { "74LS86", "74LS09", "74LS32" })
        {
            const char* description =
                (std::string(part) == "74LS86") ? "Quad 2-input XOR" :
                (std::string(part) == "74LS09") ? "Quad 2-input AND Open Collector" :
                "Quad 2-input OR";
            out << "    (libpart (lib \"74xx\") (part \"" << part << "\")\n";
            out << "      (description \"" << description << "\")\n";
            out << "      (footprints (fp \"DIP*\"))\n";
            out << "      (fields (field (name \"Reference\") \"U\")"
                   " (field (name \"Value\") \"" << part << "\"))\n";
            out << "      (pins\n";
            for (const auto& pin : commonPins)
            {
                const char* type = pin.type;
                if (std::string(part) == "74LS09" &&
                    (std::string(pin.number) == "3" || std::string(pin.number) == "6" ||
                     std::string(pin.number) == "8" || std::string(pin.number) == "11"))
                    type = "open_collector";
                out << "        (pin (num \"" << pin.number << "\") (name \""
                    << pin.name << "\") (type " << type << "))\n";
            }
            out << "      )\n";
            out << "    )\n";
        }
        out << "  )\n";
        out << "  (nets\n";
        int netCode = 1;
        for (const auto& net : nets)
        {
            std::vector<std::string> nodes;
            std::string netName;

            for (const auto& pin : net.pins)
            {
                auto port = portNames.find(pin.componentId);
                if (port != portNames.end())
                {
                    if (port->second == "inputA" || port->second == "inputB" || port->second == "Cin" ||
                        port->second == "Sum" || port->second == "Cout")
                        netName = port->second;
                    continue;
                }

                auto gate = mapping.find(pin.componentId);
                if (gate == mapping.end()) continue;

                int physicalPin = 0;
                std::string pinType;
                if (pin.type == PinType::Input)
                {
                    if (pin.index == 0) physicalPin = gate->second.inputA;
                    else if (pin.index == 1) physicalPin = gate->second.inputB;
                    else { error = "逻辑门输入引脚索引无效。"; return false; }
                    pinType = "input";
                }
                else
                {
                    if (pin.index != 0) { error = "逻辑门输出引脚索引无效。"; return false; }
                    physicalPin = gate->second.output;
                    pinType = (gate->second.value == "74LS09") ? "open_collector" : "output";
                }
                nodes.push_back("(node (ref \"" + gate->second.reference + "\") (pin \"" +
                    std::to_string(physicalPin) + "\") (pintype \"" + pinType + "\"))");
            }

            if (nodes.empty()) continue;
            std::sort(nodes.begin(), nodes.end());
            if (netName.empty())
            {
                const auto first = nodes.front();
                const auto refStart = first.find("(ref \"");
                const auto refEnd = first.find("\"", refStart + 6);
                const auto pinStart = first.find("(pin \"");
                const auto pinEnd = first.find("\"", pinStart + 6);
                const std::string ref = first.substr(refStart + 6, refEnd - (refStart + 6));
                const std::string pin = first.substr(pinStart + 6, pinEnd - (pinStart + 6));
                netName = "Net-(" + ref + "-Pad" + pin + ")";
            }

            out << "    (net (code \"" << netCode++ << "\") (name \"/" << EscapeSExpr(netName) << "\") (class \"Default\")\n";
            for (const auto& node : nodes)
                out << "      " << node << "\n";
            out << "    )\n";
        }
        out << "  )\n";
        out << ")\n";

        const std::string contents = out.str();
        const wxString wxPath = wxString::FromUTF8(filename.c_str());
        if (wxPath.IsEmpty())
        {
            error = "网表保存路径不是有效的 UTF-8 路径。";
            return false;
        }

        wxFile file;
        if (!file.Create(wxPath, true) && !file.Open(wxPath, wxFile::write))
        {
            error = "无法创建 KiCad 网表文件，请检查目录权限。";
            return false;
        }
        const wxFileOffset written = file.Write(contents.data(), contents.size());
        const bool ok = written == static_cast<wxFileOffset>(contents.size()) && file.Flush();
        file.Close();
        if (!ok)
        {
            error = "写入 KiCad 网表文件失败。";
            return false;
        }
        return true;
    }
}

bool KiCadNetlistExporter::ExportFullAdder(
    const CircuitDocument& document,
    const std::string& filename,
    std::string& error)
{
    error.clear();
    wxString validationError;
    if (!CircuitValidation::Validate(document, validationError))
    {
        error = validationError.ToStdString(wxConvUTF8);
        return false;
    }
    return WriteDocumentAsKiCadNetlist(document, filename, error);
}
