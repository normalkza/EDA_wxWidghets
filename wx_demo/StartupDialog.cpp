#include "StartupDialog.h"
#include "AiSession.h"
#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcbuffer.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/msw/wrapwin.h>
#include <algorithm>

namespace
{
class BrandPanel final : public wxPanel
{
public:
    explicit BrandPanel(wxWindow* parent) : wxPanel(parent)
    {
        SetMinSize(FromDIP(wxSize(270, 440)));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
            wxAutoBufferedPaintDC dc(this);
            const auto size = GetClientSize();
            dc.GradientFillLinear(wxRect(wxPoint(), size), wxColour(30, 75, 174), wxColour(53, 123, 225), wxSOUTH);
            dc.SetPen(wxPen(wxColour(72, 125, 211)));
            const int step = FromDIP(26);
            for (int x = 0; x < size.x; x += step) dc.DrawLine(x, 0, x, size.y);
            for (int y = 0; y < size.y; y += step) dc.DrawLine(0, y, size.x, y);
            dc.SetTextForeground(*wxWHITE);
            dc.SetFont(wxFontInfo(21).FaceName("Microsoft YaHei UI").Bold());
            dc.DrawText(wxS("连接想法"), FromDIP(27), FromDIP(42));
            dc.DrawText(wxS("构建电路"), FromDIP(27), FromDIP(82));
            dc.SetFont(wxFontInfo(10).FaceName("Microsoft YaHei UI"));
            dc.DrawText(wxS("设计 · 仿真 · AI 助手"), FromDIP(29), FromDIP(141));
            dc.SetPen(wxPen(wxColour(173, 212, 255), FromDIP(2)));
            dc.SetBrush(wxBrush(wxColour(54, 111, 207)));
            const int x = FromDIP(100), y = FromDIP(231), w = FromDIP(78), h = FromDIP(67);
            dc.DrawRoundedRectangle(x, y, w, h, FromDIP(16));
            dc.DrawLine(FromDIP(48), y + h / 3, x, y + h / 3);
            dc.DrawLine(FromDIP(48), y + h * 2 / 3, x, y + h * 2 / 3);
            dc.DrawLine(x + w, y + h / 2, FromDIP(224), y + h / 2);
            dc.SetBrush(wxBrush(wxColour(198, 225, 255)));
            for (auto point : {wxPoint(48, 253), wxPoint(48, 275), wxPoint(224, 264)})
                dc.DrawCircle(FromDIP(point), FromDIP(4));
            dc.SetFont(wxFontInfo(19).Bold());
            dc.DrawText("&", x + FromDIP(27), y + FromDIP(17));
            dc.SetFont(wxFontInfo(9).FaceName("Microsoft YaHei UI"));
            dc.SetTextForeground(wxColour(202, 224, 255));
            dc.DrawText(wxS("数字电路工作台"), FromDIP(29), size.y - FromDIP(45));
        });
    }
};
wxStaticText* Text(wxWindow* parent, const wxString& label, int size, bool bold = false)
{
    auto* text = new wxStaticText(parent, wxID_ANY, label);
    auto font = wxFontInfo(size).FaceName("Microsoft YaHei UI");
    if (bold) font.Bold();
    text->SetFont(font);
    return text;
}
}

