#include "VerilogRunner.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/regex.h>
#include <wx/msw/wrapwin.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
namespace
{
constexpr std::size_t MaxOutput = 256 * 1024;
std::wstring Quote(const wxString& value)
{
    std::wstring result = L"\""; unsigned slashes = 0;
    for (const auto ch : value.ToStdWstring())
    {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0; result += ch;
    }
    result.append(slashes * 2, L'\\'); result += L'"'; return result;
}
wxString Decode(const std::string& bytes)
{
    auto text = wxString::FromUTF8(bytes.data(), bytes.size());
    return bytes.empty() || !text.empty() ? text : wxString(bytes.data(), wxConvLocal, bytes.size());
}
struct ProcessResult { int exitCode = -1; bool cancelled = false, timedOut = false; wxString output; };
ProcessResult Execute(const std::vector<wxString>& arguments, const wxString& directory,
    const std::atomic_bool& cancelled, int timeout)
{
    ProcessResult result;
    if (cancelled) { result.cancelled = true; return result; }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &attributes, 0)) throw std::runtime_error("cannot create output pipe");
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    PROCESS_INFORMATION process{};
    std::string output;
    const auto close = [&] {
        if (job) CloseHandle(job);
        if (process.hThread) CloseHandle(process.hThread);
        if (process.hProcess) { WaitForSingleObject(process.hProcess, 1000); CloseHandle(process.hProcess); }
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        if (write) CloseHandle(write);
        if (read) CloseHandle(read);
    };
    try
    {
        if (!job || input == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot prepare compiler process");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            throw std::runtime_error("cannot configure process job");
        std::wstring command;
        for (const auto& argument : arguments) { if (!command.empty()) command += L' '; command += Quote(argument); }
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        startup.hStdInput = input; startup.hStdOutput = write; startup.hStdError = write;
        if (!CreateProcessW(arguments[0].wc_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.wc_str(), &startup, &process))
            throw std::runtime_error("cannot launch compiler or simulator");
        CloseHandle(write); write = nullptr;
        if (!AssignProcessToJobObject(job, process.hProcess))
        { TerminateProcess(process.hProcess, 1); throw std::runtime_error("cannot attach child process job"); }
        if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
        { TerminateProcess(process.hProcess, 1); throw std::runtime_error("cannot resume child process"); }
        CloseHandle(process.hThread); process.hThread = nullptr;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        bool exited = false, overflow = false;
        while (true)
        {
            DWORD available = 0;
            if (PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available)
            {
                char buffer[8192]; DWORD received = 0;
                if (ReadFile(read, buffer, (std::min)(available, DWORD(sizeof(buffer))), &received, nullptr))
                {
                    if (output.size() + received > MaxOutput) { overflow = true; break; }
                    output.append(buffer, received);
                }
            }
            else
            {
                // Process termination may race with its final pipe write. Once
                // signalled, peek again and drain before declaring output done.
                if (exited) break;
                exited = WaitForSingleObject(process.hProcess, 15) == WAIT_OBJECT_0;
            }
            if (cancelled) { result.cancelled = true; break; }
            if (std::chrono::steady_clock::now() >= deadline) { result.timedOut = true; break; }
        }
        if (result.cancelled || result.timedOut || overflow)
            TerminateJobObject(job, 1);
        DWORD status = 1;
        WaitForSingleObject(process.hProcess, 1000); GetExitCodeProcess(process.hProcess, &status);
        result.exitCode = static_cast<int>(status); result.output = Decode(output);
        if (overflow) { result.exitCode = -1; result.output += wxS("\n输出超过 256 KB，已停止进程。\n"); }
        close(); return result;
    }
    catch (...) { if (job) TerminateJobObject(job, 1); close(); throw; }
}
}

