#pragma once

#include "CircuitDocument.h"

// Design validation is independent of XML I/O and the canvas. Storage and
// simulation compilation both use this entry point and the same value checks.
class CircuitValidation
{
public:
    static bool Validate(const CircuitDocument& document, wxString& error);
    static bool ValidateRoute(const std::vector<wxPoint>& route, wxString& error);
    static bool IsCoordinate(int value);
    static bool IsRotation(int value);
    static bool IsValidXmlText(const wxString& text);
};
