#pragma once

#include "Canvas.h"
#include "MainFrame.h"
#include "MenuBar.h"
#include "Toolbox.h"
#include "KiCadSchematicExporter.h"
#include "GedaNetlistExporter.h"
#include "KiCadNetlistExporter.h"
#include "KiCadSExpr.h"
#include "SelectionState.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace TutorialMergeRegressionDetail
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(std::string("tutorial merge: ") + message);
}

// Keep the user's clipboard untouched while testing ordinary copy/paste.
class MemoryClipboard : public wxClipboard
{
    bool opened = false;
    std::unique_ptr<wxDataObject> stored;
public:
    bool Open() override { opened = true; return true; }
    void Close() override { opened = false; }
    bool IsOpened() const override { return opened; }
    bool SetData(wxDataObject* data) override { stored.reset(data); return true; }
    bool AddData(wxDataObject* data) override { return SetData(data); }
    void Clear() override { stored.reset(); }
    bool Flush() override { return true; }
    bool IsSupported(const wxDataFormat& format) override
    {
        return stored && stored->IsSupported(format, wxDataObject::Get);
    }
    bool GetData(wxDataObject& data) override
    {
        const auto format = data.GetPreferredFormat(wxDataObject::Set);
        if (!IsSupported(format)) return false;
        std::vector<unsigned char> bytes(stored->GetDataSize(format));
        return stored->GetDataHere(format, bytes.data()) && data.SetData(format, bytes.size(), bytes.data());
    }
};

inline CircuitDocument SimpleAnd()
{
    CircuitDocument document;
    document.components = {{1, "INPUT", 60, 80, 0, true}, {2, "INPUT", 60, 200, 0, false},
        {3, "AND", 280, 100, 2}, {4, "OUTPUT", 540, 140, 1}};
    document.wires = {{1, 0, 3, 0}, {2, 0, 3, 1}, {3, 0, 4, 0}};
    return document;
}

inline CircuitDocument FullAdder()
{
    CircuitDocument document;
    document.components = {{1, "INPUT", 20, 20, 0}, {2, "INPUT", 20, 100, 0}, {3, "INPUT", 20, 180, 0},
        {4, "XOR", 180, 20, 2}, {5, "XOR", 360, 20, 2}, {6, "AND", 180, 200, 2},
        {7, "AND", 360, 200, 2}, {8, "OR", 540, 200, 2}, {9, "OUTPUT", 720, 20, 1},
        {10, "OUTPUT", 720, 200, 1}};
    document.wires = {{1, 0, 4, 0}, {2, 0, 4, 1}, {4, 0, 5, 0}, {3, 0, 5, 1},
        {1, 0, 6, 0}, {2, 0, 6, 1}, {4, 0, 7, 0}, {3, 0, 7, 1},
        {6, 0, 8, 0}, {7, 0, 8, 1}, {5, 0, 9, 0}, {8, 0, 10, 0}};
    return document;
}

inline void WriteFile(const wxString& filename, const std::string& content)
{
    wxFile file(filename, wxFile::write);
    Require(file.IsOpened() && file.Write(content.data(), content.size()) == content.size() && file.Close(),
        "write self-owned exporter fixture");
}

inline std::string ReadFile(const wxString& filename)
{
    wxFile file(filename, wxFile::read);
    Require(file.IsOpened() && file.Length() > 0 && file.Length() < 1024 * 1024,
        "export produces a nonempty bounded file");
    std::string bytes(static_cast<std::size_t>(file.Length()), '\0');
    Require(file.Read(bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()), "read complete export");
    return bytes;
}

