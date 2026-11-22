#pragma once

#include <wx/wx.h>
#include <wx/weakref.h>
#include <wx/timer.h>
#include <cstdint>
#include <memory>
#include <vector>

#include "TutorialOverlay.h"
#include "CircuitDocument.h"

class MainFrame;

class TutorialManager : public wxEvtHandler
{
public:
    explicit TutorialManager(MainFrame* mainFrame);
    ~TutorialManager();
    TutorialManager(const TutorialManager&) = delete;
    TutorialManager& operator=(const TutorialManager&) = delete;

    void StartTutorial(const wxString& tutorialName);
    void StartAutoTutorial(const wxString& tutorialName);
    void CancelTutorial();

    void NotifyToolSelected(const wxString& componentType);
    void NotifyComponentPlaced(const wxString& componentType, int componentId);
    void NotifyWireCreated(const CircuitWireData& wire);

private:
    friend struct CanvasTutorialRegressionAccess;
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
    void AdvanceAutoDemo();
    wxPoint AutoPlacementPosition(std::size_t index) const;
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
    wxWeakRef<TutorialOverlay> m_overlay;
    std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
    std::uint64_t m_generation = 0;

    wxString m_currentTutorialName;

    std::vector<TutorialStep> m_steps;
    std::vector<TutorialStep> m_autoPlacements;
    std::vector<std::pair<int, wxString>> m_autoLabels;
    CircuitDocument m_autoDocument;
    wxTimer m_autoTimer;
    std::uint64_t m_autoFrameRevision = 0;
    std::size_t m_autoPlacementIndex = 0;
    std::size_t m_autoWireIndex = 0;
    bool m_autoDemo = false;
    bool m_autoPaused = false;

    int m_stepIndex = 0;
    int m_wireCount = 0;
    int m_wireTarget = 0;

    bool m_active = false;
    bool m_currentStepCompleted = false;
    bool m_waitingForWires = false;
};
