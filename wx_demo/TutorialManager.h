#pragma once

#include <wx/wx.h>
#include <vector>

#include "TutorialOverlay.h"
#include "CircuitDocument.h"

class MainFrame;

class TutorialManager
{
public:
    explicit TutorialManager(MainFrame* mainFrame);

    void StartTutorial(const wxString& tutorialName);
    void CancelTutorial();

    void NotifyToolSelected(const wxString& componentType);
    void NotifyComponentPlaced(const wxString& componentType, int componentId);
    void NotifyWireCreated(const CircuitWireData& wire);

private:
    struct TutorialStep
    {
        wxString type;
        wxString name;
        wxString message;
        bool placement = false;
        bool wire = false;
        wxString label;
    };

    // 一条预期连线，以及对应的中文教学名称
    struct ExpectedWire
    {
        CircuitWireData wire;

        wxString sourceName;
        wxString targetName;

        wxString sourcePinName;
        wxString targetPinName;
    };

    void BuildSteps();
    void ShowCurrentStep();

    void PreviousStep();
    void NextStep();

    void CompleteCurrentStep();
    void FinishTutorial();

    void BuildExpectedWires();
    // 显示当前应该完成的具体连线
    void ShowCurrentWireStep();

    wxRect GetCurrentTargetRect() const;

    std::vector<ExpectedWire> m_expectedWires;

    int m_wireStepIndex = 0;

    MainFrame* m_mainFrame = nullptr;
    TutorialOverlay* m_overlay = nullptr;

    wxString m_currentTutorialName;

    std::vector<TutorialStep> m_steps;

    int m_stepIndex = 0;
    int m_wireCount = 0;
    int m_wireTarget = 0;

    bool m_active = false;
    bool m_currentStepCompleted = false;
    bool m_waitingForWires = false;
};
