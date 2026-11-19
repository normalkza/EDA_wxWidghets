#pragma once

#include "AiCircuitExecutor.h"
#include "Canvas.h"
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

struct CanvasAiRegressionAccess
{
    static std::vector<wxRect> Bounds(const Canvas& canvas)
    {
        std::vector<wxRect> bounds;
        for (const auto& component : canvas.m_components) bounds.push_back(canvas.GetComponentRect(component));
        return bounds;
    }

    static void StartWire(Canvas& canvas)
    {
        canvas.m_pendingWire = PinReference{canvas.m_components.front().comp, PinType::Output, 0};
    }

    static bool HasPendingWire(const Canvas& canvas) { return canvas.m_pendingWire.has_value(); }
    static void ClearPendingWire(Canvas& canvas) { canvas.m_pendingWire.reset(); }
};

namespace AiCircuitRegressionDetail
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

inline bool SameComponent(const CircuitComponentData& a, const CircuitComponentData& b)
{
    return a.id == b.id && a.type == b.type && a.x == b.x && a.y == b.y && a.rotation == b.rotation &&
        a.high == b.high && a.inputCount == b.inputCount && a.fallingEdge == b.fallingEdge &&
        a.clockPeriodNs == b.clockPeriodNs && a.clockHighNs == b.clockHighNs &&
        a.clockStartNs == b.clockStartNs && a.probeLabel == b.probeLabel;
}

inline bool SameWire(const CircuitWireData& a, const CircuitWireData& b)
{
    return a.sourceId == b.sourceId && a.sourcePin == b.sourcePin && a.targetId == b.targetId &&
        a.targetPin == b.targetPin && a.route == b.route;
}

inline void RequirePrefix(const CircuitDocument& expected, const CircuitDocument& document)
{
    Require(document.formatVersion == expected.formatVersion && document.simulation == expected.simulation &&
        document.components.size() >= expected.components.size() && document.wires.size() >= expected.wires.size() &&
        document.texts.size() == expected.texts.size(), "AI preserves document version, settings and existing objects");
    for (std::size_t i = 0; i < expected.components.size(); ++i)
        Require(SameComponent(document.components[i], expected.components[i]), "AI preserves existing components");
    for (std::size_t i = 0; i < expected.wires.size(); ++i)
        Require(SameWire(document.wires[i], expected.wires[i]), "AI preserves existing manual wire routes");
    for (std::size_t i = 0; i < expected.texts.size(); ++i)
        Require(document.texts[i].rect == expected.texts[i].rect && document.texts[i].pointSize == expected.texts[i].pointSize &&
            document.texts[i].text == expected.texts[i].text, "AI preserves existing annotations");
}

inline void RequireSame(const CircuitDocument& expected, const CircuitDocument& actual)
{
    RequirePrefix(expected, actual);
    Require(actual.components.size() == expected.components.size() && actual.wires.size() == expected.wires.size(),
        "AI document sizes remain unchanged");
}
}

