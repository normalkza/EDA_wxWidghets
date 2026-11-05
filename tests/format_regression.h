#pragma once

#include "CircuitFile.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace FormatRegression
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(std::string("document format: ") + message);
}

class TemporaryFile
{
public:
    TemporaryFile()
        : path(wxFileName::CreateTempFileName(wxS("wx_demo_format_regression_")))
    {
        Require(!path.empty(), "create isolated temporary file");
    }
    ~TemporaryFile()
    {
        if (!path.empty()) wxRemoveFile(path);
    }
    wxString path;
};

inline std::string Read(const wxString& path)
{
    wxFile file(path, wxFile::read);
    Require(file.IsOpened(), "read saved file");
    const auto size = file.Length();
    Require(size >= 0 && size < 8 * 1024 * 1024, "bounded fixture file");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    Require(file.Read(bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()), "read complete fixture");
    return bytes;
}

inline void Write(const wxString& path, const std::string& bytes)
{
    wxFile file(path, wxFile::write);
    Require(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size() && file.Close(),
        "write independent XML fixture");
}

inline void Equal(const CircuitDocument& actual, const CircuitDocument& expected)
{
    Require(actual.formatVersion == expected.formatVersion, "preserve format version");
    Require(actual.simulation == expected.simulation, "preserve simulation configuration");
    Require(actual.components.size() == expected.components.size() &&
        actual.wires.size() == expected.wires.size() && actual.texts.size() == expected.texts.size(),
        "preserve document object counts");
    for (std::size_t index = 0; index < actual.components.size(); ++index)
    {
        const auto& a = actual.components[index];
        const auto& b = expected.components[index];
        Require(a.id == b.id && a.type == b.type && a.x == b.x && a.y == b.y &&
            a.inputCount == b.inputCount && a.high == b.high && a.rotation == b.rotation &&
            a.clockPeriodNs == b.clockPeriodNs && a.clockHighNs == b.clockHighNs &&
            a.clockStartNs == b.clockStartNs && a.probeLabel == b.probeLabel,
            "preserve component values");
    }
    for (std::size_t index = 0; index < actual.wires.size(); ++index)
    {
        const auto& a = actual.wires[index];
        const auto& b = expected.wires[index];
        Require(a.sourceId == b.sourceId && a.sourcePin == b.sourcePin &&
            a.targetId == b.targetId && a.targetPin == b.targetPin && a.route == b.route,
            "preserve resolved wire references and route");
    }
    for (std::size_t index = 0; index < actual.texts.size(); ++index)
    {
        const auto& a = actual.texts[index];
        const auto& b = expected.texts[index];
        Require(a.rect == b.rect && a.pointSize == b.pointSize && a.text == b.text,
            "preserve Unicode annotation and layout");
    }
}

inline CircuitDocument Design(int version)
{
    CircuitDocument document;
    document.formatVersion = version;
    document.components = {{1, "INPUT", 0, 20, 0, true}, {2, "INPUT", 0, 160, 0, false},
        {3, "AND", 220, 80, 3, false, 90}, {4, "NOT", 440, 80, 1, false},
        {5, "OUTPUT", 660, 80, 1, false}};
    document.wires = {{1, 0, 3, 0, {{40, 40}, {100, 40}, {100, 100}, {220, 100}}},
        {2, 0, 3, 2}, {3, 0, 4, 0}, {4, 0, 5, 0}};
    document.texts = {{wxRect(-20, 280, 500, 90), 16,
        wxString::FromUTF8("版本与命名引脚 <A & C>\n保持文字、角度与路径")}};
    return document;
}

inline std::string Fixture(const std::string& version, const std::string& wires)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<circuit version=\"" + version +
        "\"><components>"
        "<component id=\"7\" type=\"INPUT\" x=\"0\" y=\"0\" inputCount=\"0\" high=\"1\"/>"
        "<component id=\"5\" type=\"AND\" x=\"200\" y=\"0\" inputCount=\"3\" high=\"0\"/>"
        "<component id=\"9\" type=\"OUTPUT\" x=\"400\" y=\"0\" inputCount=\"1\" high=\"0\"/>"
        "</components><wires>" + wires + "</wires><texts/></circuit>";
}

inline CircuitDocument RouteBudgetDesign()
{
    CircuitDocument document;
    document.formatVersion = 2;
    document.components.push_back({1, "INPUT", 0, 0, 0, true});
    // Sixteen 250-point paths exercise the existing 4000-point total limit.
    for (int wire = 0; wire < 16; ++wire)
    {
        std::vector<wxPoint> route{{40, 20}};
        for (int segment = 1; segment < 250; ++segment)
        {
            auto point = route.back();
            if (segment % 2) point.x += 20;
            else point.y += 20;
            route.push_back(point);
        }
        route.back().x += wire * 20;
        const auto end = route.back();
        document.components.push_back({wire + 2, "OUTPUT", end.x, end.y - 20, 1, false});
        document.wires.push_back({1, 0, wire + 2, 0, std::move(route)});
    }
    return document;
}

