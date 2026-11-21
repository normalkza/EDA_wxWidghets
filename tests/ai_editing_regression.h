#pragma once

#include "AiCircuitExecutor.h"
#include "Canvas.h"
#include <wx/base64.h>
#include <wx/mstream.h>
#include <wx/xml/xml.h>
#include <cstring>
#include <stdexcept>

inline void RunAiEditingRegression(Canvas& canvas)
{
    const auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    wxInitAllImageHandlers();
    canvas.SetSize(1000, 700);
    CircuitDocument base; base.formatVersion = 3;
    base.components = {{2, "INPUT", 60, 100, 0, true}, {7, "OUTPUT", 660, 100, 1}};
    base.wires = {{2, 0, 7, 0, {}}};
    base.texts = {{wxRect(20, 20, 100, 30), 14, wxS("原有标注")}};
    wxString error;
    require(canvas.ImportDocument(base, error), "skill fixture import");
    AiWorkspaceContext context;
    require(canvas.CaptureAssistantContext(context, error), "capture actual canvas data and image");
    const auto bytes = context.sceneXml.ToUTF8(); wxMemoryInputStream input(bytes.data(), bytes.length());
    wxXmlDocument scene; require(scene.Load(input), "snapshot XML readable");
    auto* root = scene.GetRoot();
    require(root->GetName() == "workspace" && root->GetAttribute("width") == "1000", "snapshot uses real viewport");
    auto* components = root->GetChildren()->GetNext();
    require(components->GetName() == "components" && components->GetChildren()->GetAttribute("ref") == "c2", "persistent component identity");
    const auto png = wxBase64Decode(context.imageBase64);
    require(png.GetDataLen() > 24 && std::memcmp(png.GetData(), "\x89PNG\r\n\x1a\n", 8) == 0, "valid rendered PNG");
    require(canvas.CaptureAssistantContext(context, error, false) && context.imageBase64.empty(), "image sharing can be disabled");
    AiCircuitPatch patch;
    patch.operations = {
        {"update_component", "c2", {{"x", "180"}, {"y", "120"}, {"rotation", "90"}}, {}},
        {"delete_wire", "w0", {}, {}},
        {"add_component", "n_gate", {{"type", "NOT"}, {"inputCount", "1"}, {"x", "400"}, {"y", "120"}}, {}},
        {"add_wire", "n_w1", {{"from", "c2"}, {"outPin", "OUT"}, {"to", "n_gate"}, {"inPin", "IN"}}, {}},
        {"add_wire", "n_w2", {{"from", "n_gate"}, {"outPin", "OUT"}, {"to", "c7"}, {"inPin", "IN"}}, {}},
        {"route_wire", "n_w1", {}, {{200, 160}, {300, 160}, {300, 140}, {400, 140}}},
        {"update_text", "t0", {{"text", wxS("输入 A")}, {"x", "120"}}, {}},
        {"add_text", "n_label", {{"text", "A"}, {"x", "180"}, {"y", "80"}, {"width", "100"}, {"height", "25"}}, {}},
    };
    CircuitDocument candidate;
    require(AiCircuitExecutor::BuildPatchCandidate(base, patch, candidate, error, canvas.GetClientSize()), "existing component, text, wire editing candidate");
    require(candidate.components.size() == 3 && candidate.components[0].id == 2 && candidate.components[0].x == 180 &&
        candidate.components[0].rotation == 90 && candidate.texts.size() == 2 && candidate.wires.size() == 2 &&
        candidate.wires[0].route.size() == 4, "patch resolves existing and new objects by reference");
    require(base.components[0].x == 60 && canvas.ExportDocument().components.size() == 2, "candidate building is non-mutating");
    const auto revision = canvas.GetDocumentRevision();
    require(canvas.ApplyAssistantDocument(candidate, revision, error), "first edit commits");
    AiCircuitPatch next;
    next.operations = {{"update_component", "c7", {{"y", "220"}}, {}}};
    CircuitDocument improved;
    require(AiCircuitExecutor::BuildPatchCandidate(canvas.ExportDocument(), next, improved, error, canvas.GetClientSize()), "review can modify a current object");
    require(canvas.ApplyAssistantDocument(improved, canvas.GetDocumentRevision(), error, true), "review commits into same undo group");
    require(!canvas.ApplyAssistantDocument(candidate, revision, error), "stale context cannot overwrite current page");
    require(canvas.Undo(), "one undo restores all improvement rounds");
    const auto restored = canvas.ExportDocument();
    require(restored.components.size() == 2 && restored.components[0].x == 60 && restored.components[0].rotation == 0 &&
        restored.wires.size() == 1 && restored.texts.size() == 1 && restored.texts[0].text == wxS("原有标注") && !canvas.CanUndo(),
        "whole edit restores original circuit and history");
    const auto reject = [&](const AiCircuitPatch& invalid) {
        auto preserved = base;
        require(!AiCircuitExecutor::BuildPatchCandidate(base, invalid, preserved, error, canvas.GetClientSize()) && !error.empty(), "bad patch rejected");
        require(preserved.components[0].x == 60 && preserved.components.size() == 2 && canvas.ExportDocument().components[0].x == 60,
            "bad patch changes neither output nor live document");
    };
    reject({{}, {{"update_component", "c_missing", {{"x", "100"}}, {}}}});
    reject({{}, {{"update_component", "c2", {{"x", "200"}}, {}}, {"delete_component", "c_missing", {}, {}}}});
    reject({{}, {{"update_component", "c2", {{"x", "660"}}, {}}}});
    reject({{}, {{"update_component", "c2", {{"x", "9999"}}, {}}}});
    reject({{}, {{"add_wire", "n_bad", {{"from", "c2"}, {"outPin", "OUT"}, {"to", "c7"}, {"inPin", "IN"}}, {}}}});
    reject({{}, {{"route_wire", "w0", {}, {{100, 120}, {660, 200}}}}});
    reject({{}, {{"update_component", "c2", {{"clockPeriodNs", "30"}}, {}}}});
    AiCircuitPatch remove;
    remove.operations = {{"delete_component", "c2", {}, {}}, {"delete_text", "t0", {}, {}}};
    require(AiCircuitExecutor::BuildPatchCandidate(base, remove, candidate, error, canvas.GetClientSize()) &&
        candidate.components.size() == 1 && candidate.wires.empty() && candidate.texts.empty(), "delete removes exactly requested objects and attached wires");
    canvas.ClearDocument();
}
