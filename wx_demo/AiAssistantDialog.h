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
class wxCheckBox;
class wxChoice;
class wxSizer;

enum class AiAssistantState { Idle, Working, Answered, Applied, Failed, Cancelled };

class AiAssistantDialog final : public wxDialog
{
public:
    AiAssistantDialog(wxWindow* parent, Canvas& canvas);
    void ShowAssistant();
    bool IsBusy() const { return m_pending.has_value() || m_client.IsBusy(); }
    void BeginOwnerShutdown(std::function<void()> afterStop);
    void ResumeAfterOwnerShutdown();
    void SetStateCallback(std::function<void(AiAssistantState)> callback);

private:
    friend struct AiPetRegressionAccess;
    friend struct AiSkillRegressionAccess;
    friend struct OfflineRegressionAccess;
    struct Pending
    {
        std::uint64_t serial = 0;
        AiAssistantMode mode = AiAssistantMode::Guide;
        std::uint64_t revision = 0;
        CircuitDocument original;
        wxString goal;
        int round = 0;
        int applied = 0;
    };

    void Submit(AiAssistantMode mode);
    void OnReply(std::uint64_t serial, AiAssistantReply reply);
    void StartRound(Pending pending, const wxString& feedback = wxString());
    void StartOffline(Pending pending);
    bool IsOfflineMode() const;
    void UpdateMode();
    void QueueRound(Pending pending, const wxString& feedback);
    void Remember(const wxString& role, const wxString& text);
    void OnClose(wxCloseEvent& event);
    void UpdateButtons();
    void AppendHistory(const wxString& title, const wxString& text);
    void ReportError(const wxString& message);
    void NotifyStopped();
    void NotifyState(AiAssistantState state);

    wxWeakRef<Canvas> m_canvas;
    AiApiClient m_client;
    std::optional<Pending> m_pending;
    std::uint64_t m_serial = 0;
    std::function<void()> m_afterStop;
    wxString m_transcript;
    std::vector<AiChatMessage> m_messages;
    wxString m_offlineTopic;
    wxTextCtrl* m_history = nullptr;
    wxTextCtrl* m_question = nullptr;
    wxStaticText* m_status = nullptr;
    wxButton* m_guide = nullptr;
    wxButton* m_apply = nullptr;
    wxButton* m_cancel = nullptr;
    wxButton* m_reset = nullptr;
    wxCheckBox* m_images = nullptr;
    wxCheckBox* m_improve = nullptr;
    wxChoice* m_mode = nullptr;
    wxChoice* m_quick = nullptr;
    wxButton* m_runQuick = nullptr;
    wxSizer* m_onlineOptions = nullptr;
    wxSizer* m_offlineOptions = nullptr;
    bool m_stopping = false;
    bool m_ownerClosing = false;
    AiAssistantState m_state = AiAssistantState::Idle;
    std::function<void(AiAssistantState)> m_stateCallback;
};
