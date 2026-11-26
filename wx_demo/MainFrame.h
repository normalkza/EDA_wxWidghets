#pragma once

#include <wx/wx.h>
#include <wx/weakref.h>
#include "TutorialManager.h"

class Canvas;
class Toolbox;
class PropertyPanel;
class wxSplitterWindow;
class wxPanel;
class SimulationPanel;
class wxFrame;
class AiAssistantDialog;
class PetWebView;
class VerilogFrame;

class MainFrame : public wxFrame
{
public:
    MainFrame();
    ~MainFrame() override;

    Toolbox* GetToolbox() const { return m_toolbox; }
    Canvas* GetCanvas() const { return m_canvas; }
    void StartTutorial();
    void ShowCompanion();
private:
    friend struct SimulationUiRegressionAccess;
    friend struct CanvasTutorialRegressionAccess;
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
    void OpenAiAssistant();
    PetWebView* EnsurePetPlugin();
    void ShowShortcuts();
    void OpenVerilog(bool fromCanvas = false);

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
    wxWeakRef<AiAssistantDialog> m_aiAssistant;
    bool m_aiClosePending = false;
    TutorialManager* m_tutorialManager = nullptr;
    wxWeakRef<PetWebView> m_petPlugin;
    wxString m_assistantPetState = "idle";
    wxWeakRef<VerilogFrame> m_verilog;
};
