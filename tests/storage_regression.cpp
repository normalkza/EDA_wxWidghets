#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include "CircuitFile.h"
#include "Canvas.h"
#include "SelectionState.h"

class StorageTestApp : public wxApp
{
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(StorageTestApp);

namespace
{
void Check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string Utf8(const wxString& text)
{
    return std::string(text.utf8_string());
}

// Canvas clipboard regressions must never replace the user's clipboard contents.
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
        return stored->GetDataHere(format, bytes.data()) &&
            data.SetData(format, bytes.size(), bytes.data());
    }
};

// Each run owns a unique temporary directory and never changes user documents.
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        path = wxFileName::CreateTempFileName("wx_demo_storage_regression_");
        Check(!path.empty() && wxRemoveFile(path) && wxFileName::Mkdir(path), "create test directory");
    }
    ~TemporaryDirectory()
    {
        // Remove only this test's known files; never recursively delete a computed path.
        wxLogNull expectedErrors;
        for (const auto& name : { wxString::FromUTF8("中文电路 & 逻辑.xml"), wxString("validation.xml"),
            wxString("invalid.xml"), wxString("protected.xml"), wxString::FromUTF8("画布往返.xml") })
        {
            const auto file = File(name);
            if (wxFileExists(file)) wxRemoveFile(file);
        }
        wxFileName::Rmdir(path);
    }
    wxString File(const wxString& name) const { return wxFileName(path + wxFILE_SEP_PATH + name).GetFullPath(); }
    wxString path;
};

std::string ReadBytes(const wxString& path)
{
    wxFile file(path);
    Check(file.IsOpened(), "open file for byte comparison");
    const auto size = file.Length();
    Check(size >= 0 && size < 16 * 1024 * 1024, "reasonable test file size");
    std::string bytes(static_cast<size_t>(size), '\0');
    Check(file.Read(bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()), "read whole test file");
    return bytes;
}

void WriteBytes(const wxString& path, const std::string& bytes)
{
    wxFile file(path, wxFile::write);
    Check(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size() && file.Close(),
        "write invalid XML fixture");
}

void SameDocument(const CircuitDocument& actual, const CircuitDocument& expected, const std::string& context)
{
    Check(actual.components.size() == expected.components.size(), context + ": component count");
    Check(actual.wires.size() == expected.wires.size(), context + ": wire count");
    Check(actual.texts.size() == expected.texts.size(), context + ": text count");
    for (size_t i = 0; i < actual.components.size(); ++i)
    {
        const auto& a = actual.components[i];
        const auto& e = expected.components[i];
        Check(a.id == e.id && a.type == e.type && a.x == e.x && a.y == e.y &&
            a.inputCount == e.inputCount && a.high == e.high, context + ": component " + std::to_string(i));
    }
    for (size_t i = 0; i < actual.wires.size(); ++i)
    {
        const auto& a = actual.wires[i];
        const auto& e = expected.wires[i];
        Check(a.sourceId == e.sourceId && a.sourcePin == e.sourcePin &&
            a.targetId == e.targetId && a.targetPin == e.targetPin, context + ": wire " + std::to_string(i));
    }
    for (size_t i = 0; i < actual.texts.size(); ++i)
    {
        const auto& a = actual.texts[i];
        const auto& e = expected.texts[i];
        Check(a.rect == e.rect && a.pointSize == e.pointSize && a.text == e.text,
            context + ": text " + std::to_string(i));
    }
}

CircuitDocument FullDocument()
{
    CircuitDocument document;
    document.components.push_back({ 1, "INPUT", -240, 40, 0, true });
    document.components.push_back({ 2, "INPUT", 60, 140, 0, false });
    int id = 3;
    for (const std::string type : { "AND", "OR", "NAND", "NOR", "XOR" })
    {
        for (const int count : { 2, 16 })
        {
            document.components.push_back({ id, type, id * 100, id * 40, count, false });
            document.wires.push_back({ 1, 0, id, 0 });
            document.wires.push_back({ 2, 0, id, count - 1 });
            ++id;
        }
    }
    document.components.push_back({ 13, "NOT", 500, 440, 1, false });
    document.components.push_back({ 14, "OUTPUT", 680, 440, 1, false });
    document.components.push_back({ 15, "OUTPUT", 680, 520, 1, false });
    document.wires.push_back({ 3, 0, 13, 0 });
    document.wires.push_back({ 13, 0, 14, 0 });
    document.wires.push_back({ 1, 0, 15, 0 });
    document.texts.push_back({ wxRect(-30, 80, 460, 160), 24,
        wxString::FromUTF8("  中文注释：电路 <A & B> \"高电平\" '低电平'\n第二行\n\n尾行  ") });
    document.texts.push_back({ wxRect(20, 620, 240, 100), 14, "" });
    document.texts.push_back({ wxRect(280, 620, 220, 100), 6, " \t\n " });
    return document;
}

