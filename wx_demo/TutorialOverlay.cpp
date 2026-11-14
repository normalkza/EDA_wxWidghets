#include "TutorialOverlay.h"
#include <wx/dcbuffer.h>
#include <wx/region.h>

wxBEGIN_EVENT_TABLE(TutorialOverlay, wxFrame)
EVT_PAINT(TutorialOverlay::OnPaint)
EVT_SIZE(TutorialOverlay::OnSize)
wxEND_EVENT_TABLE()



TutorialOverlay::TutorialOverlay(wxWindow* parent)
    : wxFrame(
        parent,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxFRAME_NO_TASKBAR |
        wxSTAY_ON_TOP |
        wxFRAME_SHAPED |
        wxBORDER_NONE
    )
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    SetTransparent(175);

    
    m_panelFrame = new wxFrame(
        parent,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxFRAME_NO_TASKBAR |
        wxSTAY_ON_TOP |
        wxBORDER_NONE
    );

    m_panel = new wxPanel(m_panelFrame, wxID_ANY);

    m_panel->SetBackgroundColour(
        wxColour(225, 239, 255)
    );

    auto* root = new wxBoxSizer(wxVERTICAL);

    // 标题
    m_title = new wxStaticText(
        m_panel,
        wxID_ANY,
        wxT("新手教程")
    );

    m_title->SetForegroundColour(
        wxColour(25, 65, 130)
    );

    m_title->SetFont(
        wxFontInfo(12).Bold()
    );

    // 提示内容
    m_message = new wxStaticText(
        m_panel,
        wxID_ANY,
        wxEmptyString
    );

    m_message->SetForegroundColour(
        wxColour(35, 55, 85)
    );

    m_message->Wrap(330);

    // 进度
    m_progress = new wxStaticText(
        m_panel,
        wxID_ANY,
        wxEmptyString
    );

    m_progress->SetForegroundColour(
        wxColour(65, 90, 130)
    );

    root->Add(
        m_title,
        0,
        wxEXPAND | wxALL,
        12
    );

    root->Add(
        m_message,
        1,
        wxEXPAND | wxLEFT | wxRIGHT,
        12
    );

    root->Add(
        m_progress,
        0,
        wxLEFT | wxRIGHT | wxTOP,
        12
    );

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);

    m_previousButton = new wxButton(
        m_panel,
        wxID_ANY,
        wxT("上一步")
    );

    m_nextButton = new wxButton(
        m_panel,
        wxID_ANY,
        wxT("下一步")
    );

    m_cancelButton = new wxButton(
        m_panel,
        wxID_ANY,
        wxT("结束教程")
    );

    buttons->Add(
        m_previousButton,
        0,
        wxRIGHT,
        6
    );

    buttons->Add(
        m_nextButton,
        0,
        wxRIGHT,
        6
    );

    buttons->Add(
        m_cancelButton,
        0
    );

    root->Add(
        buttons,
        0,
        wxALL | wxALIGN_RIGHT,
        12
    );

    m_panel->SetSizer(root);

    // 绑定按钮事件
    m_previousButton->Bind(
        wxEVT_BUTTON,
        &TutorialOverlay::OnPrevious,
        this
    );

    m_nextButton->Bind(
        wxEVT_BUTTON,
        &TutorialOverlay::OnNext,
        this
    );

    m_cancelButton->Bind(
        wxEVT_BUTTON,
        &TutorialOverlay::OnCancel,
        this
    );

    m_panelFrame->SetMinSize(
        wxSize(390, 190)
    );

    m_panelFrame->Hide();

    // 默认不允许点击下一步
    SetNextEnabled(false);
}


void TutorialOverlay::SetNextEnabled(bool enabled)
{
    m_nextEnabled = enabled;

    if (m_nextButton)
    {
        m_nextButton->Enable(enabled);
    }
}


