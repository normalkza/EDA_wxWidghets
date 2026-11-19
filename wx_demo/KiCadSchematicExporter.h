
#pragma once

#include "CircuitDocument.h"
#include <string>
#include <wx/window.h>

class KiCadSchematicExporter
{
public:
    static bool ExportSchematic(
        const CircuitDocument& document,
        const std::string& filename,
        const std::string& symbolLibraryPath,
        std::string& error,
        wxWindow* parent = nullptr
    );
};
