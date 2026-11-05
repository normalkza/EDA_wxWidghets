#pragma once

#include "Canvas.h"
#include "SelectionState.h"
#include <algorithm>
#include <stdexcept>

struct CanvasStructureRegressionAccess
{
    static void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(std::string("canvas structure: ") + message);
    }

    static void Run(Canvas& canvas)
    {
        Require(!canvas.IsModified(), "new canvas is initially clean");
        g_selectedType = "AND";
        wxMouseEvent place(wxEVT_LEFT_DOWN);
        place.SetPosition(wxPoint(100, 100));
        canvas.OnLeftDown(place);
        Require(canvas.IsModified() && canvas.Undo() && !canvas.IsModified(),
            "undo to the initial version-2 empty canvas restores the saved state");

        CircuitDocument document;
        document.components = {{10, "INPUT", 80, 80, 0, false}, {20, "OUTPUT", 360, 80, 1, false}};
        document.wires = {{10, 0, 20, 0}};
        wxString error;
        Require(canvas.ImportDocument(document, error), "import a version-1 design");
        Require(canvas.ExportDocument().formatVersion == 1, "import preserves the file version");
        auto snapshot = canvas.CreateSimulationSnapshot(error);
        Require(snapshot && snapshot->SetInput(10, LogicValue::High) && snapshot->Evaluate(),
            "independent simulation can change runtime stimulus");
        const auto result = snapshot->GetResult();
        Require(canvas.ApplySimulationResult(result), "apply current result by value");
        Require(canvas.m_components[1].comp->inputs[0].value == LogicValue::High,
            "result updates the rendered output value");
        Require(!canvas.ExportDocument().components[0].high && !canvas.IsModified() && !canvas.CanUndo(),
            "runtime results do not change saved initial values, dirty state or history");

        canvas.m_selectedIndex = 0;
        Require(canvas.CopySelection() && canvas.PasteSelection(), "copy an input displaying a runtime value");
        Require(!canvas.ExportDocument().components.back().high,
            "clipboard copies the design stimulus rather than the last runtime result");
        Require(!canvas.ApplySimulationResult(result), "editing invalidates earlier results");
        Require(canvas.Undo() && !canvas.IsModified(), "undo preserves the version and initial design");

        canvas.m_selectedIndex = 0;
        Require(canvas.ToggleSelectedInput(), "first stimulus edit");
        const auto firstRevision = canvas.GetDocumentRevision();
        Require(canvas.ToggleSelectedInput() && canvas.GetDocumentRevision() != firstRevision,
            "a second edit invalidates results even while the document is already dirty");
        auto newer = canvas.CreateSimulationSnapshot(error);
        Require(newer && newer->SetInput(10, LogicValue::High) && newer->Evaluate(), "compile next result");
        auto malformed = newer->GetResult();
        malformed.values.back().componentId = 999;
        Require(!canvas.ApplySimulationResult(malformed) &&
            canvas.m_components[0].comp->outputs[0].value == LogicValue::Low,
            "invalid result packets are rejected before any displayed pin changes");
        malformed = newer->GetResult();
        malformed.values.back() = malformed.values.front();
        Require(!canvas.ApplySimulationResult(malformed), "duplicate result pins are rejected");
        auto reordered = newer->GetResult();
        std::reverse(reordered.values.begin(), reordered.values.end());
        Require(canvas.ApplySimulationResult(reordered), "result matching uses stable IDs, not vector order");

        canvas.ClearDocument();
        Require(snapshot->SetInput(10, LogicValue::Low) && snapshot->Evaluate(),
            "snapshot remains usable after its source canvas was cleared");
        Require(!canvas.ApplySimulationResult(reordered), "clearing the canvas invalidates its old revision");
        document.formatVersion = 2;
        Require(canvas.ImportDocument(document, error) && canvas.ExportDocument().formatVersion == 2,
            "version-2 imports retain their format on export");
        Require(!canvas.ApplySimulationResult(result), "reusing IDs in a different document cannot accept old results");
        canvas.ClearDocument();
        g_selectedType.Clear();
    }
};
