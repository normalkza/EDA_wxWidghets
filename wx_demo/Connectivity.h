#pragma once

#include "Pin.h"
#include <cstddef>
#include <string>
#include <vector>

// Value-only references are safe to retain after a canvas or simulation has gone
// away. Direction is an endpoint namespace; the builder treats links as undirected.
struct NetPin
{
    int componentId = 0;
    PinType type = PinType::Input;
    std::size_t index = 0;
    bool operator==(const NetPin&) const = default;
    bool operator<(const NetPin& other) const;
};

struct NetLink
{
    NetPin first;
    NetPin second;
};

struct ElectricalNet
{
    // Deterministic within this compiled topology, not persistent across edits.
    int id = 0;
    std::vector<NetPin> pins;
};

class ConnectivityBuilder
{
public:
    // Includes isolated pins, sorts every net and all nets by endpoint key, and
    // leaves 'nets' untouched on invalid input. Geometry never creates links.
    static bool Build(const std::vector<NetPin>& pins, const std::vector<NetLink>& links,
        std::vector<ElectricalNet>& nets, std::string& error);
};
