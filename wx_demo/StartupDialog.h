#pragma once

#include <wx/dialog.h>
#include <wx/timer.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

class wxPanel;
class wxTextCtrl;
class wxStaticText;
class wxButton;
class wxGauge;

class StartupDialog final : public wxDialog
{
public:
    StartupDialog();
    ~StartupDialog() override;

private:
    friend struct OfflineRegressionAccess;
    void Login();
    void EnterOffline();
    void ShowWelcome(const wxString& username);
    void Back();
    void Finish();
    void Cancel();
    void StopWorker();
    wxPanel* m_login = nullptr;
    wxPanel* m_welcome = nullptr;
    wxTextCtrl* m_username = nullptr;
    wxTextCtrl* m_key = nullptr;
    wxStaticText* m_error = nullptr;
    wxStaticText* m_greeting = nullptr;
    wxStaticText* m_progressText = nullptr;
    wxGauge* m_progress = nullptr;
    wxButton* m_enter = nullptr;
    wxTimer m_timer;
    std::thread m_worker;
    std::atomic_bool m_cancelled{false};
    std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
    std::chrono::steady_clock::time_point m_started;
    bool m_ready = false;
};
