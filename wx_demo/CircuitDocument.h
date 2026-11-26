#pragma once

#include <wx/gdicmn.h>
#include <wx/string.h>
#include <string>
#include <vector>
#include "SimulationConfig.h"

// One scheduled transition of an interactive input: at timeNs the source
// becomes high/low. Times are integer nanoseconds and strictly increasing.
struct WaveformEdge
{
    std::int64_t timeNs = 0;
    bool high = false;
    bool operator==(const WaveformEdge&) const = default;
};

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
    // DFF sampling edge; false preserves the original rising-edge behavior.
    bool fallingEdge = false;
    // Custom stimuli for INPUT / BUTTON sources. Empty means no scheduled
    // waveform: the source keeps its initial level for the whole simulation.
    std::vector<WaveformEdge> waveform;
    // COUNTER design data. The counter is 4-bit; max is 1..15 and initial is 0..max.
    // fallingEdge above selects the active clock edge for COUNTER as well as DFF.
    int counterMax = 15;
    int counterInitial = 0;
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