void TestRoundTrips(const TemporaryDirectory& directory)
{
    const auto document = FullDocument();
    wxString error;
    Check(CircuitFile::Validate(document, error), "valid document: " + Utf8(error));
    const auto path = directory.File(wxString::FromUTF8("中文电路 & 逻辑.xml"));
    Check(CircuitFile::Save(path, document, error), "save UTF-8 document: " + Utf8(error));
    wxXmlDocument xml;
    Check(xml.Load(path) && xml.GetRoot() && xml.GetRoot()->GetName() == "circuit",
        "saved document parses as XML");
    const auto bytes = ReadBytes(path);
    Check(bytes.find("中文注释") != std::string::npos && bytes.find("&amp;") != std::string::npos &&
        bytes.find("&lt;") != std::string::npos, "UTF-8 characters and XML escaping");
    CircuitDocument loaded;
    Check(CircuitFile::Load(path, loaded, error), "load saved document: " + Utf8(error));
    SameDocument(loaded, document, "full XML round trip");
    for (const auto& data : loaded.components)
    {
        auto component = CircuitFile::CreateComponent(data);
        Check(component && component->name == data.type && component->inputs.size() == data.inputCount,
            "component factory restores type and input count");
        if (data.type == "INPUT")
            Check((component->outputs[0].value == LogicValue::High) == data.high,
                "component factory restores both input levels");
    }

    CircuitDocument empty;
    Check(CircuitFile::Save(path, empty, error), "overwrite with empty document: " + Utf8(error));
    Check(CircuitFile::Load(path, loaded, error), "load empty document: " + Utf8(error));
    SameDocument(loaded, empty, "empty document replaces all data");

    CircuitDocument newlines;
    newlines.texts.push_back({ wxRect(0, 0, 400, 120), 96,
        wxString::FromUTF8("中文\r\nCRLF\rCR\nLF\tTAB") });
    Check(CircuitFile::Save(path, newlines, error) && CircuitFile::Load(path, loaded, error),
        "save and load line endings: " + Utf8(error));
    SameDocument(loaded, newlines, "CR, LF and tab preservation");
}

void TestValidation(const TemporaryDirectory& directory)
{
    const auto original = FullDocument();
    wxString error;
    const auto path = directory.File("validation.xml");
    Check(CircuitFile::Save(path, original, error), "save reference document");
    const auto originalBytes = ReadBytes(path);
    using Mutation = std::pair<std::string, std::function<void(CircuitDocument&)>>;
    const std::vector<Mutation> mutations{
        { "zero component ID", [](auto& d) { d.components[0].id = 0; } },
        { "duplicate component ID", [](auto& d) { d.components[1].id = d.components[0].id; } },
        { "unknown component", [](auto& d) { d.components[0].type = "RESISTOR"; } },
        { "too few gate inputs", [](auto& d) { d.components[2].inputCount = 1; } },
        { "too many gate inputs", [](auto& d) { d.components[2].inputCount = 17; } },
        { "invalid NOT input count", [](auto& d) { d.components[12].inputCount = 2; } },
        { "invalid input port count", [](auto& d) { d.components[0].inputCount = 1; } },
        { "high level on non-input", [](auto& d) { d.components[2].high = true; } },
        { "dangling source", [](auto& d) { d.wires[0].sourceId = 9000; } },
        { "dangling target", [](auto& d) { d.wires[0].targetId = 9000; } },
        { "source input pin", [](auto& d) { d.wires[0].sourceId = 14; } },
        { "target output pin", [](auto& d) { d.wires[0].targetId = 2; } },
        { "negative source pin", [](auto& d) { d.wires[0].sourcePin = -1; } },
        { "source pin overflow", [](auto& d) { d.wires[0].sourcePin = 1; } },
        { "target pin overflow", [](auto& d) { d.wires[0].targetPin = 2; } },
        { "duplicate wire", [](auto& d) { d.wires.push_back(d.wires[0]); } },
        { "multiple input drivers", [](auto& d) { d.wires.push_back({ 2, 0, 3, 0 }); } },
        { "coordinate limit", [](auto& d) { d.components[0].x = 1000001; } },
        { "empty text width", [](auto& d) { d.texts[0].rect.width = 0; } },
        { "font size limit", [](auto& d) { d.texts[0].pointSize = 97; } },
        { "XML control character", [](auto& d) { d.texts[0].text = wxString::FromUTF8("illegal\x01text"); } }
    };
    for (const auto& mutation : mutations)
    {
        auto invalid = original;
        mutation.second(invalid);
        Check(!CircuitFile::Validate(invalid, error) && !error.empty(), mutation.first + " rejected by validation");
        Check(!CircuitFile::Save(path, invalid, error) && !error.empty(), mutation.first + " rejected by save");
        Check(ReadBytes(path) == originalBytes, mutation.first + " leaves existing file unchanged");
    }
    CircuitDocument cycle;
    cycle.components = { { 1, "NOT", 0, 0, 1, false }, { 2, "NOT", 100, 0, 1, false } };
    cycle.wires = { { 1, 0, 2, 0 }, { 2, 0, 1, 0 } };
    Check(!CircuitFile::Validate(cycle, error) && !error.empty(), "feedback cycle rejected");
    cycle.wires = { { 1, 0, 1, 0 } };
    Check(!CircuitFile::Validate(cycle, error), "self loop rejected");
}

