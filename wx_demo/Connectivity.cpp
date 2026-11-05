#include "Connectivity.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <tuple>
#include <utility>

bool NetPin::operator<(const NetPin& other) const
{
    return std::tie(componentId, type, index) < std::tie(other.componentId, other.type, other.index);
}

bool ConnectivityBuilder::Build(const std::vector<NetPin>& pins, const std::vector<NetLink>& links,
    std::vector<ElectricalNet>& nets, std::string& error)
{
    error.clear();
    std::vector<NetPin> sorted = pins;
    std::sort(sorted.begin(), sorted.end());
    std::map<NetPin, std::size_t> indices;
    for (std::size_t index = 0; index < sorted.size(); ++index)
    {
        const auto& pin = sorted[index];
        if (pin.componentId <= 0 || (pin.type != PinType::Input && pin.type != PinType::Output))
        {
            error = "Invalid electrical endpoint.";
            return false;
        }
        if (!indices.emplace(pin, index).second)
        {
            error = "Duplicate electrical endpoint.";
            return false;
        }
    }
    std::vector<std::size_t> parent(sorted.size()), rank(sorted.size(), 0);
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&parent](std::size_t index) {
        while (parent[index] != index)
        {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };
    for (const auto& link : links)
    {
        const auto first = indices.find(link.first), second = indices.find(link.second);
        if (first == indices.end() || second == indices.end())
        {
            error = "An electrical link references an unknown endpoint.";
            return false;
        }
        auto a = root(first->second), b = root(second->second);
        if (a == b) continue;
        if (rank[a] < rank[b]) std::swap(a, b);
        parent[b] = a;
        if (rank[a] == rank[b]) ++rank[a];
    }
    std::map<std::size_t, std::vector<NetPin>> grouped;
    for (std::size_t index = 0; index < sorted.size(); ++index)
        grouped[root(index)].push_back(sorted[index]);
    std::vector<ElectricalNet> staged;
    staged.reserve(grouped.size());
    for (auto& [representative, members] : grouped)
        staged.push_back({0, std::move(members)});
    std::sort(staged.begin(), staged.end(), [](const ElectricalNet& first, const ElectricalNet& second) {
        return first.pins.front() < second.pins.front();
    });
    for (std::size_t index = 0; index < staged.size(); ++index)
        staged[index].id = static_cast<int>(index + 1);
    nets = std::move(staged);
    return true;
}
