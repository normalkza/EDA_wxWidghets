#pragma once

#include "AiApiClient.h"
#include <wx/dialog.h>
#include <wx/weakref.h>
#include <cstdint>
#include <optional>

class Canvas;
class wxButton;
class wxStaticText;
class wxTextCtrl;

class AiAssistantDialog final : public wxDialog
{
public:
    AiAssistantDialog(wxWindow* parent, Canvas& canvas);
    void ShowAssistant();
    bool IsBusy() const { return m_client.IsBusy(); }
    void BeginOwnerShutdown(std::function<void()> afterStop);
    void ResumeAfterOwnerShutdown();

private:
    struct Pending
    {
        std::uint64_t serial = 0;
        AiAssistantMode mode = AiAssistantMode::Guide;
        std::uint64_t revision = 0;
        CircuitDocument original;
    };

    void Submit(AiAssistantMode mode);
    void OnReply(std::uint64_t serial, AiAssistantReply reply);
    void OnClose(wxCloseEvent& event);
    void UpdateButtons();
    void AppendHistory(const wxString& title, const wxString& text);
    void ReportError(const wxString& message);
    void NotifyStopped();

    wxWeakRef<Canvas> m_canvas;
    AiApiClient m_client;
    std::optional<Pending> m_pending;
    std::uint64_t m_serial = 0;
    std::function<void()> m_afterStop;
    wxString m_transcript;
    wxTextCtrl* m_history = nullptr;
    wxTextCtrl* m_question = nullptr;
    wxStaticText* m_status = nullptr;
    wxButton* m_guide = nullptr;
    wxButton* m_apply = nullptr;
    wxButton* m_cancel = nullptr;
    bool m_stopping = false;
    bool m_ownerClosing = false;
};
