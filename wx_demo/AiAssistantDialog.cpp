#include "AiAssistantDialog.h"
#include "Canvas.h"
#include <wx/app.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <utility>

AiAssistantDialog::AiAssistantDialog(wxWindow* parent, Canvas& canvas)
    : wxDialog(parent, wxID_ANY, wxT("AI 助手"), wxDefaultPosition,
          parent->FromDIP(wxSize(560, 460)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_canvas(&canvas)
{
    SetBackgroundColour(wxColour(243, 246, 251));
    SetForegroundColour(wxColour(36, 50, 71));
    SetFont(wxFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI"))));
    SetMinSize(FromDIP(wxSize(410, 330)));

    m_history = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    m_history->SetName(wxT("对话记录"));
    m_history->SetBackgroundColour(*wxWHITE);
    auto* label = new wxStaticText(this, wxID_ANY, wxT("需求"));
    m_question = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(-1, 78)),
        wxTE_MULTILINE | wxTE_WORDWRAP);
    m_question->SetName(wxT("需求"));
    m_question->SetBackgroundColour(*wxWHITE);
    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_status->SetForegroundColour(wxColour(100, 116, 139));
    m_guide = new wxButton(this, wxID_ANY, wxT("给步骤"));
    m_apply = new wxButton(this, wxID_ANY, wxT("直接完成"));
    m_cancel = new wxButton(this, wxID_ANY, wxT("取消请求"));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    buttons->Add(m_cancel, 0, wxRIGHT, FromDIP(6));
    buttons->Add(m_guide, 0, wxRIGHT, FromDIP(6));
    buttons->Add(m_apply);
    auto* content = new wxBoxSizer(wxVERTICAL);
    content->Add(m_history, 1, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(label, 0, wxBOTTOM, FromDIP(5));
    content->Add(m_question, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(buttons, 0, wxEXPAND);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
    SetSizer(outer);
    CentreOnParent();

    m_guide->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Submit(AiAssistantMode::Guide); });
    m_apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Submit(AiAssistantMode::Apply); });
    m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_client.IsBusy()) return;
        m_stopping = true;
        m_status->SetLabel(wxT("正在取消…"));
        UpdateButtons();
        m_client.Cancel();
    });
    Bind(wxEVT_CLOSE_WINDOW, &AiAssistantDialog::OnClose, this);
    UpdateButtons();
}

void AiAssistantDialog::ShowAssistant()
{
    if (m_ownerClosing) return;
    Show();
    Raise();
    m_question->SetFocus();
}

void AiAssistantDialog::Submit(AiAssistantMode mode)
{
    auto* canvas = m_canvas.get();
    if (m_ownerClosing || m_client.IsBusy() || !canvas) return;
    wxString question = m_question->GetValue();
    question.Trim().Trim(false);
    if (question.empty())
    {
        m_status->SetLabel(wxT("请输入需求。"));
        m_question->SetFocus();
        return;
    }

    Pending pending;
    pending.serial = ++m_serial;
    pending.mode = mode;
    if (mode == AiAssistantMode::Apply)
    {
        pending.revision = canvas->GetDocumentRevision();
        pending.original = canvas->ExportDocument(); // stays in this process
    }
    const auto serial = pending.serial;
    m_pending = std::move(pending);
    wxWeakRef<AiAssistantDialog> dialog(this);
    wxString error;
    AppendHistory(mode == AiAssistantMode::Guide ? wxT("你 · 给步骤") : wxT("你 · 直接完成"), question);
    m_status->SetLabel(wxT("正在处理…"));
    if (!m_client.Start(mode, question, [dialog, serial](AiAssistantReply reply) {
        if (auto* window = dialog.get()) window->OnReply(serial, std::move(reply));
    }, error))
    {
        m_pending.reset();
        ReportError(error);
        UpdateButtons();
        return;
    }
    m_question->Clear();
    UpdateButtons();
}

