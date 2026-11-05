#pragma once

#include "PcbDocument.h"
#include <cstddef>

class KiCadPcbImporter
{
public:
    static constexpr std::size_t MaxFileBytes = 64u * 1024u * 1024u;
    // Both operations are transactional: failure leaves document untouched.
    static bool Load(const wxString& path, PcbDocument& document, wxString& error);
    static bool Parse(const std::string& source, PcbDocument& document, wxString& error);
};
