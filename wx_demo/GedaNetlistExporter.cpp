
#include "GedaNetlistExporter.h"
#include "Connectivity.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>

bool GedaNetlistExporter::ExportFullAdder(
    const CircuitDocument& document,
    const std::string& filename,
    std::string& error)
{
    error.clear();

    // 1. 收集五个逻辑门
    std::vector<const CircuitComponentData*> xors;
    std::vector<const CircuitComponentData*> ands;
    std::vector<const CircuitComponentData*> ors;

    for (const auto& component : document.components)
    {
        if (component.id <= 0)
        {
            error = "Invalid component ID.";
            return false;
        }

        if (component.type == "XOR")
            xors.push_back(&component);
        else if (component.type == "AND")
            ands.push_back(&component);
        else if (component.type == "OR")
            ors.push_back(&component);
        else if (component.type != "INPUT" && component.type != "OUTPUT")
        {
            error = "Unsupported component: " + component.type;
            return false;
        }
    }

    if (xors.size() != 2 || ands.size() != 2 || ors.size() != 1)
    {
        error = "Full adder requires 2 XOR, 2 AND and 1 OR gates.";
        return false;
    }

    // 2. 按元件 ID 排序，确定芯片内部的门槽位
    auto sortById = [](auto& gates) {
        std::sort(gates.begin(), gates.end(),
            [](const auto* a, const auto* b) {
                return a->id < b->id;
            });
        };

    sortById(xors);
    sortById(ands);
    sortById(ors);

    // 3. 建立程序引脚到芯片物理引脚的映射
    struct GateMapping
    {
        std::string ref;
        int inputA;
        int inputB;
        int output;
    };

    std::map<int, GateMapping> mappings;

    mappings[xors[0]->id] = { "U1", 1, 2, 3 };
    mappings[xors[1]->id] = { "U1", 4, 5, 6 };

    mappings[ands[0]->id] = { "U2", 1, 2, 3 };
    mappings[ands[1]->id] = { "U2", 4, 5, 6 };

    mappings[ors[0]->id] = { "U3", 1, 2, 3 };

    // 4. 提取所有引脚
    std::vector<NetPin> pins;
    std::vector<NetLink> links;
    std::vector<ElectricalNet> nets;

    for (const auto& component : document.components)
    {
        if (component.type == "INPUT")
        {
            if (component.inputCount != 0)
            {
                error = "INPUT must have no input pins.";
                return false;
            }

            pins.push_back({ component.id, PinType::Output, 0 });
        }
        else if (component.type == "OUTPUT")
        {
            if (component.inputCount != 1)
            {
                error = "OUTPUT must have one input pin.";
                return false;
            }

            pins.push_back({ component.id, PinType::Input, 0 });
        }
        else
        {
            if (component.inputCount != 2)
            {
                error = "Logic gates must have exactly two inputs.";
                return false;
            }

            pins.push_back({ component.id, PinType::Input, 0 });
            pins.push_back({ component.id, PinType::Input, 1 });
            pins.push_back({ component.id, PinType::Output, 0 });
        }
    }

    // 5. 提取连线
    for (const auto& wire : document.wires)
    {
        if (wire.sourcePin < 0 || wire.targetPin < 0)
        {
            error = "Invalid wire pin index.";
            return false;
        }

        links.push_back({
            { wire.sourceId, PinType::Output,
              static_cast<std::size_t>(wire.sourcePin) },
            { wire.targetId, PinType::Input,
              static_cast<std::size_t>(wire.targetPin) }
            });
    }

    // 6. 构建电气网络
    if (!ConnectivityBuilder::Build(pins, links, nets, error))
        return false;

    // 7. 创建网表文件
    std::ofstream file(filename, std::ios::out | std::ios::trunc);

    if (!file)
    {
        error = "Cannot create netlist file.";
        return false;
    }

    int netNumber = 1;

    for (const auto& net : nets)
    {
        std::vector<std::string> endpoints;

        for (const auto& pin : net.pins)
        {
            auto it = mappings.find(pin.componentId);

            // INPUT 和 OUTPUT 是外部端口，不是实际芯片引脚
            if (it == mappings.end())
                continue;

            const auto& mapping = it->second;
            int physicalPin = 0;

            if (pin.type == PinType::Input)
            {
                if (pin.index == 0)
                    physicalPin = mapping.inputA;
                else if (pin.index == 1)
                    physicalPin = mapping.inputB;
                else
                {
                    error = "Invalid gate input index.";
                    return false;
                }
            }
            else
            {
                if (pin.index != 0)
                {
                    error = "Invalid gate output index.";
                    return false;
                }

                physicalPin = mapping.output;
            }

            endpoints.push_back(
                mapping.ref + "-" + std::to_string(physicalPin));
        }

        if (endpoints.empty())
            continue;

        std::sort(endpoints.begin(), endpoints.end());

        file << "unnamed_net" << netNumber++;

        for (const auto& endpoint : endpoints)
            file << " " << endpoint;

        file << "\n";
    }

    // 8. 添加三片芯片的电源引脚
    file << "GND U3-7 U1-7 U2-7\n";
    file << "Vcc U3-14 U1-14 U2-14\n";

    file.close();

    if (!file)
    {
        error = "Failed while writing netlist file.";
        return false;
    }

    return true;
}