namespace VerilogRunner
{
TemporaryWorkspace::TemporaryWorkspace()
{
    wchar_t parent[32768], file[32768];
    const DWORD count = GetTempPathW(32768, parent);
    if (!count || count >= 32768 || !GetTempFileNameW(parent, L"wxv", 0, file)) throw std::runtime_error("cannot create temporary source folder");
    if (!DeleteFileW(file) || !CreateDirectoryW(file, nullptr)) throw std::runtime_error("cannot prepare temporary source folder");
    m_path = file;
}
TemporaryWorkspace::~TemporaryWorkspace()
{
    if (m_path.empty()) return;
    std::error_code error;
    const fs::path path(m_path.ToStdWstring()), parent = fs::temp_directory_path(error);
    if (error || path.filename().wstring().find(L"wxv") != 0) return;
    const auto resolved = fs::weakly_canonical(path, error);
    if (error || resolved != path || resolved.parent_path() != fs::weakly_canonical(parent, error) || error) return;
    fs::remove_all(resolved, error);
}
wxString TemporaryWorkspace::Path() const { return m_path; }

wxString FindTool(const wxString& name, const wxString& beside)
{
    wchar_t found[32768];
    if (!beside.empty())
    { wxFileName file(wxFileName(beside).GetPath(), name); if (file.FileExists()) return file.GetFullPath(); }
    const DWORD length = SearchPathW(nullptr, name.wc_str(), nullptr, 32768, found, nullptr);
    if (length && length < 32768) return found;
    const auto exe = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    for (const auto& folder : {exe + "\\tools\\iverilog\\bin", wxString("C:\\iverilog\\bin"), wxString("D:\\iverilog\\bin"), wxString("C:\\msys64\\mingw64\\bin")})
    { wxFileName file(folder, name); if (file.FileExists()) return file.GetFullPath(); }
    return {};
}
bool IsModuleName(const wxString& name)
{
    if (name.empty()) return true;
    wxRegEx pattern("^[A-Za-z_][A-Za-z0-9_$]{0,127}$"); return pattern.Matches(name);
}

Result Run(const Request& request, const std::atomic_bool& cancelled)
{
    Result result;
    try
    {
        if (!IsModuleName(request.top)) { result.output = wxS("顶层模块名称无效。"); return result; }
        if (request.sources.empty() || request.sources.size() > 32) { result.output = wxS("一次编译需要 1 至 32 个代码文件。"); return result; }
        if (!wxFileExists(request.compiler) || (request.simulate && !wxFileExists(request.runtime)))
        { result.output = wxS("未找到 Icarus Verilog 工具，请设置 iverilog.exe 和 vvp.exe 的路径。"); return result; }
        result.workspace = std::make_shared<TemporaryWorkspace>();
        const fs::path root(result.workspace->Path().ToStdWstring());
        std::map<wxString, fs::path> commonRoots;
        for (const auto& source : request.sources)
        {
            if (source.path.empty()) continue;
            const fs::path directory(wxFileName(source.path).GetPath().Lower().ToStdWstring());
            if (!directory.is_absolute()) throw std::runtime_error("source path must be absolute");
            const wxString volume(directory.root_name().wstring());
            const auto [entry, inserted] = commonRoots.emplace(volume, directory);
            if (!inserted)
                while (true)
                {
                    const auto relative = directory.lexically_relative(entry->second);
                    if (!relative.empty() && *relative.begin() != L"..") break;
                    const auto parent = entry->second.parent_path();
                    if (parent == entry->second) throw std::runtime_error("cannot resolve source folders");
                    entry->second = parent;
                }
        }
        std::map<wxString, unsigned> groups;
        std::map<wxString, unsigned> sourceIds;
        std::set<wxString> directories;
        std::vector<wxString> files;
        std::size_t total = 0;
        for (const auto& source : request.sources)
        {
            const auto bytes = source.code.ToUTF8(); total += bytes.length();
            if (total > VerilogWorkspace::MaxSourceBytes) throw std::runtime_error("source snapshots exceed 8 MB");
            const wxString parent = source.path.empty() ? wxString() : wxFileName(source.path).GetPath();
            fs::path folder = root / L"unsaved";
            if (!parent.empty())
            {
                const fs::path original(parent.Lower().ToStdWstring());
                const wxString volume(original.root_name().wstring());
                const auto [group, unused] = groups.emplace(volume, static_cast<unsigned>(groups.size()));
                folder = root / (L"unit" + std::to_wstring(group->second)) / original.lexically_relative(commonRoots.at(volume));
            }
            folder = folder.lexically_normal(); fs::create_directories(folder);
            const wxString filename = wxFileName(source.name).GetFullName();
            if (filename.empty() || filename != source.name || filename.StartsWith("-")) throw std::runtime_error("invalid snapshot filename");
            const fs::path target = folder / filename.ToStdWstring();
            const wxString full(target.wstring());
            if (!sourceIds.emplace(full, source.id).second) throw std::runtime_error("duplicate source filename");
            std::ofstream output(target, std::ios::binary); output.write(bytes.data(), bytes.length()); output.close();
            if (!output) throw std::runtime_error("cannot write source snapshot");
            directories.insert(wxString(folder.wstring())); if (!parent.empty()) directories.insert(parent);
            const auto ext = wxFileName(filename).GetExt().Lower();
            if (ext == "v" || ext == "sv") files.push_back(full);
        }
        if (files.empty()) { result.output = wxS("没有可编译的 .v 或 .sv 文件；.vh/.svh 仅作为包含文件。"); return result; }
        std::vector<wxString> arguments{request.compiler, "-g2012", "-grelative-include", "-Wall"};
        if (!request.top.empty()) { arguments.push_back("-s"); arguments.push_back(request.top); }
        for (const auto& directory : directories) { arguments.push_back("-I"); arguments.push_back(directory); }
        const wxString artifact((root / L"simulation.vvp").wstring());
        if (request.simulate) { arguments.push_back("-o"); arguments.push_back(artifact); }
        else { arguments.push_back("-t"); arguments.push_back("null"); }
        arguments.insert(arguments.end(), files.begin(), files.end());
        auto phase = Execute(arguments, result.workspace->Path(), cancelled, std::clamp(request.timeoutSeconds, 1, 120));
        result.output = wxS("[编译]\n") + phase.output;
        result.exitCode = phase.exitCode; result.cancelled = phase.cancelled; result.timedOut = phase.timedOut;
        // Match only filenames from this run's snapshots, never an arbitrary file emitted by a tool.
        wxStringTokenizer lines(phase.output, "\n", wxTOKEN_STRTOK);
        while (lines.HasMoreTokens() && result.diagnostics.size() < 500)
        {
            const auto line = lines.GetNextToken().Left(4096);
            for (const auto& [path, id] : sourceIds)
            {
                auto alternate = path; alternate.Replace("\\", "/");
                const auto prefix = line.StartsWith(path + ":") ? path + ":" :
                    line.StartsWith(alternate + ":") ? alternate + ":" : wxString();
                if (prefix.empty()) continue;
                const auto rest = line.Mid(prefix.length()); long number = 0;
                if (rest.BeforeFirst(':').ToLong(&number) && number > 0 && number <= 10000000)
                    result.diagnostics.push_back({id, static_cast<int>(number), rest.AfterFirst(':').Trim(false)});
            }
        }
        if (request.simulate && phase.exitCode == 0 && !phase.cancelled && !phase.timedOut)
        {
            phase = Execute({request.runtime, "-n", artifact}, result.workspace->Path(), cancelled, std::clamp(request.timeoutSeconds, 1, 120));
            result.output += wxS("\n[仿真]\n") + phase.output;
            result.exitCode = phase.exitCode; result.cancelled = phase.cancelled; result.timedOut = phase.timedOut;
            const auto wave = root / L"wave.vcd";
            if (fs::is_regular_file(wave) && fs::file_size(wave) <= 128 * 1024 * 1024) result.vcdPath = wxString(wave.wstring());
            if (result.vcdPath.empty())
                for (const auto& file : fs::directory_iterator(root))
                    if (!file.is_symlink() && file.is_regular_file() && file.path().extension() == L".vcd" && file.file_size() <= 128 * 1024 * 1024)
                    { result.vcdPath = wxString(file.path().wstring()); break; }
        }
        result.success = result.exitCode == 0 && !result.cancelled && !result.timedOut;
        if (result.cancelled) result.output += wxS("\n已取消，子进程已停止。\n");
        else if (result.timedOut) result.output += wxS("\n超过运行时限，已停止进程；请检查 testbench 是否有 $finish。\n");
        else result.output += wxString::Format(wxS("\n退出码：%d\n"), result.exitCode);
    }
    catch (const std::exception& error) { result.output += wxS("\n无法完成 Verilog 任务：") + wxString::FromUTF8(error.what()); }
    return result;
}
}
