#pragma once

#include "Canvas.h"
#include "DigitalSimulator.h"
#include "SelectionState.h"
#include <algorithm>
#include <stdexcept>
#include <string>

// Run on a normal Canvas constructed with the test suite's MemoryClipboard.
// The suite deliberately exercises the editor's public import/export, history
// and runtime boundaries rather than the modal parameter dialog.
struct CanvasTimingRegressionAccess
{
    static void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(std::string("canvas timing: ") + message);
    }

    static CircuitDocument Chain()
    {
        CircuitDocument document;
        document.formatVersion = 3;
        document.simulation = {400, 2345, 24, 6789};
        document.components = {
            {1, "INPUT", 60, 80, 0, true},
            {2, "CLOCK", 60, 220, 0, true},
            {3, "DFF", 280, 80, 3, true},
            {4, "DFF", 480, 80, 3, false},
            {5, "PROBE", 660, 80, 1, false},
            {6, "INPUT", 60, 360, 0, false}
        };
        document.components[1].clockPeriodNs = 40;
        document.components[1].clockHighNs = 11;
        document.components[1].clockStartNs = 7;
        document.components[4].probeLabel = "末级 Q / \"输出\"";
        document.wires = {{1, 0, 3, 0}, {2, 0, 3, 1}, {2, 0, 4, 1},
            {3, 0, 4, 0}, {6, 0, 3, 2}, {6, 0, 4, 2}, {4, 0, 5, 0}};
        return document;
    }

    static void CheckParameters(const CircuitComponentData& actual, const CircuitComponentData& expected)
    {
        Require(actual.type == expected.type && actual.inputCount == expected.inputCount &&
            actual.high == expected.high && actual.rotation == expected.rotation &&
            actual.clockPeriodNs == expected.clockPeriodNs && actual.clockHighNs == expected.clockHighNs &&
            actual.clockStartNs == expected.clockStartNs && actual.probeLabel == expected.probeLabel,
            "component design parameters survive the view/runtime boundary");
    }

    static void TestRoundTripAndResults(Canvas& canvas)
    {
        const auto document = Chain();
        wxString error;
        Require(canvas.ImportDocument(document, error), "version-3 chain imports");
        const auto exported = canvas.ExportDocument();
        Require(exported.formatVersion == 3 && exported.simulation == document.simulation &&
            exported.components.size() == document.components.size() &&
            exported.wires.size() == document.wires.size(), "version-3 configuration and connections roundtrip");
        for (std::size_t index = 0; index < document.components.size(); ++index)
        {
            CheckParameters(exported.components[index], document.components[index]);
            Require(exported.components[index].id == document.components[index].id &&
                exported.components[index].x == document.components[index].x &&
                exported.components[index].y == document.components[index].y,
                "stable identity and placement roundtrip");
        }
        Require(canvas.m_components[2].comp->outputs[0].value == LogicValue::High &&
            canvas.m_components[3].comp->outputs[0].value == LogicValue::Low &&
            canvas.m_components[4].comp->inputs[0].value == LogicValue::Low,
            "import displays settled DFF initial states without treating initial CLK=1 as an edge");
        Require(!canvas.IsModified() && !canvas.CanUndo(), "imported baseline is clean");

        int changed = 0;
        canvas.SetDocumentChangedCallback([&changed] { ++changed; });
        const auto revision = canvas.GetDocumentRevision();
        DigitalSimulator simulator;
        Require(simulator.Compile(document, revision, error), "compile the displayed design");
        Require(simulator.QueueInput(1, LogicValue::Low, 1, error) && simulator.AdvanceTo(7, error),
            "advance to the first falling clock edge");
        Require(canvas.ApplySimulationResult(simulator.GetResult()) &&
            canvas.m_components[1].comp->outputs[0].value == LogicValue::Low &&
            canvas.ExportDocument().components[1].high,
            "clock runtime display does not overwrite its saved initial level");
        Require(simulator.AdvanceTo(36, error) && canvas.ApplySimulationResult(simulator.GetResult()),
            "apply a complete clocked chain result");
        Require(canvas.m_components[2].comp->outputs[0].value == LogicValue::Low &&
            canvas.m_components[3].comp->outputs[0].value == LogicValue::High &&
            canvas.m_components[4].comp->inputs[0].value == LogicValue::High,
            "runtime result renders simultaneous DFF commits and the connected probe");
        const auto after = canvas.ExportDocument();
        for (std::size_t index = 0; index < document.components.size(); ++index)
            CheckParameters(after.components[index], document.components[index]);
        Require(after.simulation == document.simulation && canvas.GetDocumentRevision() == revision &&
            changed == 0 && !canvas.IsModified() && !canvas.CanUndo(),
            "simulation changes neither design/configuration, revision, dirty state, callbacks nor undo history");
        canvas.SetDocumentChangedCallback({});
    }

    static void TestGeometry(Canvas& canvas)
    {
        for (const auto* type : {"CLOCK", "DFF", "PROBE"})
        {
            CircuitDocument document;
            document.formatVersion = 3;
            const int inputs = std::string(type) == "DFF" ? 3 : std::string(type) == "PROBE" ? 1 : 0;
            document.components = {{71, type, 280, 160, inputs, false}};
            wxString error;
            Require(canvas.ImportDocument(document, error), "timing geometry fixture imports");
            canvas.m_selectedIndex = 0;
            const wxPoint center = wxPoint(280, 160) + canvas.GetComponentCenter(canvas.m_components[0]);
            for (int angle : {0, 90, 180, 270})
            {
                const auto& placed = canvas.m_components[0];
                Require(placed.rotation == angle && canvas.HitTest(center.x, center.y) == 0,
                    "timing symbol center is selectable at every rotation");
                Require(wxPoint(placed.x, placed.y) + canvas.GetComponentCenter(placed) == center,
                    "rotation keeps the symbol center fixed");
                wxBitmap bitmap(800, 700);
                {
                    wxMemoryDC dc(bitmap);
                    dc.SetBackground(*wxWHITE_BRUSH);
                    dc.Clear();
                    canvas.DrawPlacedComponent(dc, placed);
                }
                const auto image = bitmap.ConvertToImage();
                const auto bounds = canvas.GetComponentRect(placed);
                for (PinType direction : {PinType::Input, PinType::Output})
                {
                    const auto& pins = direction == PinType::Input ? placed.comp->inputs : placed.comp->outputs;
                    for (std::size_t pinIndex = 0; pinIndex < pins.size(); ++pinIndex)
                    {
                        const PinReference pin{placed.comp, direction, pinIndex};
                        const auto position = canvas.GetPinPosition(pin);
                        Require(canvas.HitTestPin(position) == pin && bounds.Contains(position),
                            "every rotated timing pin agrees with hit testing and bounds");
                        const auto local = canvas.WorldToComponent(placed, position);
                        Require(canvas.ComponentToWorld(placed, local) == position,
                            "pin transforms are invertible");
                        const int r = image.GetRed(position.x, position.y);
                        const int g = image.GetGreen(position.x, position.y);
                        const int b = image.GetBlue(position.x, position.y);
                        Require(direction == PinType::Input ? b > 150 && b > r + 60 && b > g + 40 :
                            r > 160 && r > g + 60 && r > b + 60,
                            "rendered timing terminals match their rotated electrical endpoints");
                    }
                }
                Require(canvas.RotateSelection(90), "timing symbol quarter-turn succeeds");
            }
            Require(canvas.GetSelectedRotation() == 0 &&
                canvas.ExportDocument().components[0].rotation == 0,
                "four rotations restore and persist the original timing orientation");
        }
    }

    static void TestFeedback(Canvas& canvas)
    {
        CircuitDocument document;
        document.formatVersion = 3;
        document.components = {{1, "CLOCK", 60, 220, 0, false},
            {2, "DFF", 300, 100, 3, false}, {3, "NOT", 480, 100, 1, false},
            {4, "PROBE", 660, 100, 1, false}};
        document.wires = {{1, 0, 2, 1}, {2, 0, 3, 0}, {3, 0, 2, 0}, {2, 0, 4, 0}};
        wxString error;
        Require(canvas.ImportDocument(document, error) && CircuitFile::Validate(canvas.ExportDocument(), error),
            "DFF Q -> NOT -> D feedback imports and remains persistable");
        Require(canvas.m_components[1].comp->outputs[0].value == LogicValue::Low &&
            canvas.m_components[2].comp->outputs[0].value == LogicValue::High,
            "feedback baseline settles the inverter without taking a clock edge");
        DigitalSimulator simulator;
        Require(simulator.Compile(document, canvas.GetDocumentRevision(), error) &&
            simulator.Step(error) && canvas.ApplySimulationResult(simulator.GetResult()),
            "feedback divider accepts its first explicit t=0 edge");
        Require(canvas.m_components[1].comp->outputs[0].value == LogicValue::High &&
            canvas.m_components[3].comp->inputs[0].value == LogicValue::High &&
            !canvas.ExportDocument().components[1].high && !canvas.IsModified(),
            "divider result displays a toggled Q while retaining its saved initial Q");
    }

    static void TestClipboard(Canvas& canvas)
    {
        const auto document = Chain();
        for (int selected : {1, 2, 3, 4})
        {
            wxString error;
            Require(canvas.ImportDocument(document, error), "timing clipboard fixture imports");
            DigitalSimulator simulator;
            Require(simulator.Compile(document, canvas.GetDocumentRevision(), error) &&
                simulator.QueueInput(1, LogicValue::Low, 1, error) &&
                simulator.AdvanceTo(selected == 1 ? 7 : 36, error) &&
                canvas.ApplySimulationResult(simulator.GetResult()), "prepare changed runtime display for copying");
            canvas.m_selectedIndex = selected;
            Require(canvas.CopySelection() && canvas.PasteSelection(), "timing object copy/paste uses the memory clipboard");
            const auto exported = canvas.ExportDocument();
            Require(exported.components.size() == document.components.size() + 1,
                "paste adds exactly one timing object");
            CheckParameters(exported.components.back(), document.components[selected]);
            Require(std::none_of(document.components.begin(), document.components.end(),
                [&](const auto& existing) { return existing.id == exported.components.back().id; }),
                "pasted timing object receives a fresh persistent ID");
            Require(canvas.Undo() && !canvas.IsModified() && !canvas.CanUndo() &&
                canvas.ExportDocument().components.size() == document.components.size(),
                "one undo removes the pasted object and restores the saved design");
        }
    }

    static void TestInvalidationAndConfig(Canvas& canvas)
    {
        CircuitDocument document;
        document.formatVersion = 2;
        document.components = {{11, "INPUT", 100, 100, 0, false}};
        wxString error;
        Require(canvas.ImportDocument(document, error), "configuration history fixture imports");
        int changed = 0;
        canvas.SetDocumentChangedCallback([&changed] { ++changed; });
        const auto beforeRevision = canvas.GetDocumentRevision();
        canvas.m_selectedIndex = 0;
        Require(canvas.ToggleSelectedInput() && canvas.ToggleSelectedInput() && changed == 2 &&
            canvas.GetDocumentRevision() == beforeRevision + 2,
            "every design edit notifies result invalidation while the document is already dirty");
        Require(canvas.ImportDocument(document, error), "restore clean configuration fixture");
        changed = 0;
        auto config = document.simulation;
        config.stopTimeNs = 321;
        config.maxEvents = 777;
        Require(canvas.SetSimulationConfig(config, error) && canvas.IsModified() && canvas.CanUndo() && changed == 1 &&
            canvas.ExportDocument().formatVersion == 3 && canvas.ExportDocument().simulation == config,
            "simulation configuration is a persistent, invalidating document edit");
        const auto revision = canvas.GetDocumentRevision();
        Require(canvas.SetSimulationConfig(config, error) && canvas.GetDocumentRevision() == revision && changed == 1,
            "submitting identical configuration creates no edit");
        auto invalid = config;
        invalid.maxEvents = 0;
        Require(!canvas.SetSimulationConfig(invalid, error) && !error.empty() &&
            canvas.GetDocumentRevision() == revision && canvas.ExportDocument().simulation == config && changed == 1,
            "invalid configuration changes neither the design nor history/revision");
        Require(canvas.Undo() && !canvas.IsModified() && !canvas.CanUndo() &&
            canvas.ExportDocument().formatVersion == 2 && canvas.ExportDocument().simulation == document.simulation,
            "configuration undo restores the previous file version, values and saved state");
        canvas.SetDocumentChangedCallback({});
    }

    static void TestRuntimeInput(Canvas& canvas)
    {
        CircuitDocument document;
        document.formatVersion = 3;
        document.components = {{8, "INPUT", 100, 100, 0, false},
            {9, "BUTTON", 300, 100, 0, true}};
        wxString error;
        Require(canvas.ImportDocument(document, error), "runtime input fixture imports");
        int changes = 0;
        int stimuli = 0;
        canvas.SetDocumentChangedCallback([&changes] { ++changes; });
        canvas.SetRuntimeInputCallback([&](int id) {
            ++stimuli;
            for (auto& placed : canvas.m_components)
                if (placed.id == id)
                {
                    auto& value = placed.comp->outputs[0].value;
                    value = value == LogicValue::High ? LogicValue::Low : LogicValue::High;
                    return true;
                }
            return false;
        });
        const auto revision = canvas.GetDocumentRevision();
        g_selectedType.Clear();
        for (int index : {0, 1})
        {
            canvas.m_selectedIndex = index;
            Require(canvas.ToggleSelectedInput(), "F6/menu input path dispatches runtime stimulus");
            canvas.m_pressedIndex = index;
            canvas.m_togglePending = true;
            const wxPoint local = index == 0 ? wxPoint(20, 20) : wxPoint(60, 60);
            canvas.m_pressPosition = canvas.ComponentToWorld(canvas.m_components[index], local);
            wxMouseEvent release(wxEVT_LEFT_UP);
            release.SetPosition(canvas.m_pressPosition);
            canvas.OnLeftUp(release);
        }
        Require(stimuli == 4 && changes == 0 && canvas.GetDocumentRevision() == revision &&
            !canvas.IsModified() && !canvas.CanUndo(),
            "runtime INPUT/BUTTON clicks and F6 dispatch without creating edits or undo entries");
        const auto exported = canvas.ExportDocument();
        CheckParameters(exported.components[0], document.components[0]);
        CheckParameters(exported.components[1], document.components[1]);
        canvas.SetRuntimeInputCallback({});
        canvas.m_selectedIndex = 0;
        Require(canvas.ToggleSelectedInput() && canvas.IsModified() && canvas.CanUndo() && changes == 1,
            "without an active runtime consumer an input toggle remains an undoable design edit");
        canvas.SetDocumentChangedCallback({});
    }

    static void Run(Canvas& canvas)
    {
        struct Cleanup
        {
            Canvas& canvas;
            std::function<void()> changed;
            std::function<bool(int)> runtime;
            ~Cleanup()
            {
                canvas.SetDocumentChangedCallback({});
                canvas.SetRuntimeInputCallback({});
                canvas.ClearDocument();
                canvas.SetDocumentChangedCallback(std::move(changed));
                canvas.SetRuntimeInputCallback(std::move(runtime));
                g_selectedType.Clear();
            }
        } cleanup{canvas, std::move(canvas.m_documentChangedCallback), std::move(canvas.m_runtimeInputCallback)};
        TestRoundTripAndResults(canvas);
        TestGeometry(canvas);
        TestFeedback(canvas);
        TestClipboard(canvas);
        TestInvalidationAndConfig(canvas);
        TestRuntimeInput(canvas);
    }
};