inline void Run()
{
    TemporaryFile file;
    wxString error;
    for (const int version : {1, 2, 3})
    {
        const auto expected = Design(version);
        Require(CircuitFile::Save(file.path, expected, error), "save supported version");
        const auto bytes = Read(file.path);
        Require(bytes.find("version=\"" + std::to_string(version) + "\"") != std::string::npos,
            "writer retains requested format version");
        if (version == 1)
        {
            Require(bytes.find("sourcePin=\"0\"") != std::string::npos &&
                bytes.find("sourcePinId=") == std::string::npos, "v1 retains numeric pin schema");
        }
        else
        {
            Require(bytes.find("sourcePinId=\"OUT\"") != std::string::npos &&
                bytes.find("targetPinId=\"C\"") != std::string::npos &&
                bytes.find("targetPinId=\"IN\"") != std::string::npos,
                "v2 writes actual source and target pin IDs");
            Require(bytes.find("sourcePin=") == std::string::npos && bytes.find("targetPin=") == std::string::npos,
                "v2 never mixes numeric pin attributes");
        }
        CircuitDocument loaded;
        Require(CircuitFile::Load(file.path, loaded, error), "load supported version");
        Equal(loaded, expected);
        Require(CircuitFile::Save(file.path, loaded, error), "resave loaded document");
        CircuitDocument reopened;
        Require(CircuitFile::Load(file.path, reopened, error), "reopen resaved document");
        Equal(reopened, expected);
    }

    const std::string firstWire = "<wire sourceId=\"7\" sourcePinId=\"OUT\" targetId=\"5\" targetPinId=\"C\"/>";
    const std::string secondWire = "<wire sourceId=\"5\" sourcePinId=\"OUT\" targetId=\"9\" targetPinId=\"IN\"/>";
    Write(file.path, Fixture("2", firstWire + secondWire));
    CircuitDocument independent;
    Require(CircuitFile::Load(file.path, independent, error), "read independently authored v2 XML");
    Require(independent.formatVersion == 2 && independent.wires.size() == 2 &&
        independent.wires[0].targetPin == 2 && independent.wires[1].targetPin == 0,
        "resolve named pins to runtime indices without relying on XML order");

    const std::vector<std::string> invalidXml{
        Fixture("999", firstWire),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"MISSING\" targetId=\"5\" targetPinId=\"C\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"OUT\" targetId=\"5\" targetPinId=\"D\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"IN\" targetId=\"5\" targetPinId=\"C\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"OUT\" targetId=\"5\" targetPinId=\"\"/>"),
        Fixture("2", "<wire sourceId=\"7\" targetId=\"5\" targetPinId=\"C\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"OUT\" targetId=\"5\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"OUT\" sourcePin=\"0\" targetId=\"5\" targetPinId=\"C\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePinId=\"OUT\" targetId=\"5\" targetPinId=\"C\" targetPin=\"2\"/>"),
        Fixture("2", "<wire sourceId=\"7\" sourcePin=\"0\" targetId=\"5\" targetPin=\"2\"/>"),
        Fixture("1", firstWire),
        Fixture("2", "<wire sourceId=\"999\" sourcePinId=\"OUT\" targetId=\"5\" targetPinId=\"C\"/>"),
        Fixture("2", firstWire + "<wire sourceId=\"5\" sourcePinId=\"OUT\" targetId=\"9\" targetPinId=\"NO\"/>")};
    for (const auto& bytes : invalidXml)
    {
        auto destination = Design(1);
        const auto before = destination;
        Write(file.path, bytes);
        Require(!CircuitFile::Load(file.path, destination, error) && !error.empty(), "reject unsupported version or pin schema");
        Equal(destination, before);
    }

    // -1 is a UI factory default sentinel, never a valid persisted pin count.
    for (const int version : {1, 2})
    {
        auto invalidCount = Design(version);
        invalidCount.components[2].inputCount = -1;
        Require(!CircuitFile::CreateComponent(invalidCount.components[2]), "file factory rejects UI input-count sentinel");
        Require(!CircuitFile::Validate(invalidCount, error), "in-memory validation rejects negative input count");
        const auto validBytes = Fixture(std::to_string(version), "");
        auto negativeCountXml = validBytes;
        const auto countPosition = negativeCountXml.find("inputCount=\"3\"");
        Require(countPosition != std::string::npos, "negative count fixture targets AND definition");
        negativeCountXml.replace(countPosition, std::string("inputCount=\"3\"").size(), "inputCount=\"-1\"");
        Write(file.path, negativeCountXml);
        auto preserved = Design(1);
        const auto before = preserved;
        Require(!CircuitFile::Load(file.path, preserved, error), "reader rejects negative input count without using defaults");
        Equal(preserved, before);
    }

    const auto routes = RouteBudgetDesign();
    Require(CircuitFile::Save(file.path, routes, error), "save v2 at route-point budget");
    CircuitDocument routeCopy;
    Require(CircuitFile::Load(file.path, routeCopy, error), "load v2 at route-point budget");
    Equal(routeCopy, routes);
    auto invalidVersion = routes;
    invalidVersion.formatVersion = 999;
    const auto savedBytes = Read(file.path);
    Require(!CircuitFile::Save(file.path, invalidVersion, error), "reject unsupported version on save");
    Require(Read(file.path) == savedBytes, "invalid save preserves existing bytes");

    CircuitDocument timing;
    timing.formatVersion = 3;
    timing.components = {{1, "CLOCK", 0, 0, 0, false}, {2, "DFF", 200, 0, 3, true},
        {3, "NOT", 400, 0, 1, false}, {4, "PROBE", 600, 0, 1, false},
        {5, "CONSTANT", 0, 160, 0, false}, {6, "BUTTON", 0, 240, 0, true},
        {7, "LED", 600, 160, 1, false}};
    timing.components[0].clockPeriodNs = 8000000000LL;
    timing.components[0].clockHighNs = 2000000000LL;
    timing.components[0].clockStartNs = 4000000000LL;
    timing.components[3].probeLabel = wxString::FromUTF8("分频输出 Q & 时序").ToStdString(wxConvUTF8);
    timing.wires = {{1, 0, 2, 1}, {2, 0, 3, 0}, {3, 0, 2, 0}, {2, 0, 4, 0}, {5, 0, 2, 2}, {6, 0, 7, 0}};
    timing.simulation.stopTimeNs = 32000000000LL;
    timing.simulation.maxEvents = 700;
    timing.simulation.maxDeltaCycles = 30;
    timing.simulation.maxTraceSamples = 900;
    Require(CircuitFile::Save(file.path, timing, error), "save v3 DFF feedback and 64-bit clock parameters");
    const auto timingBytes = Read(file.path);
    Require(timingBytes.find("sourcePinId=\"Q\"") != std::string::npos &&
        timingBytes.find("targetPinId=\"CLK\"") != std::string::npos &&
        timingBytes.find("clockPeriodNs=\"8000000000\"") != std::string::npos,
        "v3 serializes named DFF pins and integer nanosecond clocks");
    CircuitDocument restoredTiming;
    Require(CircuitFile::Load(file.path, restoredTiming, error), "load complete v3 simulation design");
    Equal(restoredTiming, timing);
    auto invalidAsyncFeedback = timing;
    invalidAsyncFeedback.wires.back() = {3, 0, 2, 2};
    invalidAsyncFeedback.wires.erase(invalidAsyncFeedback.wires.begin() + 4);
    Require(!CircuitFile::Validate(invalidAsyncFeedback, error), "asynchronous reset feedback remains a combinational loop");
    for (const auto* name : {"clockPeriodNs", "clockHighNs", "clockStartNs", "stopTimeNs", "maxEvents", "maxDeltaCycles", "maxTraceSamples"})
    {
        auto corrupt = timingBytes;
        const std::string attribute = std::string(name) + "=\"";
        const auto start = corrupt.find(attribute);
        Require(start != std::string::npos, "v3 numeric fixture attribute exists");
        const auto valueStart = start + attribute.size();
        const auto valueEnd = corrupt.find('"', valueStart);
        corrupt.replace(valueStart, valueEnd - valueStart, "9223372036854775808");
        Write(file.path, corrupt);
        const auto before = restoredTiming;
        Require(!CircuitFile::Load(file.path, restoredTiming, error) && !error.empty(), "v3 numeric overflow rejects without wrapping");
        Equal(restoredTiming, before);
    }
    const auto invalidV3 = [&](const std::string& from, const std::string& to) {
        auto corrupt = timingBytes;
        const auto start = corrupt.find(from);
        Require(start != std::string::npos, "v3 malformed fixture target exists");
        corrupt.replace(start, from.size(), to);
        Write(file.path, corrupt);
        const auto before = restoredTiming;
        Require(!CircuitFile::Load(file.path, restoredTiming, error), "reject invalid v3 timing schema");
        Equal(restoredTiming, before);
    };
    invalidV3("clockHighNs=\"2000000000\"", "clockHighNs=\"8000000000\"");
    invalidV3("clockStartNs=\"4000000000\"", "clockStartNs=\"-1\"");
    invalidV3("maxEvents=\"700\"", "maxEvents=\"0\"");
    invalidV3("stopTimeNs=\"32000000000\"", "stopTimeNs=\"-1\"");
    invalidV3("<simulation ", "<simulation unknown=\"1\" ");
    auto downgrade = timing;
    downgrade.formatVersion = 2;
    Write(file.path, timingBytes);
    Require(!CircuitFile::Save(file.path, downgrade, error), "new features cannot be silently written into legacy format");
    Require(Read(file.path) == timingBytes, "failed downgrade preserves original v3 file");
}
}
