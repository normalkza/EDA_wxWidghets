#pragma once

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <functional>
#include "Component.h"
#include "Circuit.h"
#include <vector>
#include <utility>

class PropertyPanel : public wxPanel
{
public:
    explicit PropertyPanel(wxWindow* parent);
    void ShowComponent(const Component* component, int x, int y);
    void ShowTool(const wxString& type, int textPointSize = 14, int gateInputCount = 2);
    void ShowTextBox(const wxRect& rect, int pointSize);
    void ShowConnection(const WireConnection* connection);
    void SetTextFontSizeCallback(std::function<void(int)> callback)
    {
        m_textFontSizeCallback = std::move(callback);
    }
    void SetGateInputCountCallback(std::function<void(int)> callback)
    {
        m_gateInputCountCallback = std::move(callback);
    }
    void Clear();

private:
    friend struct CanvasRegressionAccess;
    wxFlexGridSizer* m_grid;
    wxStaticText* m_title;
    wxScrolledWindow* m_viewport;
    wxPanel* m_content;
    std::vector<wxString> m_rowNames;
    std::vector<wxString> m_rowValues;
    std::vector<wxStaticText*> m_valueLabels;
    wxPanel* m_textSettings;
    wxSpinCtrl* m_fontSize;
    std::function<void(int)> m_textFontSizeCallback;
    wxPanel* m_gateSettings;
    wxSpinCtrl* m_gateInputCount;
    bool m_updatingGateSettings = false;
    std::function<void(int)> m_gateInputCountCallback;
    void ShowGateSettings(int inputCount);
    void ShowTextSettings(int pointSize);
    void SetRows(const wxString& title, const std::vector<std::pair<wxString, wxString>>& rows);
};