void TestRejectedLoads(const TemporaryDirectory& directory)
{
    const std::string components =
        "<component id=\"1\" type=\"INPUT\" x=\"0\" y=\"0\" inputCount=\"0\" high=\"1\"/>"
        "<component id=\"2\" type=\"INPUT\" x=\"0\" y=\"100\" inputCount=\"0\" high=\"0\"/>"
        "<component id=\"3\" type=\"NOT\" x=\"100\" y=\"0\" inputCount=\"1\" high=\"0\"/>"
        "<component id=\"4\" type=\"OUTPUT\" x=\"200\" y=\"0\" inputCount=\"1\" high=\"0\"/>";
    const std::string wire = "<wire sourceId=\"1\" sourcePin=\"0\" targetId=\"3\" targetPin=\"0\"/>";
    const std::string valid = "<circuit version=\"1\"><components>" + components +
        "</components><wires>" + wire + "</wires><texts/></circuit>";
    const auto replace = [&](const std::string& oldValue, const std::string& newValue) {
        std::string result = valid;
        const auto position = result.find(oldValue);
        Check(position != std::string::npos, "invalid XML fixture replacement exists");
        result.replace(position, oldValue.size(), newValue);
        return result;
    };
    const std::vector<std::pair<std::string, std::string>> rejected{
        { "truncated XML", valid.substr(0, valid.size() / 2) },
        { "unknown version", replace("version=\"1\"", "version=\"999\"") },
        { "missing version", replace(" version=\"1\"", "") },
        { "duplicate ID", replace("id=\"2\"", "id=\"1\"") },
        { "unknown type", replace("type=\"INPUT\"", "type=\"MISSING\"") },
        { "missing component attribute", replace(" inputCount=\"0\"", "") },
        { "invalid Boolean", replace("high=\"1\"", "high=\"yes\"") },
        { "invalid number", replace("x=\"0\"", "x=\"1x\"") },
        { "integer overflow", replace("x=\"0\"", "x=\"999999999999999999999999\"") },
        { "dangling wire", replace("sourceId=\"1\"", "sourceId=\"99\"") },
        { "wire pin overflow", replace("targetPin=\"0\"", "targetPin=\"1\"") },
        { "duplicate wire", replace("</wires>", wire + "</wires>") },
        { "multiple drivers", replace("</wires>",
            "<wire sourceId=\"2\" sourcePin=\"0\" targetId=\"3\" targetPin=\"0\"/></wires>") },
        { "self loop", replace("sourceId=\"1\"", "sourceId=\"3\"") },
        { "missing container", replace("<texts/>", "") },
        { "duplicate container", replace("<texts/>", "<texts/><texts/>") },
        { "unknown element", replace("<texts/>", "<texts/><future/>") },
        { "unknown attribute", replace("version=\"1\"", "version=\"1\" future=\"true\"") },
        { "internal DTD", "<!DOCTYPE circuit [<!ENTITY extra 'value'>]>" + valid },
        { "external DTD", "<!DOCTYPE circuit SYSTEM 'file:///__wx_demo_missing_entity__'>" + valid },
        { "unsupported XML version", "<?xml version='1.1' encoding='UTF-8'?>" + valid },
        { "non-UTF-8 encoding", "<?xml version='1.0' encoding='ISO-8859-1'?>" + valid },
        { "non-UTF-8 encoding with spaces", "<?xml version = '1.0' encoding = 'ISO-8859-1'?>" + valid }
    };
    const auto path = directory.File("invalid.xml");
    wxString error;
    WriteBytes(path, valid);
    CircuitDocument parsed;
    Check(CircuitFile::Load(path, parsed, error), "independently authored XML fixture is valid: " + Utf8(error));
    for (const auto& fixture : rejected)
    {
        WriteBytes(path, fixture.second);
        auto current = FullDocument();
        const auto before = current;
        wxLogNull expectedErrors;
        Check(!CircuitFile::Load(path, current, error) && !error.empty(), fixture.first + " rejected on load");
        SameDocument(current, before, fixture.first + " keeps previous document");
    }
    auto current = FullDocument();
    const auto before = current;
    Check(!CircuitFile::Load(directory.File("missing.xml"), current, error) && !error.empty(),
        "missing file reports failure");
    SameDocument(current, before, "missing file keeps previous document");
}

void TestFailedWrites(const TemporaryDirectory& directory)
{
    auto original = FullDocument();
    wxString error;
    const auto path = directory.File("protected.xml");
    Check(CircuitFile::Save(path, original, error), "save before replacement failure");
    const auto bytes = ReadBytes(path);
    auto changed = original;
    changed.components[0].high = false;
    {
        wxLogNull expectedErrors;
        Check(!CircuitFile::Save(directory.File("missing/subdirectory.xml"), changed, error) && !error.empty(),
            "missing parent reports write failure");
#ifdef __WXMSW__
        // Deny writes/deletion to force an actual replacement failure after serialization.
        struct FileLock
        {
            HANDLE handle;
            ~FileLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        } lock{ CreateFileW(path.wc_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr) };
        Check(lock.handle != INVALID_HANDLE_VALUE, "lock saved file against replacement");
        Check(!CircuitFile::Save(path, changed, error) && !error.empty(), "locked target reports write failure");
#endif
    }
    Check(ReadBytes(path) == bytes, "failed replacement preserves exact original bytes");
    CircuitDocument loaded;
    Check(CircuitFile::Load(path, loaded, error), "original file remains readable after failed replacement");
    SameDocument(loaded, original, "failed replacement preserves original data");
    Check(CircuitFile::Save(path, changed, error) && CircuitFile::Load(path, loaded, error),
        "save succeeds after lock released: " + Utf8(error));
    SameDocument(loaded, changed, "successful atomic replacement");
}
} // namespace

