#pragma once

#include "Component.h"
#include <cstddef>
#include <vector>

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
    void RemoveConnection(std::size_t index);
    void RemoveComponent(const Component* component);
    void PruneInvalidConnections();
    bool Evaluate(const std::vector<Component*>& components);
    const std::vector<WireConnection>& GetConnections() const { return m_connections; }

private:
    bool WouldCreateCycle(Component* output, Component* input) const;
    std::vector<WireConnection> m_connections;
};
