#pragma once

#include "CircuitDocument.h"
#include <string>

class KiCadNetlistExporter
{
public:
    // 直接根据当前电路文档生成 KiCad PCB Editor 可读取的 S-expression 网表。
    // 不调用 kicad-cli，也不弹出文件选择窗口。
    static bool ExportFullAdder(
        const CircuitDocument& document,
        const std::string& filename,
        std::string& error
    );
};
