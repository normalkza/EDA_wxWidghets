#pragma once

#include "KiCadPcbImporter.h"
#include "KiCadSExpr.h"
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace PcbImportRegression
{
inline void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(std::string("KiCad PCB import: ") + message);
}

inline bool Near(double first, double second)
{
    return std::abs(first - second) < 0.000001;
}

inline std::string Minimal(const std::string& content = {}, int version = 20211014)
{
    return "(kicad_pcb (version " + std::to_string(version) + ") (generator wx_demo_test) "
        "(general (thickness 1.6)) (paper \"A4\") "
        "(layers (0 \"F.Cu\" signal) (31 \"B.Cu\" signal) (44 \"Edge.Cuts\" user)) "
        "(setup (pad_to_mask_clearance 0)) (net 0 \"\") " + content + ")";
}

inline std::string FileBytes(const wxString& path)
{
    wxFile file(path, wxFile::read);
    Require(file.IsOpened(), "read fixture file");
    const auto size = file.Length();
    Require(size >= 0 && size <= static_cast<wxFileOffset>(KiCadPcbImporter::MaxFileBytes), "fixture size is bounded");
    std::string source(static_cast<std::size_t>(size), '\0');
    Require(file.Read(source.data(), source.size()) == static_cast<ssize_t>(source.size()), "read exact fixture bytes");
    return source;
}

inline const std::string& NetName(const PcbDocument& document, int id)
{
    const auto found = document.nets.find(id);
    Require(found != document.nets.end(), "every imported endpoint resolves to an electrical net");
    return found->second;
}

inline void TestSExpression()
{
    KiCadSExpr expression;
    std::string error;
    Require(KiCadSExprParser::Parse("(root (text \"中文 (保留) \\\"引号\\\" \\\\路径\") (at -2.54 1.25))", expression, error),
        "parse UTF-8, quoted parentheses, escaped quote and backslash");
    Require(expression.Head() == "root" && expression.Find("text") && expression.FindAll("at").size() == 1,
        "S-expression field lookup is structural");
    const auto* text = expression.Find("text");
    Require(text->children.size() == 2 && text->children[1].quoted &&
        text->children[1].value == "中文 (保留) \"引号\" \\路径", "quoted strings decode correctly");
    Require(!KiCadSExprParser::Parse("(root \"unterminated)", expression, error) && !error.empty(), "reject unterminated quote");
    Require(!KiCadSExprParser::Parse("(root) garbage", expression, error), "reject trailing atoms");
    Require(!KiCadSExprParser::Parse("(root) (second)", expression, error), "reject multiple roots");
    const auto depth = KiCadSExprParser::MaxDepth + 4;
    const std::string deep(depth, '(');
    Require(!KiCadSExprParser::Parse(deep + "x" + std::string(depth, ')'), expression, error) && !error.empty(),
        "nesting depth budget is enforced before recursive overflow");
    std::string nodes = "(root";
    nodes.reserve(KiCadSExprParser::MaxNodes * 2 + 16);
    for (std::size_t index = 0; index <= KiCadSExprParser::MaxNodes; ++index) nodes += " x";
    nodes += ')';
    Require(!KiCadSExprParser::Parse(nodes, expression, error) && !error.empty(), "S-expression node budget is enforced");
}