struct CanvasRegressionAccess
{
static CircuitDocument UndoDocument()
{
    CircuitDocument document;
    document.components = {
        { 1, "INPUT", 80, 80, 0, true },
        { 2, "INPUT", 80, 200, 0, false },
        { 3, "AND", 300, 100, 4, false },
        { 4, "OUTPUT", 600, 140, 1, false }
    };
    document.wires = { { 1, 0, 3, 3 }, { 2, 0, 3, 0 }, { 3, 0, 4, 0 } };
    document.texts = { { wxRect(80, 350, 240, 100), 14, "original note" } };
    return document;
}

static void ImportUndoDocument(Canvas& canvas, const CircuitDocument& document)
{
    wxString error;
    Check(canvas.ImportDocument(document, error), "import undo fixture: " + Utf8(error));
    canvas.SetFocusIgnoringChildren();
    g_selectedType.Clear();
    Check(!canvas.IsModified() && !canvas.CanUndo(), "opening a document starts clean with no canvas undo");
}

static void Key(Canvas& canvas, int code, bool control = false, bool shift = false, bool alt = false)
{
    wxKeyEvent event(wxEVT_CHAR_HOOK);
    event.m_keyCode = code;
    event.m_controlDown = control;
    event.m_shiftDown = shift;
    event.m_altDown = alt;
    // Dispatch the bound event, including its real shortcut-routing path.
    event.SetEventObject(&canvas);
    canvas.GetEventHandler()->ProcessEvent(event);
}

static void TestUndoShortcuts(Canvas& canvas)
{
    const auto document = UndoDocument();
    ImportUndoDocument(canvas, document);
    canvas.m_selectedIndex = 2;
    Key(canvas, 'D');
    Key(canvas, 'D', true, true);
    Key(canvas, 'D', true, false, true);
    SameDocument(canvas.ExportDocument(), document, "ordinary D and modified Ctrl+D leave canvas unchanged");
    Check(!canvas.CanUndo(), "ignored shortcuts do not create history");

    Key(canvas, 'D', true);
    const auto deleted = canvas.ExportDocument();
    Check(deleted.components.size() == 3 && deleted.wires.empty() && canvas.IsModified() && canvas.CanUndo(),
        "Ctrl+D deletes the selected gate and every attached wire");
    Key(canvas, 'Z', true);
    SameDocument(canvas.ExportDocument(), document, "Ctrl+Z restores deleted gate, pin count, values, wires and text");
    Check(!canvas.IsModified() && !canvas.CanUndo(), "undoing the only edit restores clean state and exhausts history");
    Check(!canvas.HandleEditCommand(wxID_UNDO), "undo on an empty history reports no change");

    canvas.SelectWire(0);
    Key(canvas, 'D', true);
    Check(canvas.ExportDocument().wires.size() == 2, "Ctrl+D deletes the selected wire");
    Check(canvas.HandleEditCommand(wxID_UNDO), "menu undo restores wire deletion");
    SameDocument(canvas.ExportDocument(), document, "wire undo restores endpoints and signals");
    Check(!canvas.IsModified() && !canvas.CanUndo(), "wire deletion is one undo action");

    canvas.m_selectedIndex = 2;
    canvas.SetGateInputCount(4);
    Check(!canvas.CanUndo(), "unchanged pin count creates no undo entry");
    canvas.SetGateInputCount(2);
    Check(canvas.ExportDocument().components[2].inputCount == 2 &&
        canvas.ExportDocument().wires.size() == 2, "shrinking a gate removes the invalid wire");
    Check(canvas.Undo(), "undo gate pin-count change");
    SameDocument(canvas.ExportDocument(), document, "pin-count undo restores pruned connections");
    Check(!canvas.CanUndo(), "pin-count and wire changes share one undo entry");

    auto levelDocument = document;
    levelDocument.components.erase(levelDocument.components.begin() + 1, levelDocument.components.begin() + 3);
    levelDocument.wires = { { 1, 0, 4, 0 } };
    ImportUndoDocument(canvas, levelDocument);
    Check(canvas.m_components[1].comp->inputs[0].value == LogicValue::High,
        "level fixture propagates a high signal before editing");
    wxMouseEvent down(wxEVT_LEFT_DOWN), up(wxEVT_LEFT_UP);
    down.SetPosition(wxPoint(100, 100));
    up.SetPosition(down.GetPosition());
    canvas.OnLeftDown(down);
    canvas.OnLeftUp(up);
    Check(!canvas.ExportDocument().components[0].high && canvas.m_components[1].comp->inputs[0].value == LogicValue::Low,
        "click toggles the input level and propagates the low signal");
    Check(canvas.Undo(), "undo input level toggle");
    SameDocument(canvas.ExportDocument(), levelDocument, "level undo restores source and downstream circuit");
    Check(canvas.m_components[1].comp->inputs[0].value == LogicValue::High,
        "level undo recomputes the downstream signal");
    Check(!canvas.CanUndo(), "input click creates one undo entry");

    auto disconnected = document;
    disconnected.wires.erase(disconnected.wires.begin());
    ImportUndoDocument(canvas, disconnected);
    g_selectedType = "WIRE";
    canvas.HandleWireClick(canvas.GetPinPosition({ canvas.m_components[0].comp, PinType::Output, 0 }));
    Check(!canvas.CanUndo(), "unfinished wire has no document history");
    canvas.HandleWireClick(canvas.GetPinPosition({ canvas.m_components[2].comp, PinType::Input, 3 }));
    Check(canvas.ExportDocument().wires.size() == 3 && canvas.CanUndo(), "completed wire creates undo history");
    Check(canvas.Undo(), "undo new wire");
    SameDocument(canvas.ExportDocument(), disconnected, "wire creation undo keeps other wires intact");
    Check(!canvas.CanUndo(), "completed wire creates one undo entry");
    g_selectedType.Clear();
}

static void TestUndoTransformsAndClipboard(Canvas& canvas)
{
    const auto document = UndoDocument();
    ImportUndoDocument(canvas, document);
    canvas.m_selectedIndex = 2;
    Check(canvas.CopySelection() && !canvas.CanUndo(), "copy does not create document history");
    Check(canvas.PasteSelection() && canvas.ExportDocument().components.size() == 5, "paste gate using memory clipboard");
    Check(canvas.Undo(), "undo component paste");
    SameDocument(canvas.ExportDocument(), document, "component paste is undone in one step");
    Check(!canvas.CanUndo(), "component paste creates one undo entry");

    canvas.m_selectedTextIndex = 0;
    canvas.m_selectedIndex = -1;
    Check(canvas.CopySelection() && canvas.PasteSelection(), "paste note using memory clipboard");
    canvas.SetFocusIgnoringChildren();
    Check(canvas.ExportDocument().texts.size() == 2 && canvas.Undo(), "undo pasted note");
    SameDocument(canvas.ExportDocument(), document, "note paste restores geometry, font and contents in one step");
    Check(!canvas.CanUndo(), "note paste creates one undo entry");

    canvas.CreateTextBox(wxRect(400, 450, 180, 80));
    canvas.SetFocusIgnoringChildren();
    Check(canvas.ExportDocument().texts.size() == 2 && canvas.Undo(), "undo text-box creation");
    SameDocument(canvas.ExportDocument(), document, "new text-box creation is undone in one step");
    Check(!canvas.CanUndo(), "text-box creation creates one undo entry");

    wxMouseEvent down(wxEVT_LEFT_DOWN), move(wxEVT_MOTION), up(wxEVT_LEFT_UP);
    down.SetPosition(wxPoint(320, 120));
    canvas.OnLeftDown(down);
    move.m_leftDown = true;
    move.SetPosition(wxPoint(360, 160));
    canvas.OnMouseMove(move);
    canvas.MoveDraggedComponent(move.GetPosition(), false);
    canvas.MoveDraggedComponent(wxPoint(390, 190), false);
    up.SetPosition(wxPoint(420, 220));
    canvas.OnLeftUp(up);
    Check(canvas.ExportDocument().components[2].x == 400 && canvas.ExportDocument().components[2].y == 200,
        "multi-frame gate drag reaches the final snapped position");
    Check(canvas.Undo(), "undo multi-frame component drag");
    SameDocument(canvas.ExportDocument(), document, "component drag is undone in one step");
    Check(!canvas.CanUndo(), "component drag has no intermediate-frame undo entries");

    canvas.OnLeftDown(down);
    move.SetPosition(wxPoint(370, 170));
    canvas.OnMouseMove(move);
    canvas.MoveDraggedComponent(move.GetPosition(), false);
    Check(canvas.CanUndo(), "an in-progress component move is available to the menu undo command");
    Check(canvas.HandleEditCommand(wxID_UNDO), "menu undo cancels the currently captured component move");
    SameDocument(canvas.ExportDocument(), document, "undo during a component move restores the original position");
    Check(!canvas.HasCapture() && !canvas.CanUndo(), "undo during movement releases capture and consumes one interaction");

    for (const int handle : { 8, 4 })
    {
        const wxPoint start = handle == 8 ? wxPoint(100, 370) : document.texts[0].rect.GetBottomRight();
        canvas.BeginTextTransform(0, handle, start);
        canvas.UpdateTextTransform(start + wxPoint(20, 20));
        canvas.UpdateTextTransform(start + wxPoint(80, 60));
        canvas.CancelMouseInteraction();
        Check(canvas.ExportDocument().texts[0].rect != document.texts[0].rect, "text transform changes geometry");
        Check(canvas.Undo(), "undo completed text transform");
        SameDocument(canvas.ExportDocument(), document, "text move/resize is undone in one step");
        Check(!canvas.CanUndo(), "text move/resize has no intermediate-frame undo entries");
    }
    canvas.BeginTextTransform(0, 8, wxPoint(100, 370));
    canvas.UpdateTextTransform(wxPoint(160, 430));
    canvas.CancelMouseInteraction(true);
    SameDocument(canvas.ExportDocument(), document, "cancelling a text drag restores its original rectangle");
    Check(!canvas.IsModified() && !canvas.CanUndo(), "cancelled text drag creates no undo entry");

    canvas.BeginTextTransform(0, 4, document.texts[0].rect.GetBottomRight());
    canvas.UpdateTextTransform(document.texts[0].rect.GetBottomRight() + wxPoint(80, 60));
    Check(canvas.CanUndo() && canvas.HandleEditCommand(wxID_UNDO), "menu undo handles a currently captured text resize");
    SameDocument(canvas.ExportDocument(), document, "undo during a text resize restores its original bounds");
    Check(!canvas.HasCapture() && !canvas.CanUndo(), "undo during text resize releases capture without extra history");

    canvas.m_selectedTextIndex = 0;
    canvas.SetTextFontSize(24);
    Check(canvas.ExportDocument().texts[0].pointSize == 24 && canvas.Undo(), "undo text font change");
    SameDocument(canvas.ExportDocument(), document, "font undo restores the original text style");
    Check(!canvas.CanUndo(), "font change is one undo action");
}

static void TestUndoPersistenceBoundaries(Canvas& canvas)
{
    const auto document = UndoDocument();
    ImportUndoDocument(canvas, document);
    canvas.DeleteWire(0);
    canvas.MarkSaved();
    Check(!canvas.IsModified() && canvas.CanUndo(), "save retains undo history and marks its current contents clean");
    Check(canvas.Undo() && canvas.IsModified(), "undo after save marks the restored older contents dirty");
    SameDocument(canvas.ExportDocument(), document, "undo after save restores the pre-edit contents");

    for (const bool saveBeforeFailure : { false, true })
    {
        ImportUndoDocument(canvas, document);
        canvas.DeleteWire(0);
        if (saveBeforeFailure) canvas.MarkSaved();
        const auto beforeFailure = canvas.ExportDocument();
        auto invalid = document;
        invalid.components[1].id = invalid.components[0].id;
        wxString error;
        Check(!canvas.ImportDocument(invalid, error) && !error.empty() && canvas.CanUndo(),
            "failed import preserves undo history");
        Check(canvas.IsModified() != saveBeforeFailure, "failed import preserves the current dirty state");
        SameDocument(canvas.ExportDocument(), beforeFailure, "failed import preserves edited contents");
        Check(canvas.Undo(), "undo remains usable after failed import");
        SameDocument(canvas.ExportDocument(), document, "undo after failed import restores the prior contents");
        Check(canvas.IsModified() == saveBeforeFailure, "undo after failed import compares with the saved contents");
    }

    ImportUndoDocument(canvas, document);
    canvas.DeleteWire(0);
    canvas.ClearDocument();
    Check(!canvas.CanUndo() && !canvas.IsModified() && !canvas.Undo(), "new document clears history and saved state");
    Check(canvas.ExportDocument().components.empty() && canvas.ExportDocument().texts.empty(),
        "new document cannot undo into the previous circuit");
    g_selectedType = "NOT";
    wxMouseEvent place(wxEVT_LEFT_DOWN);
    place.SetPosition(wxPoint(200, 100));
    canvas.OnLeftDown(place);
    Check(canvas.CanUndo(), "new-document placement can be undone");
    ImportUndoDocument(canvas, document);
    Check(!canvas.Undo(), "successful open clears previous history");
    SameDocument(canvas.ExportDocument(), document, "open cannot undo into the previous circuit");
}

static void TestNativeTextUndo(Canvas& canvas)
{
    const auto document = UndoDocument();
    ImportUndoDocument(canvas, document);
    canvas.DeleteItem(3, -1);
    Check(canvas.CanUndo() && canvas.ExportDocument().components.size() == 3,
        "prepare an independent canvas action before native text editing");
    auto* editor = canvas.m_textBoxes[0].editor;
    editor->SetFocus();
    Check(canvas.IsEditingText(), "native text editor owns keyboard focus");
    editor->SetInsertionPointEnd();
    editor->WriteText(" added");
    Check(editor->CanUndo() && editor->GetValue() == "original note added", "native edit creates text undo history");
    Key(canvas, 'D', true);
    Check(canvas.ExportDocument().texts.size() == 1 && canvas.ExportDocument().components.size() == 3,
        "Ctrl+D while editing cannot delete a canvas object");
    Check(canvas.HandleEditCommand(wxID_UNDO), "undo menu command reaches native text editor");
    Check(editor->GetValue() == "original note" && canvas.ExportDocument().components.size() == 3,
        "native text undo has priority over canvas object undo");
    canvas.SetFocusIgnoringChildren();
    Check(canvas.Undo(), "canvas undo remains available after native text undo");
    SameDocument(canvas.ExportDocument(), document, "native text undo consumes matching canvas text history");
    Check(!canvas.CanUndo() && !canvas.IsModified(), "native then canvas undo restores the clean document");
}

static void TestUndoTextSessionsAndSaveDuringTransform(Canvas& canvas)
{
    const auto document = UndoDocument();
    ImportUndoDocument(canvas, document);
    canvas.CreateTextBox(wxRect(400, 450, 180, 80));
    auto* editor = canvas.m_textBoxes.back().editor;
    editor->SetFocus();
    Check(canvas.IsEditingText(), "new note starts a native text-editing session");
    editor->WriteText("new note contents");
    canvas.SetFocusIgnoringChildren();
    Key(canvas, 'Z', true);
    Check(canvas.ExportDocument().texts.size() == 2 && canvas.ExportDocument().texts.back().text.empty(),
        "Ctrl+Z after leaving text editing restores contents while preserving the new box");
    Key(canvas, 'Z', true);
    SameDocument(canvas.ExportDocument(), document, "a second Ctrl+Z removes the separately created text box");
    Check(!canvas.CanUndo() && !canvas.IsModified(), "text session and box creation are distinct undo actions");

    ImportUndoDocument(canvas, document);
    canvas.DeleteItem(3, -1);
    const auto deleted = canvas.ExportDocument();
    editor = canvas.m_textBoxes[0].editor;
    editor->SetFocus();
    Check(canvas.IsEditingText() && !editor->CanUndo(), "imported note has no native undo operation");
    canvas.HandleEditCommand(wxID_UNDO);
    SameDocument(canvas.ExportDocument(), deleted, "native undo with no text history never falls back to deleting canvas history");
    canvas.SetFocusIgnoringChildren();
    Check(canvas.Undo(), "unconsumed canvas action remains available after native undo with no text history");
    SameDocument(canvas.ExportDocument(), document, "canvas history remains intact after native empty undo");

    ImportUndoDocument(canvas, document);
    const wxPoint start(100, 370);
    canvas.BeginTextTransform(0, 8, start);
    canvas.UpdateTextTransform(start + wxPoint(80, 60));
    const auto savedWhileMoving = canvas.ExportDocument();
    canvas.MarkSaved();
    Check(!canvas.IsModified(), "saving during a text move records the moved coordinates");
    canvas.CancelMouseInteraction(true);
    SameDocument(canvas.ExportDocument(), document, "Esc restores original text coordinates after an in-progress save");
    Check(canvas.IsModified(), "restoring pre-drag coordinates differs from the coordinates saved during the drag");
    Check(canvas.ExportDocument().texts[0].rect != savedWhileMoving.texts[0].rect,
        "cancelled drag retains a distinct saved baseline");
}

static void TestNativeUndoSessionBoundary(Canvas& canvas)
{
    const auto document = UndoDocument();
    ImportUndoDocument(canvas, document);
    auto* editor = canvas.m_textBoxes[0].editor;
    editor->SetFocus();
    editor->SetInsertionPointEnd();
    editor->WriteText(" A");
    canvas.SetFocusIgnoringChildren();
    canvas.m_selectedTextIndex = 0;
    canvas.SetTextFontSize(24);
    const auto afterFontChange = canvas.ExportDocument();
    Check(afterFontChange.texts[0].text == "original note A" && afterFontChange.texts[0].pointSize == 24,
        "prepare text edits separated by a font property change");

    editor->SetFocus();
    Check(canvas.IsEditingText() && !editor->CanUndo(), "a new editing session starts with fresh native undo history");
    canvas.HandleEditCommand(wxID_UNDO);
    SameDocument(canvas.ExportDocument(), afterFontChange, "native empty undo cannot cross the intervening font change");
    editor->SetInsertionPointEnd();
    editor->WriteText(" B");
    Check(editor->CanUndo() && canvas.HandleEditCommand(wxID_UNDO), "new editing session can undo its own input");
    SameDocument(canvas.ExportDocument(), afterFontChange, "native undo removes only the current session's input");

    canvas.SetFocusIgnoringChildren();
    Check(canvas.Undo(), "canvas undo restores the previous font after the native session ends");
    auto afterTextEdit = document;
    afterTextEdit.texts[0].text += " A";
    SameDocument(canvas.ExportDocument(), afterTextEdit, "native undo leaves the intervening font undo in order");
    Check(canvas.Undo(), "canvas undo restores the earlier text editing session");
    SameDocument(canvas.ExportDocument(), document, "earlier text contents undo after the property change");
    Check(!canvas.CanUndo() && !canvas.IsModified(), "separate text sessions and properties return to the saved document");
}

static void TestPersistence(Canvas& canvas, const TemporaryDirectory& directory)
{
    Check(!canvas.IsModified(), "new canvas starts clean");
    const auto document = FullDocument();
    wxString error;
    Check(canvas.ImportDocument(document, error), "canvas imports all components/wires/text: " + Utf8(error));
    Check(!canvas.IsModified(), "imported document starts clean");
    SameDocument(canvas.ExportDocument(), document, "canvas full document round trip");
    Check(canvas.m_components[13].comp->inputs[0].value == LogicValue::High &&
        canvas.m_components[14].comp->inputs[0].value == LogicValue::High,
        "import recomputes multi-stage signals and fanout");
    const auto path = directory.File(wxString::FromUTF8("画布往返.xml"));
    Check(CircuitFile::Save(path, canvas.ExportDocument(), error), "save canvas snapshot");
    canvas.ClearDocument();
    Check(!canvas.IsModified() && canvas.ExportDocument().components.empty() &&
        canvas.ExportDocument().texts.empty() && canvas.ExportDocument().wires.empty(), "clear resets document cleanly");
    CircuitDocument reopened;
    Check(CircuitFile::Load(path, reopened, error) && canvas.ImportDocument(reopened, error), "reopen canvas from file");
    SameDocument(canvas.ExportDocument(), document, "canvas file close/open round trip");

    int notifications = 0;
    canvas.SetModifiedCallback([&] { ++notifications; });
    canvas.m_selectedIndex = 2;
    canvas.SetGateInputCount(3);
    Check(canvas.IsModified() && notifications == 1, "editing gate input count marks dirty and notifies once");
    canvas.SetGateInputCount(3);
    Check(notifications == 1, "same gate input count does not notify");
    canvas.MarkSaved();
    Check(!canvas.IsModified() && notifications == 2, "mark saved clears dirty and notifies");
    canvas.MarkSaved();
    Check(notifications == 2, "mark saved twice does not notify");
    canvas.m_textBoxes[0].editor->SetValue(wxString::FromUTF8("编辑后的中文 <>&\n新行"));
    Check(canvas.IsModified() && canvas.ExportDocument().texts[0].text == wxString::FromUTF8("编辑后的中文 <>&\n新行"),
        "native text editing marks dirty and enters snapshot");
    canvas.MarkSaved();
    const auto wireCount = canvas.ExportDocument().wires.size();
    canvas.DeleteWire(0);
    Check(canvas.IsModified() && canvas.ExportDocument().wires.size() + 1 == wireCount, "wire deletion marks dirty");
    const auto beforeFailure = canvas.ExportDocument();
    auto invalid = beforeFailure;
    invalid.components[1].id = invalid.components[0].id;
    Check(!canvas.ImportDocument(invalid, error) && !error.empty() && canvas.IsModified(),
        "failed import retains dirty state");
    SameDocument(canvas.ExportDocument(), beforeFailure, "failed canvas import is transactional");
    canvas.MarkSaved();
    Check(!canvas.ImportDocument(invalid, error) && !canvas.IsModified(), "failed import also retains clean state");
    SameDocument(canvas.ExportDocument(), beforeFailure, "failed clean import keeps objects and text");
    canvas.CreateTextBox(wxRect(600, 650, 200, 80));
    Check(canvas.IsModified() && canvas.ExportDocument().texts.size() == beforeFailure.texts.size() + 1,
        "text creation marks dirty");
    canvas.ClearDocument();
    Check(!canvas.IsModified(), "clear dirty document resets saved state");
    g_selectedType = "NOT";
    wxMouseEvent place(wxEVT_LEFT_DOWN);
    place.SetPosition(wxPoint(200, 100));
    canvas.OnLeftDown(place);
    Check(canvas.IsModified() && canvas.ExportDocument().components.size() == 1,
        "interactive component placement marks dirty");
    canvas.MarkSaved();
    canvas.DeleteItem(0, -1);
    Check(canvas.IsModified() && canvas.ExportDocument().components.empty(), "component deletion marks dirty");
    canvas.SetModifiedCallback({});
    g_selectedType.Clear();
}
};

