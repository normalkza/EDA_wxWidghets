#include "AiAssistantDialog.h"
#include "Canvas.h"
#include "AiSession.h"
#include "OfflineAssistant.h"
#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <utility>

AiAssistantDialog::AiAssistantDialog(wxWindow* parent, Canvas& canvas)
    : wxDialog(parent, wxID_ANY, wxT("AI 助手"), wxDefaultPosition,
          parent->FromDIP(wxSize(600, 520)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
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
    m_images = new wxCheckBox(this, wxID_ANY, wxS("附带画布图像"));
    m_images->SetValue(true);
    m_images->SetToolTip(wxS("发送当前电路绘图区的渲染图，不包含登录信息或其它窗口。元件、连线与文本数据始终随本次请求发送。"));
    m_improve = new wxCheckBox(this, wxID_ANY, wxS("自动改进（最多 3 轮）"));
    m_improve->SetValue(true);
    m_reset = new wxButton(this, wxID_ANY, wxS("新对话"));
    m_mode = new wxChoice(this, wxID_ANY);
    if (!AiSession::Get().IsOffline()) m_mode->Append(wxS("DeepSeek 联网助手"));
    m_mode->Append(wxS("离线工具箱（无需 API）")); m_mode->SetSelection(0);
    auto* modes = new wxBoxSizer(wxHORIZONTAL);
    modes->Add(new wxStaticText(this, wxID_ANY, wxS("模式")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    modes->Add(m_mode, 1, wxRIGHT, FromDIP(8)); modes->Add(m_reset);
    auto* options = new wxBoxSizer(wxHORIZONTAL);
    options->Add(m_images, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    options->Add(m_improve, 0, wxALIGN_CENTER_VERTICAL);
    m_onlineOptions = options;
    m_quick = new wxChoice(this, wxID_ANY);
    for (const auto* command : {L"新建全加器", L"新建半加器", L"新建双输入与门", L"新建双输入或门", L"新建非门",
            L"新建双输入异或门", L"新建双输入与非门", L"新建双输入或非门", L"整理当前布局", L"标注输入输出",
            L"检查当前电路", L"旋转选中元件", L"恢复自动走线"}) m_quick->Append(command);
    m_quick->SetSelection(0);
    m_runQuick = new wxButton(this, wxID_ANY, wxS("执行操作"));
    auto* localOptions = new wxBoxSizer(wxHORIZONTAL);
    localOptions->Add(m_quick, 1, wxRIGHT, FromDIP(8)); localOptions->Add(m_runQuick);
    m_offlineOptions = localOptions;

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    buttons->Add(m_cancel, 0, wxRIGHT, FromDIP(6));
    buttons->Add(m_guide, 0, wxRIGHT, FromDIP(6));
    buttons->Add(m_apply);
    auto* content = new wxBoxSizer(wxVERTICAL);
    content->Add(modes, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(m_history, 1, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(label, 0, wxBOTTOM, FromDIP(5));
    content->Add(m_question, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(options, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(localOptions, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    content->Add(buttons, 0, wxEXPAND);
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(content, 1, wxEXPAND | wxALL, FromDIP(12));
    SetSizer(outer);
    CentreOnParent();

    m_guide->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Submit(AiAssistantMode::Guide); });
    m_apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Submit(AiAssistantMode::Apply); });
    m_cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!IsBusy()) return;
        m_pending.reset();
        m_stopping = m_client.IsBusy();
        m_status->SetLabel(wxT("已取消；已经执行的修改可用 Ctrl+Z 撤销。"));
        NotifyState(AiAssistantState::Cancelled);
        UpdateButtons();
        m_client.Cancel();
    });
    m_reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (IsBusy()) return;
        m_messages.clear(); m_offlineTopic.clear(); m_transcript.clear(); m_history->Clear();
        m_status->SetLabel(wxS("已开始新对话。"));
    });
    m_mode->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateMode(); });
    m_quick->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { m_question->ChangeValue(m_quick->GetStringSelection()); });
    m_runQuick->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (IsBusy()) return;
        m_question->ChangeValue(m_quick->GetStringSelection()); Submit(AiAssistantMode::Apply);
    });
    Bind(wxEVT_CLOSE_WINDOW, &AiAssistantDialog::OnClose, this);
    UpdateMode();
}

