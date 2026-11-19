#include "AiCircuitExecutor.h"
#include "CircuitLimits.h"
#include "CircuitValidation.h"
#include "ComponentRegistry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::size_t MaxAddedComponents = 32;
constexpr std::size_t MaxAddedWires = 64;
constexpr int Grid = 20;
constexpr int Margin = 20;
constexpr int ComponentInset = 40;

bool Fail(wxString& error, const wxString& message)
{
    error = message;
    return false;
}

bool IsReference(std::string_view value)
{
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (value.empty() || value.size() > 32 || (!letter(value.front()) && value.front() != '_')) return false;
    return std::all_of(value.begin() + 1, value.end(), [&](char c) {
        return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

bool IsAllowedType(std::string_view type)
{
    constexpr std::array<std::string_view, 8> supported{
        "INPUT", "OUTPUT", "AND", "OR", "NOT", "NAND", "NOR", "XOR"};
    return std::find(supported.begin(), supported.end(), type) != supported.end();
}

int FindPin(const std::vector<Pin>& pins, std::string_view name)
{
    for (std::size_t index = 0; index < pins.size(); ++index)
        if (pins[index].name == name) return static_cast<int>(index);
    return -1;
}

int AlignUp(int value)
{
    return ((std::max(0, value) + Grid - 1) / Grid) * Grid;
}

// These are the bounds of the current canvas symbols including their pin dots
// and outline. Rotate around the same body centre as the canvas.
wxRect ComponentBounds(const CircuitComponentData& component)
{
    const auto& type = component.type;
    int width = 80, height = 80, left = 0, right = 80;
    if (type == "INPUT" || type == "OUTPUT") width = height = right = 40;
    else if (type == "NOT") { width = height = 40; right = 60; }
    else if (type == "COMPARATOR") width = right = 100;
    else if (type == "AND" || type == "OR" || type == "NAND" || type == "NOR" || type == "XOR")
    {
        height = std::max(type == "AND" || type == "NAND" ? 80 : 120,
            (component.inputCount / 2 + 1) * 2 * Grid);
        if (type == "NOR" || type == "XOR") left = -20;
        if (type == "NAND" || type == "NOR") right = 100;
    }
    const wxPoint centre(width / 2, height / 2);
    const wxRect local(left - 5, -5, right - left + 11, height + 11);
    wxPoint minPoint(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    wxPoint maxPoint(std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
    for (const auto point : {local.GetTopLeft(), local.GetTopRight(), local.GetBottomLeft(), local.GetBottomRight()})
    {
        wxPoint relative = point - centre;
        if (component.rotation == 90) relative = wxPoint(-relative.y, relative.x);
        else if (component.rotation == 180) relative = -relative;
        else if (component.rotation == 270) relative = wxPoint(relative.y, -relative.x);
        const auto placed = wxPoint(component.x, component.y) + centre + relative;
        minPoint.x = std::min(minPoint.x, placed.x);
        minPoint.y = std::min(minPoint.y, placed.y);
        maxPoint.x = std::max(maxPoint.x, placed.x);
        maxPoint.y = std::max(maxPoint.y, placed.y);
    }
    return wxRect(minPoint, maxPoint);
}

std::vector<wxRect> OccupiedAreas(const CircuitDocument& original)
{
    std::vector<wxRect> occupied;
    occupied.reserve(original.components.size() + original.texts.size() + original.wires.size());
    std::unordered_map<int, wxRect> bounds;
    for (const auto& component : original.components)
    {
        const wxRect box = ComponentBounds(component);
        bounds.emplace(component.id, box);
        occupied.push_back(box);
    }
    for (const auto& text : original.texts) occupied.push_back(text.rect);
    for (const auto& wire : original.wires)
    {
        wxRect envelope = bounds.at(wire.sourceId);
        envelope.Union(bounds.at(wire.targetId));
        for (const auto point : wire.route) envelope.Union(wxRect(point, wxSize(1, 1)));
        // Existing automatic routes can detour by two grid cells, and a stale
        // manual endpoint can be reconnected. Reserve that clearance as well.
        envelope.Inflate(2 * Grid + 6);
        occupied.push_back(envelope);
    }
    for (auto& box : occupied) box.Inflate(Grid);
    std::sort(occupied.begin(), occupied.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
    return occupied;
}

bool FindOrigin(const CircuitDocument& original, int width, int height, const wxSize& available, wxPoint& origin)
{
    const auto occupied = OccupiedAreas(original);
    std::vector<int> columns{Margin};
    columns.reserve(occupied.size() + 1);
    for (const auto& box : occupied)
        if (box.GetRight() >= Margin) columns.push_back(AlignUp(box.GetRight() + 1));
    std::sort(columns.begin(), columns.end());
    columns.erase(std::unique(columns.begin(), columns.end()), columns.end());

    // For each possible left edge, place the whole generated block at the
    // earliest clear row. Prefer a compact extension near the canvas origin.
    const int rightLimit = available.x < 0 ? CircuitLimits::MaxCoordinate :
        std::min(CircuitLimits::MaxCoordinate, available.x - 1);
    const int bottomLimit = available.y < 0 ? CircuitLimits::MaxCoordinate :
        std::min(CircuitLimits::MaxCoordinate, available.y - 1);
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const int x : columns)
    {
        const int right = x + width - 1;
        if (right > rightLimit) break;
        int y = Margin;
        for (const auto& box : occupied)
        {
            if (box.y >= y + height) break;
            if (box.x <= right && box.GetRight() >= x && box.GetBottom() >= y)
                y = AlignUp(box.GetBottom() + 1);
        }
        const int bottom = y + height - 1;
        if (bottom > bottomLimit) continue;
        const auto cost = static_cast<std::int64_t>(right) + 2 * static_cast<std::int64_t>(bottom);
        if (cost < best)
        {
            best = cost;
            origin = wxPoint(x, y);
        }
    }
    return best != std::numeric_limits<std::int64_t>::max();
}

bool PlaceComponents(const CircuitDocument& original, std::vector<CircuitComponentData>& added,
    const std::vector<int>& depths, const wxSize& available, wxString& error)
{
    const auto depthCount = static_cast<std::size_t>(*std::max_element(depths.begin(), depths.end()) + 1);
    std::vector<int> heights(depthCount, 0);
    std::vector<int> leftEdges(depthCount, 0), rightEdges(depthCount, 0);
    for (std::size_t index = 0; index < added.size(); ++index)
    {
        auto& component = added[index];
        const auto layer = static_cast<std::size_t>(depths[index]);
        const auto footprint = ComponentBounds(component);
        component.y = heights[layer] + Grid;
        heights[layer] += AlignUp(footprint.GetHeight() + Grid);
        leftEdges[layer] = std::min(leftEdges[layer], footprint.GetLeft());
        rightEdges[layer] = std::max(rightEdges[layer], footprint.GetRight());
    }
    std::vector<int> columnX(depthCount, ComponentInset);
    for (std::size_t layer = 1; layer < depthCount; ++layer)
        columnX[layer] = AlignUp(columnX[layer - 1] + rightEdges[layer - 1] - leftEdges[layer] + 2 * Grid);
    const int tallest = *std::max_element(heights.begin(), heights.end());
    int width = 0, height = 0;
    for (std::size_t index = 0; index < added.size(); ++index)
    {
        auto& component = added[index];
        const auto layer = static_cast<std::size_t>(depths[index]);
        const int columnHeight = heights[layer];
        component.x = columnX[layer];
        component.y += ((tallest - columnHeight) / (2 * Grid)) * Grid;
        const auto bounds = ComponentBounds(component);
        width = std::max(width, AlignUp(bounds.GetRight() + Grid + 1));
        height = std::max(height, AlignUp(bounds.GetBottom() + Grid + 1));
    }
    wxPoint origin;
    if (!FindOrigin(original, width, height, available, origin))
        return Fail(error, wxS("当前画布空白区域不足，请放大窗口或尝试较小的电路。"));
    for (auto& component : added)
    {
        component.x += origin.x;
        component.y += origin.y;
    }
    return true;
}
}

bool AiCircuitExecutor::BuildCandidate(const CircuitDocument& original, const AiCircuitAction& action,
    CircuitDocument& candidate, wxString& error, const wxSize& available)
{
    error.clear();
    if (action.components.empty() || action.components.size() > MaxAddedComponents ||
        action.wires.size() > MaxAddedWires)
        return Fail(error, wxS("一次可新增 1 至 32 个元件和最多 64 条导线。"));
    if (!CircuitValidation::Validate(original, error)) return false;
    if (original.components.size() + action.components.size() > CircuitLimits::MaxComponents ||
        original.wires.size() + action.wires.size() > CircuitLimits::MaxWires)
        return Fail(error, wxS("添加后会超出文档的元件或导线数量上限。"));

    std::unordered_set<int> ids;
    for (const auto& component : original.components) ids.insert(component.id);
    int nextId = 1;
    std::unordered_map<std::string, std::size_t> refs;
    std::vector<std::unique_ptr<Component>> pinModels;
    std::vector<CircuitComponentData> added;
    pinModels.reserve(action.components.size());
    added.reserve(action.components.size());
    for (const auto& spec : action.components)
    {
        if (!IsReference(spec.ref) || !refs.emplace(spec.ref, added.size()).second)
            return Fail(error, wxS("新增元件的引用名无效或重复。"));
        if (!IsAllowedType(spec.type) || spec.inputCount < 0)
            return Fail(error, wxS("此版本仅支持基本逻辑门和输入、输出端口，且必须指定有效的输入数量。"));
        auto pins = ComponentRegistry::Create(spec.type, spec.inputCount);
        if (!pins)
            return Fail(error, wxString::Format(wxS("元件 %s 的类型或输入数量无效。"), wxString::FromUTF8(spec.ref)));
        while (ids.contains(nextId)) ++nextId;
        CircuitComponentData component;
        component.id = nextId++;
        component.type = spec.type;
        component.inputCount = spec.inputCount;
        added.push_back(std::move(component));
        pinModels.push_back(std::move(pins));
    }

    std::vector<CircuitWireData> addedWires;
    addedWires.reserve(action.wires.size());
    std::vector<std::vector<std::size_t>> outgoing(added.size());
    std::vector<int> indegree(added.size(), 0);
    std::vector<std::vector<bool>> driven;
    driven.reserve(added.size());
    for (const auto& pins : pinModels) driven.emplace_back(pins->inputs.size(), false);
    for (const auto& spec : action.wires)
    {
        if (!IsReference(spec.from) || !IsReference(spec.to))
            return Fail(error, wxS("导线两端必须引用本次新增的元件。"));
        const auto from = refs.find(spec.from), to = refs.find(spec.to);
        if (from == refs.end() || to == refs.end())
            return Fail(error, wxS("导线两端必须引用本次新增的元件。"));
        const auto source = from->second, target = to->second;
        const int outPin = FindPin(pinModels[source]->outputs, spec.outPin);
        const int inPin = FindPin(pinModels[target]->inputs, spec.inPin);
        if (outPin < 0 || inPin < 0)
            return Fail(error, wxS("导线引脚名无效：必须从已存在的输出引脚连到已存在的输入引脚。"));
        if (driven[target][static_cast<std::size_t>(inPin)])
            return Fail(error, wxS("重复连线，或同一个输入引脚连接了多个信号源。"));
        driven[target][static_cast<std::size_t>(inPin)] = true;
        addedWires.push_back({added[source].id, outPin, added[target].id, inPin, {}});
        outgoing[source].push_back(target);
        ++indegree[target];
    }

    std::queue<std::size_t> ready;
    for (std::size_t index = 0; index < indegree.size(); ++index)
        if (indegree[index] == 0) ready.push(index);
    std::vector<int> depths(added.size(), 0);
    std::size_t visited = 0;
    while (!ready.empty())
    {
        const auto source = ready.front();
        ready.pop();
        ++visited;
        for (const auto target : outgoing[source])
        {
            depths[target] = std::max(depths[target], depths[source] + 1);
            if (--indegree[target] == 0) ready.push(target);
        }
    }
    if (visited != added.size()) return Fail(error, wxS("新增电路中存在组合逻辑反馈环路。"));
    if (!PlaceComponents(original, added, depths, available, error)) return false;

    CircuitDocument staged = original;
    for (auto& component : added) staged.components.push_back(std::move(component));
    for (auto& wire : addedWires) staged.wires.push_back(std::move(wire));
    if (!CircuitValidation::Validate(staged, error)) return false;
    candidate = std::move(staged);
    return true;
}