void AiAssistantDialog::OnReply(std::uint64_t serial, AiAssistantReply reply)
{
    m_stopping = false;
    UpdateButtons();
    if (m_ownerClosing)
    {
        NotifyStopped();
        return;
    }
    if (!m_pending || m_pending->serial != serial)
    {
        m_status->SetLabel(wxEmptyString);
        return;
    }
    Pending pending = std::move(*m_pending);
    m_pending.reset();
    switch (reply.kind)
    {
    case AiAssistantReply::Kind::Cancelled:
        m_status->SetLabel(wxT("已取消。"));
        return;
    case AiAssistantReply::Kind::Error:
        ReportError(reply.text);
        return;
    case AiAssistantReply::Kind::Text:
        AppendHistory(wxT("AI 助手"), reply.text);
        m_status->SetLabel(wxT("已回复。"));
        return;
    case AiAssistantReply::Kind::Action:
        break;
    }
    if (pending.mode != AiAssistantMode::Apply)
    {
        ReportError(wxT("此请求只需要步骤，未修改电路。"));
        return;
    }
    auto* canvas = m_canvas.get();
    if (!canvas || canvas->GetDocumentRevision() != pending.revision)
    {
        ReportError(wxT("等待期间电路已更改，本次回复未应用。请重新提交。"));
        return;
    }
    CircuitDocument candidate;
    wxString error;
    if (!AiCircuitExecutor::BuildCandidate(pending.original, reply.action, candidate, error,
            canvas->GetClientSize()) ||
        !canvas->ApplyAssistantDocument(candidate, pending.revision, error))
    {
        ReportError(error.empty() ? wxString(wxT("无法添加此电路。")) : error);
        return;
    }
    AppendHistory(wxT("AI 助手"), wxString::Format(wxT("已新增 %u 个元件、%u 条连线。回到画布后可用 Ctrl+Z 整体撤销。"),
        static_cast<unsigned>(reply.action.components.size()), static_cast<unsigned>(reply.action.wires.size())));
    m_status->SetLabel(wxT("已应用到画布。"));
}

void AiAssistantDialog::OnClose(wxCloseEvent& event)
{
    m_pending.reset(); // No result can be applied after this window is closed.
    m_stopping = m_client.IsBusy();
    m_status->SetLabel(m_stopping ? wxString(wxT("正在取消…")) : wxString());
    UpdateButtons();
    if (event.CanVeto())
    {
        Hide();
        event.Veto();
    }
    else event.Skip();
    m_client.Cancel();
}

void AiAssistantDialog::BeginOwnerShutdown(std::function<void()> afterStop)
{
    m_ownerClosing = true;
    m_pending.reset();
    m_afterStop = std::move(afterStop);
    Hide();
    if (m_client.IsBusy()) m_client.Cancel();
    else NotifyStopped();
}

void AiAssistantDialog::ResumeAfterOwnerShutdown()
{
    m_ownerClosing = false;
    m_stopping = false;
    m_status->SetLabel(wxT("已取消。"));
    UpdateButtons();
}

void AiAssistantDialog::NotifyStopped()
{
    auto callback = std::exchange(m_afterStop, std::function<void()>{});
    if (callback && wxTheApp) wxTheApp->CallAfter(std::move(callback));
}

void AiAssistantDialog::UpdateButtons()
{
    const bool busy = m_client.IsBusy();
    m_guide->Enable(!busy && !m_ownerClosing);
    m_apply->Enable(!busy && !m_ownerClosing);
    m_cancel->Enable(busy && !m_stopping && !m_ownerClosing);
}

void AiAssistantDialog::AppendHistory(const wxString& title, const wxString& text)
{
    if (!m_transcript.empty()) m_transcript += "\n\n";
    m_transcript += title + "\n" + text;
    constexpr std::size_t MaxHistory = 64000;
    if (m_transcript.length() > MaxHistory) m_transcript = m_transcript.Right(MaxHistory);
    m_history->ChangeValue(m_transcript);
    m_history->SetInsertionPointEnd();
    m_history->ShowPosition(m_history->GetLastPosition());
}

void AiAssistantDialog::ReportError(const wxString& message)
{
    AppendHistory(wxT("AI 助手"), message.empty() ? wxString(wxT("请求未完成。")) : message);
    m_status->SetLabel(wxT("请求未完成。"));
}
