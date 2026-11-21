#pragma once

#include <wx/frame.h>
#include <wx/timer.h>
#include <functional>
#include <memory>

// 透明显示层只覆盖编辑画布；原生输入由组合控制器转发。
class PetWebView final : public wxFrame
{
public:
    PetWebView(wxWindow* owner, wxWindow* workspace);
    ~PetWebView() override;
    void SetActionCallback(std::function<void(const wxString&)> callback);
    void SetStatusCallback(std::function<void(const wxString&)> callback);
    void SetPetState(const wxString& state);
    void SetPageVisible(bool visible);
    void Reload();
    bool IsReady() const;
    void SyncBounds();
    void ReadDiagnostics(std::function<void(const wxString&)> callback);

    static wxString FindResourceDirectory();
    static wxString RuntimeDirectory();
    static bool IsAllowedAction(const wxString& command);

private:
    struct State;
    void StartBrowser();
    void ShowFailure(const wxString& message, const char* stage, long result = 0);
    void UpdateBounds();
    void ProbePointer();
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
    std::shared_ptr<State> m_state;
    bool m_comInitialized = false;
    wxTimer m_startupTimer;
    wxTimer m_pointerTimer;
};
