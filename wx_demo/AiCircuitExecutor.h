#pragma once

#include "CircuitDocument.h"
#include <string>
#include <map>
#include <vector>

struct AiComponentSpec
{
    std::string ref;
    std::string type;
    int inputCount = 0;
};

struct AiWireSpec
{
    std::string from;
    std::string outPin;
    std::string to;
    std::string inPin;
};

struct AiCircuitAction
{
    std::vector<AiComponentSpec> components;
    std::vector<AiWireSpec> wires;
};

struct AiEditOperation
{
    std::string kind;
    std::string ref;
    std::map<std::string, wxString> fields;
    std::vector<wxPoint> points;
};

struct AiCircuitPatch
{
    wxString summary;
    std::vector<AiEditOperation> operations;
};

namespace AiCircuitExecutor
{
// V1 appends a subcircuit. References are batch-local, and pins are named.
// Neither the source document nor the output candidate is changed on failure.
bool BuildCandidate(const CircuitDocument& original, const AiCircuitAction& action,
    CircuitDocument& candidate, wxString& error, const wxSize& available = wxDefaultSize);
bool BuildPatchCandidate(const CircuitDocument& original, const AiCircuitPatch& patch,
    CircuitDocument& candidate, wxString& error, const wxSize& available = wxDefaultSize);
wxRect ComponentBounds(const CircuitComponentData& component);
bool ArrangeDocument(const CircuitDocument& original, CircuitDocument& candidate,
    wxString& error, const wxSize& available);
}
