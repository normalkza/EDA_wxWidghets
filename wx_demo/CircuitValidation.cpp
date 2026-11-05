#include "CircuitValidation.h"
#include "CircuitLimits.h"
#include "ComponentRegistry.h"
#include "TopologyRules.h"
#include <memory>
#include <unordered_map>
#include <utility>
#include <limits>

using namespace CircuitLimits;

namespace
{
bool Fail(wxString& error, const wxString& message)
{
    error = message;
    return false;
}
}

bool CircuitValidation::IsCoordinate(int value)
{
    return value >= -MaxCoordinate && value <= MaxCoordinate;
}

bool CircuitValidation::IsRotation(int value)
{
    return value == 0 || value == 90 || value == 180 || value == 270;
}

bool CircuitValidation::ValidateRoute(const std::vector<wxPoint>& route, wxString& error)
{
    if (route.empty()) return true;
    if (route.size() < 2 || route.size() > MaxRoutePoints)
        return Fail(error, wxS("手动连线路径必须包含 2 至 256 个点。"));
    for (std::size_t index = 0; index < route.size(); ++index)
    {
        const auto& point = route[index];
        if (!IsCoordinate(point.x) || !IsCoordinate(point.y))
            return Fail(error, wxS("连线路径坐标必须在 -1000000 至 1000000 之间。"));
        if (index == 0) continue;
        const auto& previous = route[index - 1];
        if (point == previous || (point.x != previous.x && point.y != previous.y))
            return Fail(error, wxS("连线路径相邻点必须形成非零长度的水平或垂直线段。"));
    }
    return true;
}

bool CircuitValidation::IsValidXmlText(const wxString& text)
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

bool CircuitValidation::Validate(const CircuitDocument& document, wxString& error)
{
    error.clear();
    if (document.formatVersion < 1 || document.formatVersion > CurrentFormatVersion)
        return Fail(error, wxS("不支持此电路文件版本，目前支持 version=1、2 或 3。"));
    const auto& config = document.simulation;
    if (config.stopTimeNs < 0 || config.stopTimeNs == std::numeric_limits<std::int64_t>::max() ||
        config.maxEvents == 0 || config.maxEvents > 10000000 ||
        config.maxDeltaCycles == 0 || config.maxDeltaCycles > 100000 ||
        config.maxTraceSamples == 0 || config.maxTraceSamples > 10000000)
        return Fail(error, wxS("仿真配置无效：停止时间须非负，事件/记录上限为 1～10000000，delta 上限为 1～100000。"));
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
        if (!IsRotation(data.rotation))
            return Fail(error, wxS("元件旋转角度只能是 0、90、180 或 270 度。"));
        if (data.inputCount < 0)
            return Fail(error, wxString::Format(wxS("元件 %d 的输入数量不能为负数。"), data.id));
        if (data.clockPeriodNs < 2 || data.clockHighNs < 1 || data.clockHighNs >= data.clockPeriodNs ||
            data.clockStartNs < 0 || data.clockStartNs > std::numeric_limits<std::int64_t>::max() - data.clockPeriodNs)
            return Fail(error, wxString::Format(wxS("元件 %d 的时钟参数无效：高低时间均至少为 1 ns，启动延迟不能为负或溢出。"), data.id));
        if (data.type != "CLOCK" && (data.clockPeriodNs != 20 || data.clockHighNs != 10 || data.clockStartNs != 0))
            return Fail(error, wxString::Format(wxS("元件 %d 不是时钟，不能设置时钟参数。"), data.id));
        const auto label = wxString::FromUTF8(data.probeLabel);
        if ((!data.probeLabel.empty() && label.empty()) || label.length() > 128 || !IsValidXmlText(label) ||
            label.Find('\n') != wxNOT_FOUND || label.Find('\r') != wxNOT_FOUND || label.Find('\t') != wxNOT_FOUND ||
            (!data.probeLabel.empty() && data.type != "PROBE"))
            return Fail(error, wxString::Format(wxS("元件 %d 的探针名称无效：仅探针支持名称，长度最多 128 个字符。"), data.id));
        auto component = ComponentRegistry::Create(data.type, data.inputCount, data.high);
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
        if (data.pointSize < MinPointSize || data.pointSize > MaxPointSize)
            return Fail(error, wxS("文本字号必须在 6 至 96 之间。"));
        totalLength += data.text.length();
        if (data.text.length() > MaxTextLength || totalLength > MaxTotalTextLength)
            return Fail(error, wxS("文本过长：单框最多 65536 个字符，全部文本最多 262144 个字符。"));
        if (!IsValidXmlText(data.text))
            return Fail(error, wxS("文本包含 XML 1.0 不支持的控制字符或无效 Unicode 字符。"));
    }
    std::vector<Component*> nodes;
    for (const auto& component : components) nodes.push_back(component.get());
    std::vector<WireConnection> connections;
    connections.reserve(document.wires.size());
    std::size_t totalRoutePoints = 0;
    for (const auto& wire : document.wires)
    {
        if (!ValidateRoute(wire.route, error)) return false;
        totalRoutePoints += wire.route.size();
        if (totalRoutePoints > MaxTotalRoutePoints)
            return Fail(error, wxS("全部连线的路径点总数不能超过 4000 个。"));
        const auto source = indices.find(wire.sourceId), target = indices.find(wire.targetId);
        if (source == indices.end() || target == indices.end())
            return Fail(error, wxS("连线引用了不存在的元件。"));
        if (wire.sourcePin < 0 || wire.targetPin < 0 ||
            static_cast<std::size_t>(wire.sourcePin) >= components[source->second]->outputs.size() ||
            static_cast<std::size_t>(wire.targetPin) >= components[target->second]->inputs.size())
            return Fail(error, wxS("连线引脚越界，源端必须是输出引脚，目标端必须是输入引脚。"));
        connections.push_back({
            {components[source->second].get(), PinType::Output, static_cast<std::size_t>(wire.sourcePin)},
            {components[target->second].get(), PinType::Input, static_cast<std::size_t>(wire.targetPin)}});
    }
    const auto result = TopologyRules::Validate(nodes, connections);
    if (result == ConnectionResult::Duplicate)
        return Fail(error, wxS("电路包含重复连线。"));
    if (result == ConnectionResult::InputOccupied)
        return Fail(error, wxS("同一个输入引脚不能连接多个信号源。"));
    if (result == ConnectionResult::Cycle)
        return Fail(error, wxS("电路包含组合逻辑反馈环路。"));
    if (result != ConnectionResult::Added)
        return Fail(error, wxS("电路包含无效连线。"));
    return true;
}

