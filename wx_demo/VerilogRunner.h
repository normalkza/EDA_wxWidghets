#pragma once

#include "VerilogWorkspace.h"
#include <atomic>
#include <memory>

namespace VerilogRunner
{
struct Request
{
    std::vector<VerilogWorkspace::Source> sources;
    wxString compiler;
    wxString runtime;
    wxString top;
    bool simulate = false;
    int timeoutSeconds = 10;
};
class TemporaryWorkspace
{
public:
    TemporaryWorkspace();
    ~TemporaryWorkspace();
    wxString Path() const;
private:
    wxString m_path;
};
struct Result
{
    bool success = false;
    bool cancelled = false;
    bool timedOut = false;
    int exitCode = -1;
    wxString output;
    wxString vcdPath;
    std::vector<VerilogWorkspace::Diagnostic> diagnostics;
    std::shared_ptr<TemporaryWorkspace> workspace;
};
wxString FindTool(const wxString& name, const wxString& beside = wxString());
bool IsModuleName(const wxString& name);
Result Run(const Request& request, const std::atomic_bool& cancelled);
}
