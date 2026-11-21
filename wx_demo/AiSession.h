#pragma once

#include <wx/string.h>
#include <atomic>

// 本机登录会话：只持有用户名、私有本地服务地址与临时认证令牌。
class AiSession final
{
public:
    static AiSession& Get();
    ~AiSession();
    bool Start(const wxString& username, const wxString& apiKey,
        const std::atomic_bool& cancelled, wxString& error);
    void Stop();
    void EnterOffline(const wxString& username);
    bool IsOffline() const { return m_offline; }
    const wxString& UserName() const { return m_username; }
    wxString Endpoint() const;
    const wxString& Token() const { return m_token; }

private:
    friend struct AiSkillRegressionAccess;
    AiSession() = default;
    AiSession(const AiSession&) = delete;
    AiSession& operator=(const AiSession&) = delete;
    wxString m_username, m_endpoint, m_token;
    bool m_offline = false;
    void* m_process = nullptr;
    void* m_input = nullptr;
    void* m_output = nullptr;
    void* m_job = nullptr;
};
