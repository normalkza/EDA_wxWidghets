#pragma once

#include "CircuitDocument.h"

namespace OfflineAssistant
{
struct Result
{
    enum class Kind { Answer, Edit, Error };
    Kind kind = Kind::Answer;
    wxString text;
    wxString topic;
    CircuitDocument document;
};

Result Plan(const CircuitDocument& current, const wxSize& available, const wxString& request,
    bool apply, int selectedComponentId = 0, const wxString& previousTopic = wxString());
wxString Help();
}