bool AiAssistantDialog::IsOfflineMode() const
{
    return AiSession::Get().IsOffline() || m_mode->GetStringSelection() == wxS("离线工具箱（无需 API）");
}

void AiAssistantDialog::UpdateMode()
{
    const bool offline = IsOfflineMode();
    SetTitle(offline ? wxS("离线助手") : wxS("AI 助手"));
    m_onlineOptions->ShowItems(!offline); m_offlineOptions->ShowItems(offline);
    m_question->SetHint(offline ? wxS("例如：新建全加器、整理当前布局、标注输入输出") : wxS("输入要解释或完成的电路需求"));
    m_status->SetLabel(offline ? wxS("本地规则与模板，无需 API。") : wxS("DeepSeek 联网模式。"));
    Layout(); UpdateButtons();
}

void AiAssistantDialog::ShowAssistant()
{
    if (m_ownerClosing) return;
    Show();
    Raise();
    m_question->SetFocus();
}

void AiAssistantDialog::Remember(const wxString& role, const wxString& text)
{
    if (text.empty()) return;
    m_messages.push_back({role, text.Left(4000)});
    std::size_t length = 0;
    for (const auto& message : m_messages) length += message.text.length();
    while (m_messages.size() > 8 || length > 20000)
    {
        length -= m_messages.front().text.length(); m_messages.erase(m_messages.begin());
    }
}

void AiAssistantDialog::Submit(AiAssistantMode mode)
{
    if (m_ownerClosing || IsBusy() || !m_canvas.get()) return;
    wxString question = m_question->GetValue(); question.Trim().Trim(false);
    if (question.empty()) { m_status->SetLabel(wxS("请输入需求。")); m_question->SetFocus(); return; }
    Pending pending; pending.serial = ++m_serial; pending.mode = mode; pending.goal = question;
    AppendHistory(mode == AiAssistantMode::Guide ? wxS("你 · 给步骤") : wxS("你 · 直接完成"), question);
    Remember("user", question);
    StartRound(std::move(pending));
}

void AiAssistantDialog::StartRound(Pending pending, const wxString& feedback)
{
    if (IsOfflineMode()) { StartOffline(std::move(pending)); return; }
    auto* canvas = m_canvas.get();
    if (!canvas || m_ownerClosing) { m_pending.reset(); UpdateButtons(); return; }
    if (pending.round > 0 && canvas->GetDocumentRevision() != pending.revision)
    {
        m_pending.reset(); ReportError(wxS("画布已被手动更改，自动改进已停止。请按当前画布重新提交需求。")); UpdateButtons(); return;
    }
    AiWorkspaceContext context; wxString error;
    if (!canvas->CaptureAssistantContext(context, error, m_images->GetValue()))
    { m_pending.reset(); ReportError(error); UpdateButtons(); return; }
    context.history = m_messages; context.feedback = feedback; context.review = pending.round > 0;
    pending.original = canvas->ExportDocument(); pending.revision = canvas->GetDocumentRevision();
    const auto serial = pending.serial; const auto mode = pending.mode; const auto goal = pending.goal;
    const int round = pending.round;
    m_pending = std::move(pending);
    wxWeakRef<AiAssistantDialog> dialog(this);
    m_status->SetLabel(round == 0 ? wxString(wxS("正在分析当前画布…")) :
        wxString::Format(wxS("正在检查并改进（第 %d / 3 轮）…"), round + 1));
    if (!m_client.Start(mode, goal, [dialog, serial](AiAssistantReply reply) {
        if (auto* window = dialog.get()) window->OnReply(serial, std::move(reply));
    }, error, &context))
    { m_pending.reset(); ReportError(error); UpdateButtons(); return; }
    if (round == 0) m_question->Clear();
    NotifyState(AiAssistantState::Working); UpdateButtons();
}