inline void TestExport()
{
    // This fixture checks generated structure, not KiCad's electrical library
    // interpretation. It requires neither an installed library nor a CLI.
    struct TemporaryFiles
    {
        wxString rootDirectory;
        wxString directory;
        TemporaryFiles()
        {
            rootDirectory = wxFileName::CreateTempFileName("wx_demo_merge_export_");
            Require(!rootDirectory.empty() && wxRemoveFile(rootDirectory) && wxFileName::Mkdir(rootDirectory),
                "create exporter test directory");
            directory = wxFileName(rootDirectory, wxS("中文导出目录")).GetFullPath();
            Require(wxFileName::Mkdir(directory), "create Unicode exporter test subdirectory");
        }
        wxString Path(const wxString& name) const { return wxFileName(directory, name).GetFullPath(); }
        ~TemporaryFiles()
        {
            wxLogNull quiet;
            for (const auto* name : {wxS("74xx.kicad_sym"), wxS("与门导出.kicad_sch"), wxS("全加器导出.net"), wxS("全加器KiCad网表.net")})
                if (wxFileExists(Path(name))) wxRemoveFile(Path(name));
            wxFileName::Rmdir(directory);
            wxFileName::Rmdir(rootDirectory);
        }
    } files;
    std::string library = "(kicad_symbol_lib (version 20231120) (generator \"merge-test\")";
    for (const auto* symbol : {"74LS86", "74LS09", "74LS08", "74LS32"})
        library += std::string("(symbol \"") + symbol + "\" (property \"Reference\" \"U\"))";
    library += ")";
    WriteFile(files.Path("74xx.kicad_sym"), library);
    std::string error;
    Require(KiCadSchematicExporter::ExportSchematic(SimpleAnd(),
        files.Path(wxS("与门导出.kicad_sch")).ToStdString(wxConvUTF8),
        files.Path("74xx.kicad_sym").ToStdString(wxConvUTF8), error), "simple AND schematic export succeeds");
    KiCadSExpr root;
    const auto schematicBytes = ReadFile(files.Path(wxS("与门导出.kicad_sch")));
    Require(KiCadSExprParser::Parse(schematicBytes, root, error) &&
        root.Head() == "kicad_sch" && root.Find("version") && root.Find("lib_symbols"),
        "schematic output is a parseable KiCad s-expression with version and embedded symbols");
    Require(root.FindAll("wire").size() == 3 && !root.FindAll("symbol").empty(),
        "export retains the AND instance and all three wires");
    for (const bool invalidWire : {false, true})
    {
        auto invalid = SimpleAnd();
        if (invalidWire) invalid.wires[0].targetPin = 99;
        else
        {
            invalid.formatVersion = 3;
            invalid.components.push_back({5, "CLOCK", 20, 300, 0});
        }
        Require(!KiCadSchematicExporter::ExportSchematic(invalid,
            files.Path(wxS("与门导出.kicad_sch")).ToStdString(wxConvUTF8),
            files.Path("74xx.kicad_sym").ToStdString(wxConvUTF8), error) && !error.empty(),
            "schematic exporter rejects unsupported components and invalid wires before replacing a file");
        Require(ReadFile(files.Path(wxS("与门导出.kicad_sch"))) == schematicBytes,
            "rejected schematic export preserves the original file bytes");
    }

    Require(GedaNetlistExporter::ExportFullAdder(FullAdder(), files.Path(wxS("全加器导出.net")).ToStdString(wxConvUTF8), error),
        "full-adder gEDA export supports Unicode paths");
    const auto netlistBytes = ReadFile(files.Path(wxS("全加器导出.net")));
    std::istringstream lines(netlistBytes);
    std::string line;
    int nets = 0;
    while (std::getline(lines, line))
    {
        std::istringstream words(line);
        std::string net, endpoint;
        Require(static_cast<bool>(words >> net >> endpoint), "each gEDA line names a net and at least one chip pin");
        do
        {
            Require(endpoint.size() >= 4 && endpoint[0] == 'U' && endpoint.find('-') != std::string::npos,
                "gEDA endpoint uses chip-reference and pin-number syntax");
        } while (words >> endpoint);
        ++nets;
    }
    Require(nets >= 5 && netlistBytes.find("GND ") != std::string::npos && netlistBytes.find("Vcc ") != std::string::npos,
        "gEDA output contains signal nets and power nets");
    auto invalidAdder = FullAdder();
    invalidAdder.wires[0].targetPin = 99;
    Require(!GedaNetlistExporter::ExportFullAdder(invalidAdder,
        files.Path(wxS("全加器导出.net")).ToStdString(wxConvUTF8), error) && !error.empty(),
        "gEDA exporter rejects invalid connections");
    Require(ReadFile(files.Path(wxS("全加器导出.net"))) == netlistBytes,
        "rejected gEDA export preserves the original file bytes");

    const auto kicadPath = files.Path(wxS("全加器KiCad网表.net"));
    Require(KiCadNetlistExporter::ExportFullAdder(FullAdder(), kicadPath.ToStdString(wxConvUTF8), error),
        "direct KiCad full-adder netlist supports Unicode paths without a CLI");
    const auto kicadBytes = ReadFile(kicadPath);
    Require(KiCadSExprParser::Parse(kicadBytes, root, error) && root.Head() == "export" &&
        root.Find("components") && root.Find("libparts") && root.Find("nets"), "KiCad netlist uses native s-expression structure");
    Require(root.Find("components")->FindAll("comp").size() == 3 &&
        root.Find("libparts")->FindAll("libpart").size() == 3, "KiCad netlist retains all three chip packages and libraries");
    for (const auto* part : root.Find("libparts")->FindAll("libpart"))
        Require(part->Find("pins") && part->Find("pins")->FindAll("pin").size() == 14, "each chip has its physical DIP-14 pin definition");
    std::set<std::string> usedPins;
    const auto kicadNets = root.Find("nets")->FindAll("net");
    Require(kicadNets.size() == 8, "KiCad full-adder retains all signal networks");
    for (const auto* net : kicadNets)
        for (const auto* node : net->FindAll("node"))
        {
            const auto* ref = node->Find("ref"); const auto* pin = node->Find("pin");
            Require(ref && ref->children.size() == 2 && pin && pin->children.size() == 2,
                "net nodes carry chip reference and physical pin number");
            Require(usedPins.insert(ref->children[1].value + ":" + pin->children[1].value).second,
                "a physical pin belongs to exactly one signal network");
        }
    Require(usedPins.size() == 15, "all five logical gates retain their physical pins");
    for (const auto& invalid : {invalidAdder, SimpleAnd()})
    {
        Require(!KiCadNetlistExporter::ExportFullAdder(invalid, kicadPath.ToStdString(wxConvUTF8), error) && !error.empty(),
            "KiCad exporter rejects invalid or unsupported documents");
        Require(ReadFile(kicadPath) == kicadBytes, "failed KiCad export leaves the existing file intact");
    }
}
}

