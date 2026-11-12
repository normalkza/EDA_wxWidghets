#pragma once

#include <wx/wx.h>
#include "DigitalSimulator.h"
#include <wx/timer.h>
#include <functional>

class Canvas;
class WaveformPanel;
class wxChoice;

// UI-thread controller: each timer callback has a wall-time budget, but only
// the solver's integer event times advance virtual time.
class SimulationPanel : public wxPanel
{
public:
    SimulationPanel(wxWindow* parent, Canvas* canvas);
    void InvalidateDocument();
    void Run();
    void Pause();
    void Stop();
    void Reset();
    void Step();
    void StepPeriod();
    bool ToggleInput(int componentId);
    void SetExampleCallback(std::function<void()> callback) { m_exampleCallback = std::move(callback); }

private:
    friend struct SimulationUiRegressionAccess;
    bool Prepare();
    bool ApplyStopTime();
    void RefreshResult();
    void UpdateControls();
    void RefreshClockChoices();
    void ShowError(const wxString& error);
    void Export(bool vcd);
    void Configure();
    void OnTimer(wxTimerEvent& event);
    void Pump();

    Canvas* m_canvas;
    DigitalSimulator m_simulator;
    wxTimer m_timer;
    WaveformPanel* m_waveform;
    wxStaticText* m_state;
    wxTextCtrl* m_stopTime;
    wxChoice* m_clock;
    wxButton *m_run, *m_pause, *m_stop, *m_reset, *m_step, *m_period, *m_csv, *m_vcd;
    std::vector<int> m_clockIds;
    std::function<void()> m_exampleCallback;
    wxString m_warnings;
    bool m_active = false;
    bool m_running = false;
    std::optional<std::int64_t> m_periodTarget;
};