void AiAssistantDialog::StartOffline(Pending pending)
{
    auto* canvas = m_canvas.get();
    if (!canvas || m_ownerClosing) { m_pending.reset(); UpdateButtons(); return; }
    pending.original = canvas->ExportDocument(); pending.revision = canvas->GetDocumentRevision();
    const auto selection = canvas->GetSelectedComponentData();
    const int selectedId = selection ? selection->id : 0;
    const auto available = canvas->GetClientSize(); const auto serial = pending.serial;
    m_pending = std::move(pending); m_question->Clear();
    m_status->SetLabel(wxS("正在本地处理…")); NotifyState(AiAssistantState::Working); UpdateButtons();
    wxWeakRef<AiAssistantDialog> dialog(this);
    wxTheApp->CallAfter([dialog, serial, selectedId, available] {
        auto* window = dialog.get();
        if (!window || window->m_ownerClosing || !window->m_pending || window->m_pending->serial != serial) return;
        auto pending = std::move(*window->m_pending); window->m_pending.reset();
        auto* canvas = window->m_canvas.get();
        if (!canvas || canvas->GetDocumentRevision() != pending.revision)
        { window->ReportError(wxS("画布已经更改，本次离线操作未应用，请重新执行。")); window->UpdateButtons(); return; }
        auto result = OfflineAssistant::Plan(pending.original, available, pending.goal,
            pending.mode == AiAssistantMode::Apply, selectedId, window->m_offlineTopic);
        if (result.kind == OfflineAssistant::Result::Kind::Error)
        { window->ReportError(result.text); window->UpdateButtons(); return; }
        wxString error;
        const bool edit = result.kind == OfflineAssistant::Result::Kind::Edit;
        if (edit && !canvas->ApplyAssistantDocument(result.document, pending.revision, error))
        { window->ReportError(error); window->UpdateButtons(); return; }
        if (!result.topic.empty()) window->m_offlineTopic = result.topic;
        const wxString message = result.text + (edit ? wxString(wxS(" 可用 Ctrl+Z 整体撤销。")) : wxString());
        window->AppendHistory(wxS("离线工具箱"), message); window->Remember("assistant", message);
        window->m_status->SetLabel(edit ? wxS("已离线完成，可撤销。") : wxS("本地步骤已显示。"));
        window->NotifyState(edit ? AiAssistantState::Applied : AiAssistantState::Answered); window->UpdateButtons();
    });
}

void AiAssistantDialog::QueueRound(Pending pending, const wxString& feedback)
{
    ++pending.round;
    const auto serial = pending.serial;
    m_pending = std::move(pending); UpdateButtons();
    wxWeakRef<AiAssistantDialog> dialog(this);
    wxTheApp->CallAfter([dialog, serial, feedback] {
        auto* window = dialog.get();
        if (!window || !window->m_pending || window->m_pending->serial != serial || window->m_ownerClosing) return;
        auto pending = std::move(*window->m_pending);
        window->StartRound(std::move(pending), feedback);
    });
}

