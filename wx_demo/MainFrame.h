#pragma once

#include <wx/wx.h>

class Canvas;

class MainFrame : public wxFrame
{
public:
    MainFrame();

private:
    bool SaveDocument(bool saveAs = false);
    bool ConfirmDiscardChanges();
    void UpdateDocumentTitle();
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);

    Canvas* m_canvas = nullptr;
    wxString m_filePath;
};
