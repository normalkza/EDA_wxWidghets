#pragma once

#include <wx/string.h>
#include <vector>

struct AiChatMessage
{
    wxString role;
    wxString text;
};

struct AiWorkspaceContext
{
    wxString sceneXml;
    wxString imageBase64;
    std::vector<AiChatMessage> history;
    wxString feedback;
    bool review = false;
};
