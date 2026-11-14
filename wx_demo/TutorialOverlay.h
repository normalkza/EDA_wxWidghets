
#pragma once

#include <wx/wx.h>
#include <functional>
#include <utility>

class TutorialOverlay : public wxFrame
{
public:
    explicit TutorialOverlay(wxWindow* parent);

    void SetStep(
        const wxString& title,
        const wxString& message,
        const wxString& progress,
        const wxRect& targetRect,
        const wxRect& secondTargetRect = wxRect()
    );

    void SetPreviousCallback(std::function<void()> callback)
    {
        m_previousCallback = std::move(callback);
    }

    void SetNextCallback(std::function<void()> callback)
    {
        m_nextCallback = std::move(callback);
    }

    void SetCancelCallback(std::function<void()> callback)
    {
        m_cancelCallback = std::move(callback);
    }

    void SetNextEnabled(bool enabled);

    void UpdateOverlay();
    void HideOverlay();

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);

    void OnPrevious(wxCommandEvent& event);
    void OnNext(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    // 单独的教程说明窗口，不跟随遮罩变透明
    wxFrame* m_panelFrame = nullptr;
    wxPanel* m_panel = nullptr;

    wxStaticText* m_title = nullptr;
    wxStaticText* m_message = nullptr;
    wxStaticText* m_progress = nullptr;

    wxButton* m_previousButton = nullptr;
    wxButton* m_nextButton = nullptr;
    wxButton* m_cancelButton = nullptr;

    wxRect m_targetRect;
    wxRect m_secondTargetRect;
    std::function<void()> m_previousCallback;
    std::function<void()> m_nextCallback;
    std::function<void()> m_cancelCallback;

    bool m_nextEnabled = false;

    wxDECLARE_EVENT_TABLE();
};
