#pragma once

#include <cstddef>

// Shared design/storage limits. Parsers can enforce budgets while reading;
// validation enforces the same budgets for documents created in memory.
namespace CircuitLimits
{
inline constexpr int CurrentFormatVersion = 3;
inline constexpr std::size_t MaxFileBytes = 8 * 1024 * 1024;
inline constexpr std::size_t MaxComponents = 1000;
inline constexpr std::size_t MaxWires = 4000;
inline constexpr std::size_t MaxRoutePoints = 256;
inline constexpr std::size_t MaxTotalRoutePoints = 4000;
inline constexpr std::size_t MaxTexts = 1000;
inline constexpr std::size_t MaxTextLength = 65536;
inline constexpr std::size_t MaxTotalTextLength = 262144;
inline constexpr int MaxCoordinate = 1000000;
inline constexpr int MaxDimension = 1000000;
inline constexpr int MinPointSize = 6;
inline constexpr int MaxPointSize = 96;
inline constexpr int MaxXmlDepth = 8;
inline constexpr std::size_t MaxXmlNodes = 16000;
}
