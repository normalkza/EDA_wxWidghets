// wxWidgets 的部分 CRT 兼容封装会触发 MSVC C4996。
// 仅在包含相关头文件时屏蔽，不影响本文件后续代码。
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif

#include "KiCadNetlistExporter.h"

#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/utils.h>
#include <wx/tokenzr.h>
#include <wx/arrstr.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace
{
    bool IsExecutableFile(const wxString& path)
    {
        return !path.IsEmpty() && wxFileExists(path);
    }
}

wxString KiCadNetlistExporter::FindKiCadCli()
{
#ifdef __WXMSW__
    const wxString executable = wxT("kicad-cli.exe");
#else
    const wxString executable = wxT("kicad-cli");
#endif

    // 1. 优先检查用户配置的环境变量
    wxString configured;
    if (wxGetEnv(wxT("KICAD_CLI"), &configured))
    {
        if (IsExecutableFile(configured))
            return configured;
    }

    // 2. 检查系统 PATH
    wxString pathValue;
    if (wxGetEnv(wxT("PATH"), &pathValue))
    {
#ifdef __WXMSW__
        const wxChar separator = wxT(';');
#else
        const wxChar separator = wxT(':');
#endif

        wxStringTokenizer tokenizer(pathValue, wxString(separator));

        while (tokenizer.HasMoreTokens())
        {
            const wxString directory = tokenizer.GetNextToken();
            const wxString candidate =
                wxFileName(directory, executable).GetFullPath();

            if (IsExecutableFile(candidate))
                return candidate;
        }
    }

    // 3. 检查 Windows 常见安装目录
#ifdef __WXMSW__
    const wxString roots[] = {
        wxT("C:\\Program Files\\KiCad\\10.0\\bin"),
        wxT("C:\\Program Files\\KiCad\\9.0\\bin"),
        wxT("C:\\Program Files\\KiCad\\8.0\\bin"),
        wxT("C:\\Program Files (x86)\\KiCad\\10.0\\bin"),
        wxT("D:\\Program Files\\KiCad\\10.0\\bin")
    };

    for (const auto& root : roots)
    {
        const wxString candidate =
            wxFileName(root, executable).GetFullPath();

        if (IsExecutableFile(candidate))
            return candidate;
    }
#endif

    return wxEmptyString;
}

bool KiCadNetlistExporter::ExportFromSchematic(
    const std::string& schematicFile,
    const std::string& netlistFile,
    const wxString& cliPath,
    std::string& error)
{
    error.clear();

    const wxString schPath = wxString::FromUTF8(schematicFile);
    const wxString netPath = wxString::FromUTF8(netlistFile);

    // 检查原理图文件
    if (!wxFileExists(schPath))
    {
        error = "找不到待导出的 KiCad 原理图文件。";
        return false;
    }

    // 检查用户选择的 CLI
    if (!IsExecutableFile(cliPath))
    {
        error = "KiCad CLI 文件不存在，请重新选择 kicad-cli.exe。";
        return false;
    }

    // 将参数加上引号，避免路径中有空格导致执行失败
    auto QuoteArgument = [](const wxString& value)
        {
            wxString result = value;
            result.Replace(wxT("\""), wxT("\\\""));
            return wxT("\"") + result + wxT("\"");
        };

    // 组装 KiCad CLI 命令
    const wxString command =
        QuoteArgument(cliPath) +
        wxT(" sch export netlist -o ") +
        QuoteArgument(netPath) +
        wxT(" ") +
        QuoteArgument(schPath);

    // 同步执行 KiCad CLI
    const long exitCode = wxExecute(command, wxEXEC_SYNC);
    if (exitCode != 0)
    {
        error =
            "KiCad CLI 导出网表失败，退出代码：" +
            std::to_string(exitCode) +
            "。请确认原理图能在 KiCad 中正常打开，并检查 CLI 版本。";

        return false;
    }

    if (!wxFileExists(netPath))
    {
        error = "KiCad CLI 已结束，但没有生成网表文件。";
        return false;
    }

    return true;
}
