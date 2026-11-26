#pragma once

#include "CircuitDocument.h"
#include <wx/string.h>
#include <vector>

namespace VerilogWorkspace
{
inline constexpr std::size_t MaxSourceBytes = 8 * 1024 * 1024;
struct Source
{
    unsigned id = 0;
    wxString name;
    wxString path;
    wxString code;
};
struct Diagnostic
{
    unsigned sourceId = 0;
    int line = 0;
    wxString message;
};
bool ReadUtf8(const wxString& path, wxString& code, wxString& error);
bool SaveUtf8(const wxString& path, const wxString& code, wxString& error);
bool FromCircuit(const CircuitDocument& circuit, wxString& code, wxString& error);
wxString ModuleTemplate();
wxString FullAdder();
wxString FullAdderTestbench();
wxString DffTemplate();
wxString TestbenchTemplate();
}
