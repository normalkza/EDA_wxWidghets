#pragma once

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include "Component.h"
#include <vector>
#include <utility>

class PropertyPanel : public wxPanel
{
public:
    explicit PropertyPanel(wxWindow* parent);
    void ShowComponent(const Component* component, int x, int y);
    void ShowTool(const wxString& type);
    void Clear();

private:
    wxFlexGridSizer* m_grid;
    wxStaticText* m_title;
    wxScrolledWindow* m_content;
    std::vector<wxString> m_rowNames;
    std::vector<wxStaticText*> m_valueLabels;
    void SetRows(const wxString& title, const std::vector<std::pair<wxString, wxString>>& rows);
};
