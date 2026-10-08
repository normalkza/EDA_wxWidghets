#pragma once

#include <wx/wx.h>
#include <functional>

wxToolBar* CreateMainToolBar(wxFrame* frame, std::function<void(const wxString&)> selectionCallback = {});
void UpdateMainToolBarSelection(wxToolBar* toolbar, const wxString& type);