void TutorialOverlay::SetStep(
    const wxString& title,
    const wxString& message,
    const wxString& progress,
    const wxRect& targetRect,
    const wxRect& secondTargetRect
)
{
    if (!m_panelFrame || !m_panel)
        return;

    m_title->SetLabel(title);
    m_message->SetLabel(message);
    m_progress->SetLabel(progress);

    // 保存两个透明挖空区域
    m_targetRect = targetRect;
    m_secondTargetRect = secondTargetRect;

    m_message->Wrap(330);

    m_panel->Layout();
    m_panel->Fit();

    m_panelFrame->Layout();
    m_panelFrame->Fit();

    wxWindow* parent = GetParent();

    if (!parent)
        return;

    const wxSize parentSize = parent->GetClientSize();

    const wxPoint parentScreenPos =
        parent->ClientToScreen(wxPoint(0, 0));

    const wxSize panelSize = m_panelFrame->GetSize();

    int panelX = parentSize.x - panelSize.x - 20;
    int panelY = 80;

    if (panelX < 0)
        panelX = 10;

    if (panelY + panelSize.y > parentSize.y)
        panelY = parentSize.y - panelSize.y - 10;

    if (panelY < 0)
        panelY = 10;

    m_panelFrame->SetPosition(
        parentScreenPos + wxPoint(panelX, panelY)
    );

    SetNextEnabled(m_nextEnabled);

    UpdateOverlay();

    Show();
    Raise();

    m_panelFrame->Show();
    m_panelFrame->Raise();
}


void TutorialOverlay::UpdateOverlay()
{
    wxWindow* parent = GetParent();

    if (!parent)
        return;

    // 获取主窗口客户区的屏幕位置
    wxPoint parentScreenPos =
        parent->ClientToScreen(wxPoint(0, 0));

    wxSize parentSize = parent->GetClientSize();

    // 设置遮罩窗口的位置和大小
    SetPosition(parentScreenPos);
    SetSize(parentSize);

    // 获取遮罩窗口自己的屏幕原点
    wxPoint overlayScreenPos =
        ClientToScreen(wxPoint(0, 0));

    // 建立完整遮罩区域
    wxRegion region(
        0,
        0,
        GetClientSize().x,
        GetClientSize().y
    );

    // 第一个透明区域
    if (!m_targetRect.IsEmpty())
    {
        wxRect hole = m_targetRect;

        hole.SetPosition(
            hole.GetPosition() - overlayScreenPos
        );

        hole.Inflate(6, 6);

        region.Subtract(hole);
    }

    // 第二个透明区域
    if (!m_secondTargetRect.IsEmpty())
    {
        wxRect hole = m_secondTargetRect;

        hole.SetPosition(
            hole.GetPosition() - overlayScreenPos
        );

        hole.Inflate(6, 6);

        region.Subtract(hole);
    }

    SetShape(region);

    Refresh(false);
}


void TutorialOverlay::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);

    dc.SetBackground(wxBrush(wxColour(0, 0, 0)));
    dc.Clear();

    wxWindow* parent = GetParent();

    if (!parent)
        return;

    // 绘制第一个高亮框
    if (!m_targetRect.IsEmpty())
    {
        wxRect hole = m_targetRect;

        hole.SetPosition(
            parent->ScreenToClient(
                m_targetRect.GetPosition()
            )
        );

        hole.Inflate(6, 6);

        wxPen pen(
            wxColour(255, 205, 40),
            2,
            wxPENSTYLE_SHORT_DASH
        );

        dc.SetPen(pen);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);

        dc.DrawRectangle(hole);
    }

    // 绘制第二个高亮框
    if (!m_secondTargetRect.IsEmpty())
    {
        wxRect secondHole = m_secondTargetRect;

        secondHole.SetPosition(
            parent->ScreenToClient(
                m_secondTargetRect.GetPosition()
            )
        );

        secondHole.Inflate(6, 6);

        wxPen pen(
            wxColour(255, 205, 40),
            2,
            wxPENSTYLE_SHORT_DASH
        );

        dc.SetPen(pen);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);

        dc.DrawRectangle(secondHole);
    }
}


void TutorialOverlay::OnPrevious(wxCommandEvent&)
{
    if (m_previousCallback)
    {
        m_previousCallback();
    }
}


void TutorialOverlay::OnNext(wxCommandEvent&)
{
    if (m_nextEnabled && m_nextCallback)
    {
        m_nextCallback();
    }
}


void TutorialOverlay::OnCancel(wxCommandEvent&)
{
    if (m_cancelCallback)
    {
        m_cancelCallback();
    }
}

void TutorialOverlay::HideOverlay()
{
    // 隐藏教程遮罩
    Hide();

    // 隐藏独立的教程说明框
    if (m_panelFrame)
        m_panelFrame->Hide();
}

void TutorialOverlay::OnSize(wxSizeEvent& event)
{
    event.Skip();

    if (IsShown())
    {
        UpdateOverlay();
    }
}