struct CanvasTutorialRegressionAccess
{
    static wxBitmap Render(Canvas& canvas)
    {
        const auto size = canvas.GetClientSize();
        wxBitmap bitmap(size.x, size.y);
        wxMemoryDC dc(bitmap);
        canvas.DrawCanvas(dc, wxRect(wxPoint(), size));
        dc.SelectObject(wxNullBitmap);
        return bitmap;
    }

    static void TestLabel(Canvas& canvas)
    {
        using namespace TutorialMergeRegressionDetail;
        canvas.ClearDocument();
        CircuitDocument document;
        document.components = {{1, "OUTPUT", 320, 140, 1}};
        wxString error;
        Require(canvas.ImportDocument(document, error), "label geometry fixture imports");
        const wxString label = wxS("SUM_CARRY_WIDE_LABEL");
        Require(canvas.SetTutorialLabel(1, label), "tutorial label attaches to the placed port ID");
        wxClientDC measure(&canvas);
        measure.SetFont(wxFontInfo(11).Family(wxFONTFAMILY_SWISS).Bold());
        const auto size = measure.GetTextExtent(label);
        const auto& placed = canvas.m_components.front();
        const auto anchor = canvas.ComponentToWorld(placed, wxPoint(canvas.m_portSize + 8, canvas.m_portSize / 2));
        const wxRect textBounds(anchor.x, anchor.y - size.y / 2, size.x, size.y);
        const auto beforeBounds = canvas.GetComponentRect(placed);
        Require(beforeBounds.Contains(textBounds), "component hit/refresh bounds include the full 11-point bold tutorial tag");

        auto partial = Render(canvas);
        canvas.m_pressedIndex = 0;
        canvas.m_dragOffset = wxPoint();
        canvas.MoveDraggedComponent(wxPoint(80, 280), false);
        wxRect dirty = beforeBounds.Union(canvas.GetComponentRect(canvas.m_components.front()));
        dirty.Inflate(3);
        {
            wxMemoryDC dc(partial);
            wxDCClipper clip(dc, dirty);
            canvas.DrawCanvas(dc, dirty);
        }
        const auto expected = Render(canvas).ConvertToImage();
        const auto actual = partial.ConvertToImage();
        const auto bytes = static_cast<std::size_t>(actual.GetWidth()) * actual.GetHeight() * 3;
        Require(std::equal(actual.GetData(), actual.GetData() + bytes, expected.GetData()),
            "moving a wide tutorial tag leaves no pixels outside the dirty region");
        canvas.ClearDocument();
    }

