#pragma once

#include "Circuit.h"
#include <vector>

// Shared digital topology rules for interactive connections, document loading,
// and compilation. Geometry and file format details do not belong here.
class TopologyRules
{
public:
    static ConnectionResult CheckConnection(const std::vector<WireConnection>& connections,
        PinReference first, PinReference second);
    static ConnectionResult Validate(const std::vector<Component*>& components,
        const std::vector<WireConnection>& connections);

    // The legacy evaluator can execute only fully combinational components.
    // Return false before changing any pin if a timing/state boundary is present.
    static bool EvaluationOrder(const std::vector<Component*>& components,
        const std::vector<WireConnection>& connections, std::vector<Component*>& order);
};
