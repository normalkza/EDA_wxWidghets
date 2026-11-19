#pragma once

#include "AiCircuitExecutor.h"
#include <wx/timer.h>
#include <wx/webrequest.h>
#include <functional>
#include <string>

enum class AiAssistantMode { Guide, Apply };

struct AiAssistantReply
{
    enum class Kind { Text, Action, Error, Cancelled };
    Kind kind = Kind::Error;
    wxString text;
    AiCircuitAction action;
};

// GUI-thread client for the local assistant broker. A successful Start invokes
// its completion once, after the request has reached a terminal state.
class AiApiClient final : public wxEvtHandler
{
public:
    using Completion = std::function<void(AiAssistantReply)>;

    AiApiClient();
    ~AiApiClient() override;
    bool Start(AiAssistantMode mode, const wxString& question, Completion completion, wxString& error);
    void Cancel();
    bool IsBusy() const { return m_request.IsOk(); }

private:
    enum class StopReason { None, User, Deadline, BodyLimit };
    void Stop(StopReason reason);
    void DetachEvents();
    void Complete(AiAssistantReply reply);
    void OnData(wxWebRequestEvent& event);
    void OnState(wxWebRequestEvent& event);

    wxWebSession m_session;
    wxWebRequest m_request;
    wxTimer m_deadline;
    Completion m_completion;
    std::string m_body;
    int m_eventId = wxID_NONE;
    bool m_sessionReady = false;
    AiAssistantMode m_mode = AiAssistantMode::Guide;
    StopReason m_stopReason = StopReason::None;
};
