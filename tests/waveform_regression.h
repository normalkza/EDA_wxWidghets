#pragma once

#include "Canvas.h"
#include "CircuitFile.h"
#include "CircuitValidation.h"
#include "DigitalSimulator.h"
#include "InputWaveformDialog.h"
#include <wx/filename.h>
#include <wx/frame.h>
#include <memory>
#include <stdexcept>
#include <vector>

inline void TestInputWaveform()
{
    const auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(std::string("input waveform: ") + message);
    };

    require(WaveformUi::EffectiveUnit(0, 500) == 1, "short horizons stay in ns");
    require(WaveformUi::EffectiveUnit(0, 2000) == 2, "microsecond horizons select us");
    require(WaveformUi::EffectiveUnit(0, 3000000) == 3, "millisecond horizons select ms");
    std::int64_t parsed = 0;
    require(WaveformUi::ParseTime(wxS("1.5"), 3, 5000000, parsed) && parsed == 1500000,
        "unit-aware time parsing");
    require(WaveformUi::ParseTime(wxS("99"), 3, 1000000, parsed) && parsed == 1000000,
        "parsed time clamps to the horizon");
    parsed = 0;
    require(WaveformUi::ParseTime(wxS("100 ns"), 1, 400, parsed) && parsed == 100,
        "default values carrying a unit suffix parse correctly");
    require(WaveformUi::ParseTime(wxS("1.5 ms"), 3, 5000000, parsed) && parsed == 1500000,
        "explicit unit suffix overrides the selected unit");
    require(!WaveformUi::ParseTime(wxS("5p ns"), 1, 400, parsed),
        "malformed time with a unit suffix is rejected");
    require(!WaveformUi::ParseTime(wxS("-1"), 1, 1000, parsed), "negative time is rejected");
    require(WaveformUi::FormatTime(1500000, 0, 5000000) == wxS("1.5 ms"), "unit-aware time display");

    std::vector<WaveformEdge> edges{{100, false}, {50, false}, {50, true}, {-1, false}, {900, true}};
    WaveformUi::Normalize(edges, 200);
    require(edges.size() == 2 && edges[0].timeNs == 50 && edges[0].high &&
        edges[1].timeNs == 100 && !edges[1].high, "edges sort, merge and clamp");

    const auto square = WaveformUi::MakeSquare(0, 100, 50, 250, false);
    require(square.size() == 6 && square[0].high && !square[1].high &&
        square[4].timeNs == 200 && square[5].timeNs == 250 && !square[5].high,
        "square generator alternates up to the end time");
    const auto pulse = WaveformUi::MakePulse(100, 50, true, false);
    require(pulse.size() == 2 && pulse[0].high && pulse[0].timeNs == 100 &&
        pulse[1].timeNs == 150 && !pulse[1].high, "pulse generator returns two edges");

    CircuitDocument document;
    document.formatVersion = 3;
    document.components.push_back({1, "INPUT", 20, 20, 0, false});
    document.components[0].waveform = {{0, false}, {100, true}, {200, false}};
    document.simulation.stopTimeNs = 400;
    wxString error;
    require(CircuitValidation::Validate(document, error), "custom waveform document validates");

    wxString path = wxFileName::CreateTempFileName(wxS("waveform_regression_"));
    require(!path.empty(), "temporary file is available");
    struct Cleanup
    {
        wxString path;
        ~Cleanup() { if (!path.empty()) wxRemoveFile(path); }
    } cleanup{path};
    require(CircuitFile::Save(path, document, error), "custom waveform saves in v3");
    CircuitDocument loaded;
    if (!CircuitFile::Load(path, loaded, error))
        throw std::runtime_error(std::string("input waveform: load failed: ") + error.ToStdString());
    require(loaded.components[0].waveform == document.components[0].waveform,
        "waveform survives save and load");

    CircuitDocument invalid = document;
    invalid.components[0].waveform = {{200, true}, {100, false}};
    require(!CircuitValidation::Validate(invalid, error), "out-of-order waveform is rejected");
    invalid = document;
    invalid.components[0].type = "OUTPUT";
    invalid.components[0].inputCount = 1;
    require(!CircuitValidation::Validate(invalid, error), "non-input source cannot carry a waveform");
    invalid = document;
    invalid.components[0].waveform = {{500, true}};
    require(!CircuitValidation::Validate(invalid, error), "waveform beyond stop time is rejected");

    DigitalSimulator simulator;
    require(simulator.Compile(document, 1, error), "waveform document compiles");
    require(simulator.Step(error) && simulator.GetValue(1, PinType::Output, 0) == LogicValue::Low,
        "t0 waveform edge is consumed by the first step");
    require(simulator.Step(error) && simulator.GetTime() == 100 &&
        simulator.GetValue(1, PinType::Output, 0) == LogicValue::High, "scheduled rising edge fires");
    require(simulator.Step(error) && simulator.GetValue(1, PinType::Output, 0) == LogicValue::Low,
        "scheduled falling edge fires");
    require(simulator.Step(error) && simulator.IsFinished(), "simulation reaches the horizon");
    require(simulator.Reset(error) && simulator.GetTime() == 0 &&
        simulator.GetValue(1, PinType::Output, 0) == LogicValue::Low, "reset replays the same waveform");

    // 自定义波形可以直接驱动 D 触发器的 CLK 与 D：CLK 由 INPUT 的定时激励
    // 产生边沿，DFF 只在上升沿采样，Q 与波形严格同步。
    CircuitDocument sequential;
    sequential.formatVersion = 3;
    sequential.simulation.stopTimeNs = 300;
    sequential.components.push_back({1, "INPUT", 20, 20, 0, false});   // CLK
    sequential.components.push_back({2, "INPUT", 20, 80, 0, false});   // D
    sequential.components.push_back({3, "DFF", 160, 50, 2, false});
    sequential.components.push_back({4, "OUTPUT", 280, 50, 1, false});
    sequential.components[0].waveform = WaveformUi::MakeSquare(0, 100, 50, 300, false);
    sequential.components[1].waveform = {{0, false}, {75, true}, {175, false}};
    sequential.wires.push_back({1, 0, 3, 1}); // INPUT(CLK) -> DFF.CLK
    sequential.wires.push_back({2, 0, 3, 0}); // INPUT(D)   -> DFF.D
    sequential.wires.push_back({3, 0, 4, 0}); // DFF.Q      -> OUTPUT
    require(CircuitValidation::Validate(sequential, error), "waveform-driven DFF document validates");

    DigitalSimulator sequentialSim;
    require(sequentialSim.Compile(sequential, 2, error), "waveform-driven DFF compiles");
    const auto q = [&sequentialSim] { return sequentialSim.GetValue(3, PinType::Output, 0); };
    require(sequentialSim.AdvanceTo(75, error) && q() == LogicValue::Low,
        "DFF holds low before the first rising edge");
    require(sequentialSim.AdvanceTo(100, error) && q() == LogicValue::High,
        "waveform rising edge samples D high");
    require(sequentialSim.AdvanceTo(175, error) && q() == LogicValue::High,
        "DFF holds when D falls between edges");
    require(sequentialSim.AdvanceTo(200, error) && q() == LogicValue::Low,
        "next waveform rising edge samples D low");
    require(sequentialSim.Reset(error) && sequentialSim.AdvanceTo(100, error) && q() == LogicValue::High,
        "reset replays the waveform-driven DFF sequence");

    auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, wxS("Input waveform regression"));
    auto* canvas = new Canvas(frame.get());
    require(canvas->ImportDocument(document, error), "waveform document imports into the canvas");
    auto edited = canvas->ExportDocument();
    edited.components[0].waveform = {{50, true}, {150, false}};
    edited.simulation.stopTimeNs = 300;
    const auto revision = canvas->GetDocumentRevision();
    require(canvas->ApplyWaveformDocument(edited, error) && canvas->GetDocumentRevision() > revision,
        "applying waveforms is one design edit");
    require(canvas->ExportDocument().components[0].waveform.size() == 2 &&
        canvas->ExportDocument().simulation.stopTimeNs == 300, "editor changes reach the document");
    require(canvas->Undo() && canvas->ExportDocument().components[0].waveform ==
        document.components[0].waveform && canvas->ExportDocument().simulation.stopTimeNs == 400,
        "waveform edits are undoable");

    InputWaveformDialog dialog(frame.get(), canvas);
    require(dialog.GetSignals().size() == 1 && dialog.GetSignals()[0].id == 1,
        "editor lists available input sources");
    require(dialog.GetDurationNs() == 400, "editor picks up the simulation horizon");
}