    static void TestNotifications(Canvas& canvas)
    {
        using namespace TutorialMergeRegressionDetail;
        canvas.ClearDocument();
        int placements = 0, placedId = 0;
        wxString placedType;
        canvas.SetComponentPlacedCallback([&](const wxString& type, int id) { ++placements; placedType = type; placedId = id; });
        struct ClearCallbacks
        {
            Canvas& canvas;
            ~ClearCallbacks() { canvas.SetComponentPlacedCallback({}); canvas.SetWireCreatedCallback({}); }
        } cleanup{canvas};
        g_selectedType = "INPUT";
        wxMouseEvent place(wxEVT_LEFT_DOWN);
        place.SetPosition(wxPoint(100, 100));
        canvas.OnLeftDown(place);
        Require(placements == 1 && placedType == "INPUT" && placedId == canvas.ExportDocument().components[0].id,
            "successful placement reports the real type and persistent component ID");
        Require(g_selectedType.empty(), "placement still restores selection mode");
        canvas.DeselectAll();
        Require(placements == 2 && placedType.empty() && placedId == 0, "deselect uses empty/zero only to reset tool presentation");

        auto document = SimpleAnd();
        document.wires.clear();
        wxString error;
        Require(canvas.ImportDocument(document, error), "wire callback fixture imports");
        int wires = 0;
        CircuitWireData latest;
        canvas.SetWireCreatedCallback([&](const CircuitWireData& wire) { ++wires; latest = wire; });
        const auto pin = [&](std::size_t component, PinType type, std::size_t index) {
            return canvas.GetPinPosition({canvas.m_components[component].comp, type, index});
        };
        canvas.HandleWireClick(pin(0, PinType::Output, 0));
        Require(wires == 0, "an unfinished wire never advances tutorial progress");
        canvas.HandleWireClick(pin(2, PinType::Input, 0));
        Require(wires == 1 && latest.sourceId == 1 && latest.sourcePin == 0 && latest.targetId == 3 && latest.targetPin == 0,
            "successful wire callback reports source and destination IDs/pins");
        canvas.HandleWireClick(pin(0, PinType::Output, 0));
        canvas.HandleWireClick(pin(2, PinType::Input, 0));
        Require(wires == 1 && canvas.m_circuit.GetConnections().size() == 1, "duplicate wire has no tutorial callback");
        canvas.HandleWireClick(pin(1, PinType::Output, 0));
        canvas.HandleWireClick(pin(2, PinType::Input, 0));
        Require(wires == 1 && canvas.m_circuit.GetConnections().size() == 1, "occupied input has no tutorial callback");
        canvas.CancelPendingWire();
        canvas.HandleWireClick(pin(0, PinType::Output, 0));
        canvas.HandleWireClick(pin(1, PinType::Output, 0));
        Require(wires == 1, "output-to-output rejection has no tutorial callback");
        canvas.ClearDocument();
        g_selectedType.Clear();
    }

