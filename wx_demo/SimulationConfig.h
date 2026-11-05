#pragma once

#include <cstddef>
#include <cstdint>

// All digital simulation time values are integer nanoseconds.
struct SimulationConfig
{
    std::int64_t stopTimeNs = 1000;
    std::size_t maxEvents = 100000;
    std::size_t maxDeltaCycles = 128;
    std::size_t maxTraceSamples = 200000;
    bool operator==(const SimulationConfig&) const = default;
};
