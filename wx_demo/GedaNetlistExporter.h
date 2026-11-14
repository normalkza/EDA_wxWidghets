
#pragma once

#include "CircuitDocument.h"
#include <string>

class GedaNetlistExporter
{
public:
    // 将一位全加器电路导出为 gEDA 网表文件
    static bool ExportFullAdder(
        const CircuitDocument& document,
        const std::string& filename,
        std::string& error
    );
};