void AiAssistantDialog::OnReply(std::uint64_t serial, AiAssistantReply reply)
{
    m_stopping = false;
    if (m_ownerClosing) { m_pending.reset(); NotifyStopped(); return; }
    if (!m_pending || m_pending->serial != serial) { UpdateButtons(); return; }
    Pending pending = std::move(*m_pending); m_pending.reset();
    if (reply.kind == AiAssistantReply::Kind::Cancelled)
    { m_status->SetLabel(wxS("已取消。")); NotifyState(AiAssistantState::Cancelled); UpdateButtons(); return; }
    if (reply.kind == AiAssistantReply::Kind::Error)
    {
        ReportError(reply.text + (pending.applied ? wxString(wxS(" 此前已执行的修改仍保留，可用 Ctrl+Z 整体撤销。")) : wxString()));
        UpdateButtons(); return;
    }
    auto* canvas = m_canvas.get();
    if (!canvas || canvas->GetDocumentRevision() != pending.revision)
    {
        // Old tests and read-only replies do not hold a mutation revision.
        if (pending.mode == AiAssistantMode::Apply)
        { ReportError(wxS("等待期间画布已更改，本次回复未应用。请重新提交。")); UpdateButtons(); return; }
    }
    if (reply.kind == AiAssistantReply::Kind::Text)
    {
        AppendHistory(wxS("AI 助手"), reply.text); Remember("assistant", reply.text);
        m_status->SetLabel(pending.applied ? wxS("检查完成；Ctrl+Z 可撤销本次修改。") : wxS("已回复。"));
        NotifyState(pending.applied ? AiAssistantState::Applied : AiAssistantState::Answered);
        UpdateButtons(); return;
    }
    if (pending.mode != AiAssistantMode::Apply || !canvas)
    { ReportError(wxS("指导模式不会更改画布。")); UpdateButtons(); return; }
    const bool patch = reply.kind == AiAssistantReply::Kind::Patch;
    CircuitDocument candidate; wxString error;
    const bool built = patch ? AiCircuitExecutor::BuildPatchCandidate(pending.original, reply.patch, candidate, error, canvas->GetClientSize()) :
        AiCircuitExecutor::BuildCandidate(pending.original, reply.action, candidate, error, canvas->GetClientSize());
    const bool applied = built && canvas->ApplyAssistantDocument(candidate, pending.revision, error, pending.applied > 0);
    const bool again = patch && m_improve->GetValue() && pending.round < 2;
    if (!applied)
    {
        const wxString message = error.empty() ? wxString(wxS("无法应用此电路修改。")) : error;
        if (again && canvas->GetDocumentRevision() == pending.revision)
        {
            AppendHistory(wxS("本地校验"), message + wxS(" 正在让助手修正。"));
            QueueRound(std::move(pending), wxS("上一轮方案：") + reply.patch.summary.Left(2000) +
                wxS("\n本地校验失败，本批未修改画布：") + message + wxS("\n请根据最新画布修正操作，已经满足目标则直接说明。"));
        }
        else
        {
            ReportError(message + (pending.applied ? wxString(wxS(" 此前已执行的修改仍保留，可用 Ctrl+Z 整体撤销。")) : wxString()));
            UpdateButtons();
        }
        return;
    }
    ++pending.applied; pending.revision = canvas->GetDocumentRevision();
    const wxString message = patch ? wxString::Format(wxS("已执行 %zu 项画布修改。"), reply.patch.operations.size()) + reply.patch.summary :
        wxString::Format(wxS("已新增 %zu 个元件、%zu 条连线。"), reply.action.components.size(), reply.action.wires.size());
    AppendHistory(wxS("本地执行"), message); Remember("assistant", message);
    if (again)
    {
        QueueRound(std::move(pending), wxS("执行成功：") + message +
            wxS("\n这是执行后的最新画布和渲染图。检查用户目标是否已满足；满足就简要总结，不再调用工具。只有确有剩余问题时继续修改。"));
        return;
    }
    AppendHistory(wxS("AI 助手"), patch && pending.round >= 2 ? wxS("本次修改已应用，已达到三轮上限；如需进一步调整可继续输入。Ctrl+Z 可整体撤销。") :
        wxS("已应用到画布，Ctrl+Z 可整体撤销。"));
    m_status->SetLabel(wxS("已应用到画布。")); NotifyState(AiAssistantState::Applied); UpdateButtons();
}

void AiAssistantDialog::OnClose(wxCloseEvent& event)
{
    m_pending.reset(); // No result can be applied after this window is closed.
    m_stopping = m_client.IsBusy();
    NotifyState(m_stopping ? AiAssistantState::Cancelled : AiAssistantState::Idle);
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
    NotifyState(AiAssistantState::Cancelled);
    UpdateButtons();
}

void AiAssistantDialog::NotifyStopped()
{
    auto callback = std::exchange(m_afterStop, std::function<void()>{});
    if (callback && wxTheApp) wxTheApp->CallAfter(std::move(callback));
}

void AiAssistantDialog::UpdateButtons()
{
    const bool busy = IsBusy();
    const bool offline = IsOfflineMode();
    m_mode->Enable(!busy && !m_ownerClosing);
    m_quick->Enable(!busy && !m_ownerClosing); m_runQuick->Enable(!busy && !m_ownerClosing);
    m_reset->Enable(!busy && !m_ownerClosing);
    m_images->Enable(!offline && !busy && !m_ownerClosing);
    m_improve->Enable(!offline && !busy && !m_ownerClosing);
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
    AppendHistory(IsOfflineMode() ? wxS("离线工具箱") : wxS("AI 助手"), message.empty() ? wxString(wxT("请求未完成。")) : message);
    m_status->SetLabel(wxT("请求未完成。"));
    NotifyState(AiAssistantState::Failed);
}

void AiAssistantDialog::SetStateCallback(std::function<void(AiAssistantState)> callback)
{
    m_stateCallback = std::move(callback);
    if (m_stateCallback) m_stateCallback(m_state);
}

void AiAssistantDialog::NotifyState(AiAssistantState state)
{
    m_state = state;
    if (m_stateCallback) m_stateCallback(state);
}
