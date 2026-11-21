#include "AiSession.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/msw/wrapwin.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace
{
wxString FindBroker()
{
    wxFileName folder = wxFileName::DirName(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
    for (int depth = 0; depth < 6; ++depth)
    {
        wxFileName script(folder.GetPath(), "assistant_server.py");
        if (script.FileExists()) return script.GetFullPath();
        if (!folder.GetDirCount()) break;
        folder.RemoveLastDir();
    }
    return {};
}
wxString FindPython(bool& launcher)
{
    wchar_t filename[32768];
    for (const auto* candidate : {L"py.exe", L"python.exe"})
    {
        const DWORD length = SearchPathW(nullptr, candidate, nullptr, 32768, filename, nullptr);
        if (length && length < 32768)
        {
            launcher = wxString(candidate) == "py.exe";
            return wxString(filename);
        }
    }
    return {};
}
bool ValidToken(const wxString& token)
{
    if (token.length() < 16 || token.length() > 128) return false;
    for (const auto character : token)
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '-')) return false;
    return true;
}
}

AiSession& AiSession::Get() { static AiSession session; return session; }
AiSession::~AiSession() { Stop(); }
wxString AiSession::Endpoint() const
{
    return m_endpoint.empty() ? wxString("http://127.0.0.1:8765/assistant/turn") : m_endpoint;
}

void AiSession::Stop()
{
    if (m_input) CloseHandle(static_cast<HANDLE>(m_input));
    m_input = nullptr;
    // 作业对象只包含本程序启动的服务，关闭后不会留下 Python 子进程。
    if (m_job) CloseHandle(static_cast<HANDLE>(m_job));
    m_job = nullptr;
    if (m_process)
    {
        if (WaitForSingleObject(static_cast<HANDLE>(m_process), 1500) == WAIT_TIMEOUT)
            TerminateProcess(static_cast<HANDLE>(m_process), 0);
        CloseHandle(static_cast<HANDLE>(m_process));
    }
    if (m_output) CloseHandle(static_cast<HANDLE>(m_output));
    m_process = m_output = nullptr;
    m_username.clear(); m_endpoint.clear(); m_token.clear();
    m_offline = false;
}

void AiSession::EnterOffline(const wxString& username)
{
    Stop();
    m_username = username.empty() ? wxString(wxS("访客")) : username;
    m_offline = true;
}

bool AiSession::Start(const wxString& username, const wxString& apiKey,
    const std::atomic_bool& cancelled, wxString& error)
{
    Stop();
    error.clear();
    bool validKey = !apiKey.empty() && apiKey.length() <= 1024;
    for (const auto character : apiKey)
        if (character < 33 || character > 126) validKey = false;
    if (!validKey) { error = wxS("请输入有效的 API Key，不要包含空格或换行。"); return false; }
    const wxString script = FindBroker();
    bool launcher = false;
    const wxString python = FindPython(launcher);
    if (script.empty()) { error = wxS("找不到 AI 助手服务文件，请保留程序旁的 assistant_server.py。"); return false; }
    if (python.empty()) { error = wxS("未找到 Python 3，请先安装 Python 并启用 py 或 python 命令。"); return false; }
    if (cancelled) return false;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE inputRead = nullptr, inputWrite = nullptr, outputRead = nullptr, outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &attributes, 0))
    { error = wxS("无法准备 AI 助手连接，请重试。"); return false; }
    if (!CreatePipe(&outputRead, &outputWrite, &attributes, 0))
    {
        CloseHandle(inputRead); CloseHandle(inputWrite);
        error = wxS("无法准备 AI 助手连接，请重试。"); return false;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullError = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = inputRead; startup.hStdOutput = outputWrite;
    startup.hStdError = nullError != INVALID_HANDLE_VALUE ? nullError : outputWrite;
    // 密钥仅写入匿名管道，命令行中只有解释器和本地脚本路径。
    const wxString command = "\"" + python + "\" " + (launcher ? "-3 " : "") +
        "-u \"" + script + "\" --session-stdin --port 0";
    std::wstring mutableCommand = command.ToStdWstring();
    PROCESS_INFORMATION process{};
    const wxString directory = wxFileName(script).GetPath();
    const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.wc_str(), &startup, &process);
    CloseHandle(inputRead); CloseHandle(outputWrite);
    if (nullError != INVALID_HANDLE_VALUE) CloseHandle(nullError);
    if (!created)
    {
        CloseHandle(inputWrite); CloseHandle(outputRead);
        error = wxS("无法启动 AI 助手服务，请检查 Python 3 安装。"); return false;
    }
    m_input = inputWrite; m_output = outputRead; m_process = process.hProcess;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
            AssignProcessToJobObject(job, process.hProcess)) m_job = job;
        else CloseHandle(job);
    }
    ResumeThread(process.hThread); CloseHandle(process.hThread);
    std::string secret = apiKey.ToStdString(wxConvUTF8) + "\n";
    DWORD written = 0;
    const bool sent = WriteFile(inputWrite, secret.data(), static_cast<DWORD>(secret.size()), &written, nullptr) && written == secret.size();
    SecureZeroMemory(secret.data(), secret.size());
    if (!sent) { Stop(); error = wxS("无法配置 AI 助手服务，请重试。"); return false; }
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!cancelled && std::chrono::steady_clock::now() < deadline)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available)
        {
            char buffer[1024]; DWORD count = 0;
            if (!ReadFile(outputRead, buffer, (std::min)(available, DWORD(sizeof(buffer))), &count, nullptr)) break;
            output.append(buffer, count);
            if (output.size() > 4096) break;
            const auto newline = output.find('\n');
            if (newline != std::string::npos)
            {
                wxStringTokenizer fields(wxString::FromUTF8(output.data(), newline));
                const wxString marker = fields.GetNextToken(), port = fields.GetNextToken(), token = fields.GetNextToken();
                unsigned long value = 0;
                if (marker == "WX_DEMO_ASSISTANT_READY" && port.ToULong(&value) && value > 0 && value <= 65535 && ValidToken(token))
                {
                    m_username = username;
                    m_endpoint = wxString::Format("http://127.0.0.1:%lu/assistant/turn", value);
                    m_token = token;
                    return true;
                }
                break;
            }
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(process.hProcess, &code) || code != STILL_ACTIVE) break;
        Sleep(25);
    }
    Stop();
    if (!cancelled) error = wxS("AI 助手服务未能就绪，请确认 Python 3 可以正常运行后重试。");
    return false;
}