inline void TestOldFormatAndTransforms()
{
    const std::string fields =
        "(net 1 \"SIG\\\"A\") (net 2 \"GND\") "
        "(footprint \"wx_demo:Back\" (layer \"B.Cu\") (at 20 30 90) "
        "(fp_text reference \"U1\" (at 0 -2 90) (layer \"B.SilkS\") (effects (font (size 1 1)))) "
        "(fp_text value \"器件\" (at 0 2 90) (layer \"B.Fab\") (effects (font (size 1 1)))) "
        "(fp_line (start 0 0) (end 2 0) (layer \"B.SilkS\") (width 0.15)) "
        "(pad \"1\" smd rect (at 2 0 90) (size 2 1) (layers \"B.Cu\" \"B.Mask\") (net 1 \"SIG\\\"A\"))) "
        "(segment (start 20 28) (end 25 28) (width 0.25) (layer \"B.Cu\") (net 1)) "
        "(via (at 25 28) (size 0.8) (drill 0.4) (layers \"F.Cu\" \"B.Cu\") (net 1)) "
        "(arc (start 25 28) (mid 26 27) (end 27 28) (width 0.25) (layer \"F.Cu\") (net 2)) "
        "(gr_rect (start 10 10) (end 40 40) (stroke (width 0.1) (type solid)) (fill none) (layer \"Edge.Cuts\"))";
    PcbDocument document;
    wxString error;
    const auto source = Minimal(fields);
    Require(KiCadPcbImporter::Parse(source, document, error) && error.empty(), "import KiCad 6 numeric net format");
    Require(document.version == 20211014 && document.originalSource == source, "retain version and exact source bytes");
    Require(document.footprints.size() == 1 && document.footprints[0].reference == "U1" &&
        document.footprints[0].value == "器件", "legacy fp_text reference and value import");
    const auto& footprint = document.footprints[0];
    Require(footprint.layer == "B.Cu" && footprint.pads.size() == 1, "backside footprint and pad retained");
    const auto world = PcbTransformFootprintPoint(footprint, {2, 0});
    Require(Near(world.x, 20) && Near(world.y, 28), "KiCad positive rotation maps local +X toward negative Y");
    Require(Near(footprint.pads[0].position.x, 20) && Near(footprint.pads[0].position.y, 28) &&
        Near(footprint.pads[0].rotation, 90), "pad world geometry is transformed once without an extra backside mirror");
    const auto line = std::find_if(footprint.graphics.begin(), footprint.graphics.end(), [](const auto& graphic) {
        return graphic.kind == PcbGraphicKind::Line;
    });
    Require(line != footprint.graphics.end() && line->points.size() == 2 &&
        Near(line->points[0].x, 20) && Near(line->points[0].y, 30) &&
        Near(line->points[1].x, 20) && Near(line->points[1].y, 28),
        "footprint graphics use the same world transform as pads");
    Require(document.tracks.size() == 2 && document.vias.size() == 1 && document.graphics.size() == 1,
        "segments, arc, via and board graphics import");
    Require(document.tracks[1].mid && Near(document.tracks[1].mid->x, 26) &&
        Near(document.tracks[1].mid->y, 27), "track arcs preserve their through-point");
    Require(NetName(document, footprint.pads[0].net) == "SIG\"A" &&
        footprint.pads[0].net == document.tracks[0].net && footprint.pads[0].net == document.vias[0].net,
        "physical endpoint connectivity uses explicit net identity");
    Require(document.nets.at(0).empty(), "net 0 remains unconnected");
    Require(document.bounds.valid && document.bounds.minX <= 10 && document.bounds.maxX >= 40,
        "board bounds include imported graphics");
    for (const int version : {20211014, 20221018, 20240108, 20241229})
        Require(KiCadPcbImporter::Parse(Minimal({}, version), document, error), "compatible KiCad 6 to 9 headers import");
    Require(document.footprints.empty() && document.tracks.empty() && document.vias.empty(),
        "valid empty replacement clears previous imported geometry");

    const auto fullPadStack = Minimal(
        "(footprint \"full-stack\" (layer \"F.Cu\") (at 10 10) "
        "(pad \"1\" thru_hole circle (at 0 0) (size 2 2) (drill 1) "
        "(layers \"F.Cu\" \"In1.Cu\" \"In30.Cu\" \"B.Cu\" \"*.Mask\") (net 0)))");
    Require(KiCadPcbImporter::Parse(fullPadStack, document, error), "full copper padstack imports on a two-layer board");
    Require(std::none_of(document.layers.begin(), document.layers.end(), [](const auto& layer) {
        return layer.name == "In1.Cu" || layer.name == "In30.Cu";
    }), "inactive full-padstack copper entries cannot fabricate extra board layers");
    const auto& activePadLayers = document.footprints[0].pads[0].layers;
    Require(std::find(activePadLayers.begin(), activePadLayers.end(), "In1.Cu") == activePadLayers.end() &&
        std::find(activePadLayers.begin(), activePadLayers.end(), "In30.Cu") == activePadLayers.end(),
        "inactive copper padstack layers are excluded from normalized visible geometry");
}

inline void TestFailureTransactions()
{
    PcbDocument document;
    wxString error;
    const auto original = Minimal("(net 1 \"saved\")");
    Require(KiCadPcbImporter::Parse(original, document, error), "prepare transaction fixture");
    document.sourcePath = wxT("原始文件.kicad_pcb");
    const std::vector<std::string> invalid{
        "", "()", "(kicad_sch (version 20211014))", "(kicad_pcb (version nope))",
        "(kicad_pcb (version (nested)))", "(kicad_pcb (version 20211014)",
        Minimal() + " extra", Minimal("(net 1 \"A\") (net 1 \"B\")"),
        Minimal("(segment (start nan 0) (end 1 0) (width 0.2) (layer \"F.Cu\") (net 0))"),
        Minimal("(segment (start 0 inf) (end 1 0) (width 0.2) (layer \"F.Cu\") (net 0))"),
        Minimal("(segment (start 0 0) (end 1 0) (width -1) (layer \"F.Cu\") (net 0))"),
        Minimal("(segment (start 0 0) (end 1 0) (width 0.2) (layer \"F.Cu\") (net 99))"),
        Minimal("(via (at 0 0) (size 0.5) (drill 0.8) (layers \"F.Cu\" \"B.Cu\") (net 0))"),
        Minimal("(footprint \"x\" (layer \"F.Cu\") (at 0 0) (pad \"1\" smd rect (at 0 0) (size 0 1) (layers \"F.Cu\") (net 0)))"),
        Minimal("(segment (start (bad) 0) (end 1 0) (width 0.2) (layer \"F.Cu\") (net 0))")};
    for (const auto& source : invalid)
    {
        Require(!KiCadPcbImporter::Parse(source, document, error) && !error.empty(), "reject malformed or electrically ambiguous PCB data");
        Require(document.originalSource == original && document.version == 20211014 &&
            document.sourcePath == wxT("原始文件.kicad_pcb") && document.nets.at(1) == "saved",
            "parse failures preserve the prior document and source association");
    }
    auto invalidUtf8 = Minimal("(property \"note\" \"bad\")");
    invalidUtf8.insert(invalidUtf8.find("bad") + 3, std::string("\xc3\x28", 2));
    Require(!KiCadPcbImporter::Parse(invalidUtf8, document, error), "reject invalid UTF-8");
    auto nul = Minimal();
    nul.insert(10, 1, '\0');
    Require(!KiCadPcbImporter::Parse(nul, document, error), "reject embedded NUL bytes");
}