int main(int argc, char** argv)
{
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    int result = 0;
    try
    {
        TemporaryDirectory directory;
        TestRoundTrips(directory);
        TestValidation(directory);
        TestRejectedLoads(directory);
        TestFailedWrites(directory);
        // A real, offscreen parent permits native focus/undo and mouse-capture
        // checks without showing a test window over the user's application.
        auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Storage regression", wxPoint(-32000, -32000), wxSize(1200, 900));
        MemoryClipboard clipboard;
        auto* canvas = new Canvas(frame.get(), &clipboard);
        canvas->SetSize(1100, 800);
        frame->ShowWithoutActivating();
        CanvasRegressionAccess::TestPersistence(*canvas, directory);
        CanvasRegressionAccess::TestUndoShortcuts(*canvas);
        CanvasRegressionAccess::TestUndoTransformsAndClipboard(*canvas);
        CanvasRegressionAccess::TestUndoPersistenceBoundaries(*canvas);
        CanvasRegressionAccess::TestNativeTextUndo(*canvas);
        CanvasRegressionAccess::TestUndoTextSessionsAndSaveDuringTransform(*canvas);
        CanvasRegressionAccess::TestNativeUndoSessionBoundary(*canvas);
        std::cout << "PASS: XML round trips (8 types, 2/16 inputs, levels, fanout, UTF-8, escaping, whitespace), "
            "invalid XML/network rejection, transactional loads, failed write preservation, canvas reopen, dirty tracking, "
            "Ctrl+D/Ctrl+Z, canvas undo grouping, saved history, failed imports and native text undo\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