StartupDialog::StartupDialog()
    : wxDialog(nullptr, wxID_ANY, wxS("登录 · 数字电路编辑器"), wxDefaultPosition,
        wxSize(830, 530), wxDEFAULT_DIALOG_STYLE), m_timer(this)
{
    SetFont(wxFontInfo(10).FaceName("Microsoft YaHei UI"));
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour(wxColour(36, 50, 71));
    auto* brand = new BrandPanel(this);
    auto* pages = new wxPanel(this);
    pages->SetBackgroundColour(*wxWHITE);
    m_login = new wxPanel(pages);
    m_welcome = new wxPanel(pages);
    m_login->SetBackgroundColour(*wxWHITE);
    m_welcome->SetBackgroundColour(*wxWHITE);

    auto* form = new wxBoxSizer(wxVERTICAL);
    form->AddStretchSpacer();
    form->Add(Text(m_login, wxS("欢迎使用"), 23, true), 0, wxBOTTOM, FromDIP(10));
    auto* subtitle = Text(m_login, wxS("登录你的电路工作台"), 10);
    subtitle->SetForegroundColour(wxColour(111, 126, 148));
    form->Add(subtitle, 0, wxBOTTOM, FromDIP(28));
    form->Add(Text(m_login, wxS("用户名"), 10, true), 0, wxBOTTOM, FromDIP(7));
    m_username = new wxTextCtrl(m_login, wxID_ANY, wxEmptyString, wxDefaultPosition,
        FromDIP(wxSize(350, 36)), wxTE_PROCESS_ENTER);
    m_username->SetHint(wxS("请输入用户名"));
    m_username->SetMaxLength(32);
    form->Add(m_username, 0, wxEXPAND | wxBOTTOM, FromDIP(17));
    form->Add(Text(m_login, "DeepSeek API Key", 10, true), 0, wxBOTTOM, FromDIP(7));
    m_key = new wxTextCtrl(m_login, wxID_ANY, wxEmptyString, wxDefaultPosition,
        FromDIP(wxSize(350, 36)), wxTE_PASSWORD | wxTE_PROCESS_ENTER);
    m_key->SetHint(wxS("请输入 API Key"));
    m_key->SetMaxLength(1024);
    form->Add(m_key, 0, wxEXPAND | wxBOTTOM, FromDIP(8));
    auto* showKey = new wxCheckBox(m_login, wxID_ANY, wxS("显示密钥"));
    form->Add(showKey, 0, wxBOTTOM, FromDIP(9));
    auto* privacy = Text(m_login, wxS("密钥仅用于本次运行，不保存到工程文件。"), 9);
    privacy->SetForegroundColour(wxColour(121, 135, 155));
    form->Add(privacy, 0, wxBOTTOM, FromDIP(10));
    m_error = Text(m_login, wxEmptyString, 9);
    m_error->SetForegroundColour(wxColour(198, 65, 72));
    m_error->SetMinSize(FromDIP(wxSize(-1, 24)));
    form->Add(m_error, 0, wxEXPAND | wxBOTTOM, FromDIP(9));
    auto* submit = new wxButton(m_login, wxID_ANY, wxS("登录并继续"), wxDefaultPosition, FromDIP(wxSize(-1, 42)));
    submit->SetBackgroundColour(wxColour(53, 120, 229));
    submit->SetForegroundColour(*wxWHITE);
    submit->SetDefault();
    form->Add(submit, 0, wxEXPAND);
    auto* offline = new wxButton(m_login, wxID_ANY, wxS("无需 API，直接使用"), wxDefaultPosition,
        FromDIP(wxSize(-1, 40)));
    form->Add(offline, 0, wxEXPAND | wxTOP, FromDIP(10));
    form->AddStretchSpacer();
    auto* formOuter = new wxBoxSizer(wxVERTICAL);
    formOuter->Add(form, 1, wxEXPAND | wxALL, FromDIP(34));
    m_login->SetSizer(formOuter);

    auto* welcome = new wxBoxSizer(wxVERTICAL);
    welcome->AddStretchSpacer();
    auto* mark = Text(m_welcome, wxS("你好，新灵感"), 11, true);
    mark->SetForegroundColour(wxColour(53, 120, 229));
    welcome->Add(mark, 0, wxBOTTOM, FromDIP(18));
    m_greeting = Text(m_welcome, wxS("欢迎来到工作台"), 22, true);
    welcome->Add(m_greeting, 0, wxBOTTOM, FromDIP(14));
    welcome->Add(Text(m_welcome, wxS("让我们开始今天的电路设计。"), 11), 0, wxBOTTOM, FromDIP(35));
    m_progress = new wxGauge(m_welcome, wxID_ANY, 100, wxDefaultPosition, FromDIP(wxSize(350, 7)), wxGA_SMOOTH);
    welcome->Add(m_progress, 0, wxEXPAND | wxBOTTOM, FromDIP(15));
    m_progressText = Text(m_welcome, wxS("正在准备 AI 助手…"), 10);
    m_progressText->SetForegroundColour(wxColour(111, 126, 148));
    m_progressText->SetMinSize(FromDIP(wxSize(-1, 53)));
    welcome->Add(m_progressText, 0, wxEXPAND | wxBOTTOM, FromDIP(20));
    m_enter = new wxButton(m_welcome, wxID_ANY, wxS("进入工作区"), wxDefaultPosition, FromDIP(wxSize(-1, 40)));
    welcome->Add(m_enter, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    auto* back = new wxButton(m_welcome, wxID_ANY, wxS("返回登录"));
    welcome->Add(back, 0, wxALIGN_CENTER);
    welcome->AddStretchSpacer();
    auto* welcomeOuter = new wxBoxSizer(wxVERTICAL);
    welcomeOuter->Add(welcome, 1, wxEXPAND | wxALL, FromDIP(34));
    m_welcome->SetSizer(welcomeOuter);
    auto* pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(m_login, 1, wxEXPAND);
    pageSizer->Add(m_welcome, 1, wxEXPAND);
    pages->SetSizer(pageSizer);
    m_welcome->Hide();
    auto* outer = new wxBoxSizer(wxHORIZONTAL);
    outer->Add(brand, 0, wxEXPAND);
    outer->Add(pages, 1, wxEXPAND);
    SetSizer(outer);
    SetSize(FromDIP(wxSize(810, 610)));
    CentreOnScreen();

    submit->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Login(); });
    offline->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EnterOffline(); });
    m_username->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Login(); });
    m_key->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Login(); });
    showKey->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        ::SendMessageW(static_cast<HWND>(m_key->GetHandle()), EM_SETPASSWORDCHAR,
            event.IsChecked() ? 0 : static_cast<WPARAM>(0x25CF), 0);
        m_key->Refresh();
    });
    m_enter->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Finish(); });
    back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Back(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Cancel(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Cancel(); }, wxID_CANCEL);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_started).count();
        if (m_ready)
        {
            m_progress->SetValue(100);
            if (elapsed >= 2000) Finish();
        }
        else m_progress->SetValue((std::min)(85, static_cast<int>(elapsed / 150)));
    }, m_timer.GetId());
    m_username->SetFocus();
}

