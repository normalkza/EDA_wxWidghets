#pragma once

#include "Component.h"
#include <wx/gdicmn.h>
#include <cstddef>
#include <vector>
#include <string>
#include <unordered_map>

struct PinReference
{
    Component* component = nullptr;
    PinType type = PinType::Input;
    std::size_t index = 0;
    bool IsValid() const;
    Pin& Get() const;
    bool operator==(const PinReference& other) const;
};

struct WireConnection
{
    PinReference output;
    PinReference input;
    LogicValue GetValue() const;
    std::vector<wxPoint> route;
};

enum class ConnectionResult
{
    Added, Duplicate, InvalidPin, SameDirection, InputOccupied, Cycle
};

// 组合逻辑网络：单输入单驱动、输出可分支，按拓扑顺序计算并传播电平。
class Circuit
{
public:
    ConnectionResult Connect(PinReference first, PinReference second);
    // Atomic bulk import after one shared topology validation. This API checks
    // electrical connections only; callers validate document route geometry.
    // Invalid input leaves the existing connections unchanged.
    ConnectionResult ReplaceConnections(const std::vector<Component*>& components,
        const std::vector<WireConnection>& connections);
    ConnectionResult ValidateConnection(PinReference first, PinReference second) const;
    ConnectionResult ValidateTopology(const std::vector<Component*>& components) const;
    void RemoveConnection(std::size_t index);
    bool SetConnectionRoute(std::size_t index, const std::vector<wxPoint>& route);
    void RemoveComponent(const Component* component);
    void PruneInvalidConnections();
    bool Evaluate(const std::vector<Component*>& components);
    const std::vector<WireConnection>& GetConnections() const { return m_connections; }


private:
    std::vector<WireConnection> m_connections;
};