inline void RunAiCircuitRegression(Canvas& canvas)
{
    using namespace AiCircuitRegressionDetail;
    // Deliberately list the gate first: the layout must follow dependencies,
    // not whichever order was used in the model's tool payload.
    const AiCircuitAction andAction{
        {{"gate", "AND", 2}, {"left", "INPUT", 0}, {"right", "INPUT", 0}, {"result", "OUTPUT", 1}},
        {{"left", "OUT", "gate", "A"}, {"right", "OUT", "gate", "B"}, {"gate", "OUT", "result", "IN"}}};

    wxString error;
    CircuitDocument empty, compact;
    Require(AiCircuitExecutor::BuildCandidate(empty, andAction, compact, error, wxSize(430, 220)),
        "AI can place a basic AND example inside a narrow canvas");
    Require(compact.components.size() == 4 && compact.wires.size() == 3, "AI creates exactly the proposed nodes and wires");
    Require(canvas.ImportDocument(compact, error), "AI compact example imports");
    const wxRect viewport(wxPoint(), wxSize(430, 220));
    for (const auto& bounds : CanvasAiRegressionAccess::Bounds(canvas))
        Require(viewport.Contains(bounds), "AI renders all generated component bounds within the requested viewport");

    CircuitDocument original;
    original.formatVersion = 2;
    original.components = {{2, "INPUT", 80, 80, 0, true}, {7, "OUTPUT", 340, 100, 1},
        {23, "NOR", 120, 240, 16, false, 90}};
    original.wires = {{2, 0, 7, 0, {{120, 100}, {220, 100}, {220, 120}, {340, 120}}}};
    original.texts = {{wxRect(40, 170, 360, 110), 13, wxS("已有标注")}};
    Require(canvas.ImportDocument(original, error), "AI regression fixture imports");
    const auto base = canvas.ExportDocument();
    RequireSame(original, base);
    const auto revision = canvas.GetDocumentRevision();
    CircuitDocument candidate;
    Require(AiCircuitExecutor::BuildCandidate(base, andAction, candidate, error, wxSize(1100, 800)),
        "AI produces a valid append-only candidate without changing the canvas");
    RequirePrefix(base, candidate);
    RequireSame(base, canvas.ExportDocument());
    Require(canvas.GetDocumentRevision() == revision && !canvas.IsModified() && !canvas.CanUndo(),
        "AI preview does not mark the canvas modified or create undo");
    const auto first = base.components.size();
    const auto& gate = candidate.components[first];
    const auto& left = candidate.components[first + 1];
    const auto& right = candidate.components[first + 2];
    const auto& result = candidate.components[first + 3];
    Require(left.x == right.x && left.y != right.y && left.x < gate.x && gate.x < result.x,
        "AI lays out inputs, gate and output in topological columns");
    Require(candidate.wires[base.wires.size()].targetPin == 0 &&
        candidate.wires[base.wires.size() + 1].targetPin == 1 &&
        candidate.wires[base.wires.size() + 2].route.empty(),
        "AI resolves named pins and leaves new wires in automatic routing");

    SimulationSnapshot simulation;
    Require(simulation.Compile(candidate, revision, error), "AI AND candidate can be compiled by the simulator");
    for (int a : {0, 1}) for (int b : {0, 1})
    {
        const auto toLogic = [](int v) { return v ? LogicValue::High : LogicValue::Low; };
        Require(simulation.SetInput(left.id, toLogic(a)) && simulation.SetInput(right.id, toLogic(b)) && simulation.Evaluate(),
            "AI generated inputs can be stimulated and evaluated");
        Require(simulation.GetValue(result.id, PinType::Input, 0) == toLogic(a && b), "AI circuit matches all four AND rows");
    }

    auto invalid = andAction;
    auto preserved = compact;
    const auto reject = [&](const AiCircuitAction& action) {
        Require(!AiCircuitExecutor::BuildCandidate(base, action, preserved, error) && !error.empty(),
            "invalid AI action is rejected with a diagnostic");
        RequireSame(compact, preserved);
        RequireSame(base, canvas.ExportDocument());
    };
    invalid.wires[0].to = "nonexistent"; reject(invalid);
    invalid = andAction; invalid.wires[0].outPin = "IN"; reject(invalid);
    invalid = andAction; invalid.wires[0].inPin = "a"; reject(invalid);
    invalid = andAction; invalid.wires[1].inPin = "A"; reject(invalid);
    invalid = andAction; invalid.wires.push_back(invalid.wires.front()); reject(invalid);
    invalid = andAction; invalid.components[1].ref = "gate"; reject(invalid);
    invalid = andAction; invalid.components[0].inputCount = 17; reject(invalid);
    invalid = andAction; invalid.components[0].type = "MUX"; reject(invalid);
    reject({{{"a", "NOT", 1}, {"b", "NOT", 1}}, {{"a", "OUT", "b", "IN"}, {"b", "OUT", "a", "IN"}}});
    reject({std::vector<AiComponentSpec>(33, {"a", "INPUT", 0}), {}});
    reject({andAction.components, std::vector<AiWireSpec>(65, andAction.wires.front())});
    Require(!AiCircuitExecutor::BuildCandidate(base, andAction, preserved, error, wxSize(140, 100)) && !error.empty(),
        "AI refuses a layout that cannot fit instead of generating invisible components");
    RequireSame(compact, preserved);

    CanvasAiRegressionAccess::StartWire(canvas);
    Require(!canvas.ApplyAssistantDocument(candidate, revision, error) && CanvasAiRegressionAccess::HasPendingWire(canvas),
        "AI rejects an active wire without discarding the in-progress interaction");
    CanvasAiRegressionAccess::ClearPendingWire(canvas);
    auto invalidDocument = candidate;
    invalidDocument.wires.back().targetPin = 99;
    Require(!canvas.ApplyAssistantDocument(invalidDocument, revision, error), "AI applies no partial state on invalid candidate");
    Require(!canvas.ApplyAssistantDocument(base, revision, error), "AI no-op creates no transaction");
    RequireSame(base, canvas.ExportDocument());
    Require(canvas.GetDocumentRevision() == revision && !canvas.IsModified() && !canvas.CanUndo(),
        "AI failed or no-op applies preserve revision, dirty state and undo history");

    Require(canvas.ApplyAssistantDocument(candidate, revision, error), "AI candidate commits to canvas");
    Require(canvas.IsModified() && canvas.CanUndo() && canvas.GetDocumentRevision() != revision,
        "AI commit marks the canvas dirty and advances its revision");
    RequireSame(candidate, canvas.ExportDocument());
    const auto bounds = CanvasAiRegressionAccess::Bounds(canvas);
    const wxRect oldWire = Wire::GetBounds(base.wires.front().route);
    for (std::size_t i = first; i < bounds.size(); ++i)
    {
        Require(!bounds[i].Intersects(base.texts[0].rect) && !bounds[i].Intersects(oldWire),
            "AI layout avoids existing notes and manual routes");
        for (std::size_t j = 0; j < i; ++j)
            Require(!bounds[i].Intersects(bounds[j]), "AI layout avoids both existing and new component overlap");
    }
    Require(!canvas.ApplyAssistantDocument(candidate, revision, error), "AI cannot replay a stale preview");
    RequireSame(candidate, canvas.ExportDocument());
    Require(canvas.Undo() && !canvas.IsModified() && !canvas.CanUndo(), "one Undo restores the entire AI edit and saved state");
    RequireSame(base, canvas.ExportDocument());

    auto editedSettings = base.simulation;
    ++editedSettings.stopTimeNs;
    Require(canvas.SetSimulationConfig(editedSettings, error), "ordinary edit establishes earlier undo history");
    const auto edited = canvas.ExportDocument();
    Require(!canvas.ApplyAssistantDocument(candidate, revision, error), "AI rejects a preview after an independent edit");
    RequireSame(edited, canvas.ExportDocument());
    Require(AiCircuitExecutor::BuildCandidate(edited, andAction, candidate, error, wxSize(1100, 800)) &&
        canvas.ApplyAssistantDocument(candidate, canvas.GetDocumentRevision(), error),
        "a fresh AI transaction commits on top of earlier undo history");
    Require(canvas.Undo() && canvas.IsModified() && canvas.CanUndo(), "AI undo preserves earlier edits and history");
    RequireSame(edited, canvas.ExportDocument());
    Require(canvas.Undo() && !canvas.IsModified() && !canvas.CanUndo(), "earlier undo remains available after AI undo");
    RequireSame(base, canvas.ExportDocument());
    canvas.ClearDocument();
}