StartupDialog::~StartupDialog()
{
    StopWorker();
    if (GetReturnCode() != wxID_OK) AiSession::Get().Stop();
}

void StartupDialog::StopWorker()
{
    m_timer.Stop();
    m_lifetime.reset();
    m_cancelled.store(true);
    if (m_worker.joinable()) m_worker.join();
}

void StartupDialog::Login()
{
    if (!m_login->IsShown()) return;
    wxString username = m_username->GetValue(), key = m_key->GetValue();
    username.Trim().Trim(false); key.Trim().Trim(false);
    if (username.empty() || username.length() > 32)
    {
        m_error->SetLabel(wxS("请输入 1 至 32 字的用户名。")); m_username->SetFocus(); return;
    }
    bool valid = !key.empty() && key.length() <= 1024;
    for (const auto character : key) if (character < 33 || character > 126) valid = false;
    if (!valid)
    {
        m_error->SetLabel(wxS("请输入有效的 API Key，不要包含空格或换行。")); m_key->SetFocus(); return;
    }
    ShowWelcome(username);
    const std::weak_ptr<int> alive = m_lifetime;
    m_worker = std::thread([this, alive, username, key]() mutable {
        wxString error;
        const bool success = AiSession::Get().Start(username, key, m_cancelled, error);
        key.clear();
        wxTheApp->CallAfter([this, alive, success, error] {
            if (!alive.lock()) return;
            if (m_worker.joinable()) m_worker.join();
            m_ready = success;
            if (success)
            {
                m_key->Clear();
                m_progressText->SetLabel(wxS("准备完成，即将进入工作区。"));
                m_enter->Enable();
            }
            else
            {
                m_timer.Stop();
                m_progressText->SetForegroundColour(wxColour(198, 65, 72));
                m_progressText->SetLabel(error.empty() ? wxString(wxS("连接已取消，请返回登录后重试。")) : error);
                m_progressText->Wrap(FromDIP(350));
            }
            m_welcome->Layout();
        });
    });
}

void StartupDialog::EnterOffline()
{
    if (!m_login->IsShown()) return;
    wxString username = m_username->GetValue(); username.Trim().Trim(false);
    if (username.empty()) username = wxS("访客");
    AiSession::Get().EnterOffline(username);
    m_key->Clear();
    ShowWelcome(username);
    m_ready = true; m_progress->SetValue(100); m_enter->Enable();
    m_progressText->SetLabel(wxS("离线模式已就绪，即将进入工作区。"));
}

void StartupDialog::ShowWelcome(const wxString& username)
{
    m_error->SetLabel(wxEmptyString);
    m_login->Hide(); m_welcome->Show();
    m_greeting->SetLabel(wxS("欢迎，") + wxControl::EscapeMnemonics(username));
    m_greeting->Wrap(FromDIP(350));
    m_progressText->SetLabel(wxS("正在准备 AI 助手…"));
    m_progressText->SetForegroundColour(wxColour(111, 126, 148));
    m_progress->SetValue(0); m_enter->Disable(); m_ready = false;
    SetTitle(wxS("欢迎 · 数字电路编辑器"));
    Layout();
    m_started = std::chrono::steady_clock::now();
    m_cancelled.store(false);
    m_lifetime = std::make_shared<int>(0);
    m_timer.Start(50);
}

void StartupDialog::Finish()
{
    if (!m_ready) return;
    m_timer.Stop();
    EndModal(wxID_OK);
}
void StartupDialog::Back()
{
    StopWorker(); AiSession::Get().Stop();
    m_welcome->Hide(); m_login->Show(); m_ready = false;
    SetTitle(wxS("登录 · 数字电路编辑器"));
    Layout(); m_username->SetFocus();
}
void StartupDialog::Cancel()
{
    StopWorker(); AiSession::Get().Stop();
    EndModal(wxID_CANCEL);
}