    static void TestMainFrame()
    {
        using namespace TutorialMergeRegressionDetail;
        wxLogNull quiet;
        MemoryClipboard clipboard;
        auto frame = std::make_unique<MainFrame>();
        frame->Move(-32000, -32000);
        frame->ShowWithoutActivating();
        frame->Layout();
        auto& canvas = *frame->m_canvas;
        canvas.m_clipboard = &clipboard;
        const int ids[] = {ID_HELP_AI_ASSISTANT, ID_START_TUTORIAL, ID_FILE_IMPORT_KICAD_PCB,
            ID_EXPORT_KICAD_NETLIST, ID_EXPORT_KICAD_SCHEMATIC};
        for (std::size_t i = 0; i < std::size(ids); ++i)
        {
            auto* item = frame->GetMenuBar()->FindItem(ids[i]);
            Require(item && item->IsEnabled() && !item->GetItemLabelText().empty(),
                "AI, tutorial, PCB import and the new export commands remain labeled and reachable in the merged menu");
            for (std::size_t j = 0; j < i; ++j) Require(ids[i] != ids[j], "merged menu commands have distinct IDs");
        }
        Require(canvas.GetClientSize().x > canvas.FromDIP(200) && frame->GetToolbox()->IsShownOnScreen(),
            "merged main window leaves the component library and canvas usable");
        const auto chooseTool = [&](int id) {
            wxCommandEvent choose(wxEVT_MENU, id);
            frame->GetEventHandler()->ProcessEvent(choose);
        };
        chooseTool(ID_TOOL_INPUT);
        Require(frame->GetToolbox()->GetSelectedType() == "INPUT", "menu tool selection synchronizes the library");
        wxMouseEvent place(wxEVT_LEFT_DOWN);
        place.SetPosition(wxPoint(100, 100));
        canvas.OnLeftDown(place);
        Require(frame->GetToolbox()->GetSelectedType().empty() && g_selectedType.empty(),
            "merged placement callback preserves the old library reset behavior");
        for (std::size_t i = 0; i < frame->GetToolBar()->GetToolsCount(); ++i)
        {
            const auto* tool = frame->GetToolBar()->GetToolByPos(static_cast<int>(i));
            if (tool->IsSeparator()) continue;
            Require(frame->GetToolBar()->GetToolState(tool->GetId()) == (tool->GetLabel() == wxS("选择")),
                "merged placement callback restores only the selection toolbar button");
        }
        canvas.CreateTextBox(wxRect(200, 200, 180, 80));
        canvas.m_textBoxes.back().editor->ChangeValue(wxS("clipboard merge test"));
        canvas.SetFocusIgnoringChildren();
        canvas.m_selectedIndex = -1;
        canvas.m_selectedTextIndex = 0;
        Require(canvas.CopySelection(), "copy a note using the memory clipboard");
        chooseTool(ID_TOOL_TEXT);
        Require(frame->GetToolbox()->GetSelectedType() == "TEXT", "prepare visibly active text tool before paste");
        Require(canvas.PasteSelection(), "paste a note through the merged callback path");
        Require(canvas.ExportDocument().texts.size() == 2 && frame->GetToolbox()->GetSelectedType().empty(),
            "pure-text paste also resets the library without reporting a fabricated component placement");

        const auto previousDocument = canvas.ExportDocument();
        Require(!previousDocument.components.empty() && !previousDocument.texts.empty(),
            "tutorial starts with real earlier circuit and annotation contents");
        auto& tutorial = *frame->m_tutorialManager;
        tutorial.StartTutorial(wxS("半加器 (Half Adder)"));
        Require(tutorial.m_active && tutorial.m_stepIndex == 0 && canvas.ExportDocument().components.empty() &&
            canvas.ExportDocument().texts.empty(), "starting a tutorial creates a fresh teaching canvas");
        tutorial.NotifyToolSelected("INPUT");
        Require(tutorial.m_currentStepCompleted && tutorial.m_stepIndex == 0,
            "a tutorial selection queues its next step instead of advancing synchronously");
        wxString error;
        Require(canvas.ApplyAssistantDocument(SimpleAnd(), canvas.GetDocumentRevision(), error),
            "a complete AI proposal can replace the tutorial canvas atomically");
        Require(!tutorial.m_active && tutorial.m_stepIndex == 0,
            "successful AI replacement cancels the previous teaching session");
        wxTheApp->ProcessPendingEvents();
        Require(!tutorial.m_active && tutorial.m_stepIndex == 0 && canvas.ExportDocument().components.size() == 4 &&
            canvas.ExportDocument().wires.size() == 3,
            "a queued tutorial step cannot revive or modify the AI replacement");

        tutorial.StartTutorial(wxS("半加器 (Half Adder)"));
        tutorial.NotifyToolSelected("INPUT");
        Require(tutorial.m_active && tutorial.m_currentStepCompleted && tutorial.m_stepIndex == 0,
            "prepare a pending tutorial callback before destroying its owner");
        const std::weak_ptr<int> tutorialLifetime = tutorial.m_lifetime;
        frame->Hide();
        frame.reset();
        Require(tutorialLifetime.expired(), "destroying the main window releases the tutorial callback lifetime");
        wxTheApp->ProcessPendingEvents();
        g_selectedType.Clear();
    }
};

inline void RunTutorialMergeRegression(Canvas& canvas)
{
    CanvasTutorialRegressionAccess::TestLabel(canvas);
    CanvasTutorialRegressionAccess::TestNotifications(canvas);
    CanvasTutorialRegressionAccess::TestMainFrame();
    TutorialMergeRegressionDetail::TestExport();
}
