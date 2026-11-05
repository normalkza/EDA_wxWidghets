#pragma once

#include <wx/gdicmn.h>
#include <wx/string.h>
#include <string>
#include <vector>
#include "SimulationConfig.h"

// Design data contains values and persistent IDs only. It owns no runtime
// components or controls and can be copied for undo, storage and compilation.
struct CircuitComponentData
{
    int id = 0;
    std::string type;
    int x = 0;
    int y = 0;
    int inputCount = 0;
    bool high = false;
    int rotation = 0;
    std::int64_t clockPeriodNs = 20;
    std::int64_t clockHighNs = 10;
    std::int64_t clockStartNs = 0;
    std::string probeLabel;
};

struct CircuitWireData
{
    int sourceId = 0;
    int sourcePin = 0;
    int targetId = 0;
    int targetPin = 0;
    // Empty means automatic routing; an explicit route includes both endpoints.
    std::vector<wxPoint> route;
};

struct CircuitTextData
{
    wxRect rect;
    int pointSize = 14;
    wxString text;
};

struct CircuitDocument
{
    std::vector<CircuitComponentData> components;
    std::vector<CircuitWireData> wires;
    std::vector<CircuitTextData> texts;
    // Keep these after the original collections to preserve aggregate initializers. A reader retains
    // the input format version; changing runtime signal values does not change it.
    int formatVersion = 1;
    SimulationConfig simulation;
};
