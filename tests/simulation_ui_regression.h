#pragma once

#include "MainFrame.h"
#include "SimulationPanel.h"
#include "Canvas.h"
#include "CircuitFile.h"
#include "WaveformPanel.h"
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/splitter.h>
#include <memory>
#include <stdexcept>

struct SimulationUiRegressionAccess
{
    static void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(std::string("simulation UI: ") + message);
    }
    static void Run()
    {
        // Regression binaries live outside the application resource directory.
        // Missing optional toolbar images must not open a modal wxLogGui dialog.
        wxLogNull quiet;
        auto frame = std::make_unique<MainFrame>();
        auto& panel = *frame->m_simulation;
        auto& canvas = *frame->m_canvas;
        Require(panel.GetSize().y > panel.FromDIP(260), "initial split leaves a usable waveform region");
        frame->LoadTimingExample();
        Require(canvas.ExportDocument().simulation.stopTimeNs == 200 && panel.m_stopTime->GetValue() == "200",
            "example config reaches controls");
        Require(!canvas.IsModified(), "loaded example starts clean");
        panel.Step();
        Require(panel.m_active && !panel.m_running && panel.m_simulator.GetTime() == 0 &&
            panel.m_simulator.GetValue(2, PinType::Output, 0) == LogicValue::High,
            "next-event control consumes the explicit t0 edge");
        Require(panel.m_clockIds == std::vector<int>{1} && panel.m_clock->GetSelection() == 0,
            "period control binds an explicit clock");
        panel.Step();
        Require(panel.m_simulator.GetTime() == 10, "next-event control advances integer virtual time");
        panel.StepPeriod();
        for (int batch = 0; batch < 10 && panel.m_running; ++batch) panel.Pump();
        Require(panel.m_simulator.GetTime() == 30 &&
            panel.m_simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low,
            "period control uses selected clock");
        Require(!canvas.IsModified(), "simulation controls preserve the saved design");
        panel.Reset();
        Require(panel.m_simulator.GetTime() == 0 &&
            panel.m_simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low,
            "reset restores initial baseline before the t0 event");
        panel.Run();
        Require(panel.m_running && panel.m_pause->IsEnabled(), "run enables pause");
        panel.Pause();
        Require(!panel.m_running && !panel.m_timer.IsRunning(), "pause stops the UI scheduler");
        panel.Run();
        wxTimerEvent tick(panel.m_timer);
        panel.OnTimer(tick);
        Require(!panel.m_running && panel.m_simulator.IsFinished() && panel.m_simulator.GetTime() == 200,
            "timer budget executes events to the configured stop time");
        Require(panel.m_csv->IsEnabled() && panel.m_vcd->IsEnabled(), "completed traces are exportable");
        panel.Reset(); panel.Step();
        Require(panel.ToggleInput(6) && panel.m_simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low,
            "runtime reset stimulus applies asynchronously through the controller");
        Require(!canvas.ExportDocument().components.back().high && !canvas.IsModified(),
            "controller stimuli do not alter persistent initial levels");
        panel.Stop();
        Require(!panel.m_active && !panel.m_simulator.GetTraces().empty(), "stop preserves exportable history");
        auto config = canvas.ExportDocument().simulation;
        config.stopTimeNs = 400;
        wxString error;
        Require(canvas.SetSimulationConfig(config, error) && !panel.m_simulator.IsCompiled() &&
            panel.m_stopTime->GetValue() == "400", "design edits invalidate the controller and old traces");
        Require(canvas.Undo() && panel.m_stopTime->GetValue() == "200" &&
            canvas.ExportDocument().simulation.stopTimeNs == 200,
            "undo synchronizes restored configuration with the controller fields");
        panel.Step();
        Require(panel.m_simulator.GetConfig().stopTimeNs == 200 && !canvas.IsModified(),
            "stepping after undo cannot overwrite the restored configuration with a stale field");
        panel.Pause();
        const auto pausedTime = panel.m_simulator.GetTime();
        panel.OnTimer(tick);
        Require(panel.m_simulator.GetTime() == pausedTime, "queued timer ticks cannot advance a paused simulation");

        CircuitDocument multiple;
        multiple.formatVersion = 3;
        multiple.components = {{1, "CLOCK", 20, 20, 0, false}, {2, "CLOCK", 140, 20, 0, false}};
        multiple.components[0].clockPeriodNs = 2000; multiple.components[0].clockHighNs = 1000;
        multiple.components[1].clockPeriodNs = 2; multiple.components[1].clockHighNs = 1;
        multiple.simulation.stopTimeNs = 4000;
        Require(canvas.ImportDocument(multiple, error), "multi-clock UI budget fixture imports");
        panel.StepPeriod();
        Require(panel.m_running && panel.m_simulator.GetTime() < 2000 && panel.m_simulator.GetEventCount() <= 257,
            "a long reference period yields in bounded batches when another clock is faster");
        panel.Pause();
        Require(!panel.m_periodTarget && !panel.m_timer.IsRunning(), "period stepping remains pausable");

        const wxString directory = wxFileName(wxString::FromUTF8(__FILE__)).GetPath();
        for (const auto* name : {"dff_divider.xml", "dff_chain.xml"})
        {
            wxFileName path(directory + wxFILE_SEP_PATH + wxT("..") + wxFILE_SEP_PATH + wxT("examples") +
                wxFILE_SEP_PATH + wxString::FromUTF8(name));
            path.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
            CircuitDocument document;
            Require(CircuitFile::Load(path.GetFullPath(), document, error), "shipped example loads using the strict XML reader");
            DigitalSimulator simulator;
            Require(simulator.Compile(document, 1, error), "shipped example compiles");
            if (std::string(name) == "dff_chain.xml")
            {
                Require(simulator.AdvanceTo(5, error) && simulator.GetValue(4, PinType::Output, 0) == LogicValue::High &&
                    simulator.GetValue(5, PinType::Output, 0) == LogicValue::Low, "shipped chain advances one stage at 5 ns");
                Require(simulator.AdvanceTo(25, error) && simulator.GetValue(5, PinType::Output, 0) == LogicValue::High,
                    "shipped chain advances the second stage at 25 ns");
            }
            else
            {
                Require(simulator.AdvanceTo(0, error) && simulator.GetValue(2, PinType::Output, 0) == LogicValue::High,
                    "shipped divider consumes t0 clock edge");
                Require(simulator.AdvanceTo(20, error) && simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low,
                    "shipped divider flips again at 20 ns");
            }
            Require(simulator.AdvanceTo(document.simulation.stopTimeNs, error), "shipped example reaches its horizon");
        }
    }
};
