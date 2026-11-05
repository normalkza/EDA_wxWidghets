#pragma once

#include <wx/wx.h>

class Canvas;
class Toolbox;
class PropertyPanel;
class wxSplitterWindow;
class wxPanel;
class SimulationPanel;
class wxFrame;

class MainFrame : public wxFrame
{
public:
    MainFrame();

private:
    friend struct SimulationUiRegressionAccess;
    bool SaveDocument(bool saveAs = false);
    bool ConfirmDiscardChanges();
    void UpdateDocumentTitle();
    void UpdateSidebarVisibility();
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnImportKiCadPcb(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void LoadTimingExample();
    void OpenSimulationWindow();
    void RestoreSimulationPanel();

    Canvas* m_canvas = nullptr;
    wxSplitterWindow* m_workspace = nullptr;
    wxSplitterWindow* m_sidebar = nullptr;
    wxPanel* m_editor = nullptr;
    SimulationPanel* m_simulation = nullptr;
    Toolbox* m_toolbox = nullptr;
    PropertyPanel* m_properties = nullptr;
    bool m_showToolbox = true;
    bool m_showProperties = true;
    int m_sidebarWidth = 0;
    int m_sidebarSash = 0;
    wxString m_filePath;
    wxFrame* m_simulationFrame = nullptr;
};
