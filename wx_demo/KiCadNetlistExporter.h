#pragma once

#include "CircuitDocument.h"
#include <string>
#include <wx/string.h>

class KiCadNetlistExporter
{
public:
    // 自动查找 KiCad CLI
    static wxString FindKiCadCli();

    // 使用指定的 KiCad CLI 导出网表
    static bool ExportFromSchematic(
        const std::string& schematicFile,
        const std::string& netlistFile,
        const wxString& cliPath,
        std::string& error
    );
};