inline void TestShippedBoardAndFileIO()
{
    wxFileName fixture(wxFileName(wxString::FromUTF8(__FILE__)).GetPath() + wxFILE_SEP_PATH + wxT("..") +
        wxFILE_SEP_PATH + wxT("examples") + wxFILE_SEP_PATH + wxT("kicad_import_demo.kicad_pcb"));
    fixture.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    const auto source = FileBytes(fixture.GetFullPath());
    PcbDocument document;
    wxString error;
    Require(KiCadPcbImporter::Load(fixture.GetFullPath(), document, error) && error.empty(), "shipped native KiCad board loads");
    Require(document.originalSource == source && document.sourcePath == fixture.GetFullPath(),
        "load retains exact original bytes and source path");
    Require(document.version == 20260206 && document.footprints.size() == 2 && document.tracks.size() == 7 &&
        document.vias.size() == 1 && document.zones.size() == 1, "KiCad 10 generated board retains native object counts");
    const auto front = std::find_if(document.footprints.begin(), document.footprints.end(), [](const auto& fp) { return fp.reference == "J1"; });
    const auto back = std::find_if(document.footprints.begin(), document.footprints.end(), [](const auto& fp) { return fp.reference == "R1"; });
    Require(front != document.footprints.end() && back != document.footprints.end() && front->pads.size() == 2 && back->pads.size() == 2,
        "both front and back footprints contain their real pads");
    Require(front->value == "连接器" && Near(back->pads[0].position.x, 90) && Near(back->pads[0].position.y, 66),
        "native KiCad transform and UTF-8 values match the source");
    Require(NetName(document, front->pads[0].net) == "/SIGNAL" && NetName(document, front->pads[1].net) == "GND" &&
        front->pads[0].net == back->pads[0].net && front->pads[0].net == document.vias[0].net,
        "KiCad 10 name-only net syntax connects pads, vias and tracks correctly");
    Require(document.nets.at(0).empty() && document.nets.size() == 3, "named net normalization keeps disconnected net 0");
    Require(std::any_of(document.tracks.begin(), document.tracks.end(), [](const auto& track) { return track.mid.has_value(); }),
        "native arc imports as a real curved copper track");
    Require(document.zones[0].polygons.size() >= 2 &&
        std::any_of(document.zones[0].polygons.begin(), document.zones[0].polygons.end(), [](const auto& polygon) { return polygon.filled; }),
        "zone outline and saved fill polygons import separately");
    Require(document.bounds.valid && document.bounds.minX <= 50 && document.bounds.maxX >= 104 &&
        document.bounds.minY <= 50 && document.bounds.maxY >= 82, "native board bounds cover the entire outline");
    Require(FileBytes(fixture.GetFullPath()) == source, "import never modifies the KiCad source");

    const wxString temporary = wxFileName::CreateTempFileName(wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxT("KiCad_导入测试_"));
    Require(!temporary.empty(), "create owned Unicode-path test file");
    struct Cleanup { wxString path; ~Cleanup() { wxRemoveFile(path); } } cleanup{temporary};
    {
        wxFile file(temporary, wxFile::write);
        Require(file.IsOpened() && file.Write(source.data(), source.size()) == static_cast<ssize_t>(source.size()) && file.Close(),
            "write exact KiCad bytes to Unicode path");
    }
    Require(KiCadPcbImporter::Load(temporary, document, error) && document.originalSource == source, "load Unicode file path");
    Require(!KiCadPcbImporter::Load(temporary + wxT(".missing"), document, error) && document.originalSource == source &&
        document.sourcePath == temporary, "missing file leaves loaded board unchanged");
    {
        wxFile file(temporary, wxFile::write);
        Require(file.IsOpened() && file.Write("(broken", 7) == 7 && file.Close(), "write intentionally truncated owned file");
    }
    Require(!KiCadPcbImporter::Load(temporary, document, error) && document.originalSource == source &&
        document.sourcePath == temporary, "malformed file cannot partially replace the previous PCB");
}

inline void Run()
{
    wxLogNull quiet;
    TestSExpression();
    TestOldFormatAndTransforms();
    TestFailureTransactions();
    TestShippedBoardAndFileIO();
}
}
