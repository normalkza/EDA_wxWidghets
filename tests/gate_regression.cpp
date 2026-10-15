#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/clipbrd.h>
#include <array>
#include <functional>
#include <memory>
#include <algorithm>
#include <utility>
#include <iostream>
#include <stdexcept>
#include <optional>
#include "Component.h"
#include "Canvas.h"
#include "PropertyPanel.h"
#include "AndGate.h"
#include "OrGate.h"
#include "NotGate.h"
#include "NandGate.h"
#include "NorGate.h"
#include "XorGate.h"
#include "SelectionState.h"
#include "IOComponent.h"

class TestApp : public wxApp
{
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(TestApp);

void Check(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

std::unique_ptr<Component> Gate(const std::string& type, int count)
{
    if (type == "AND") return std::make_unique<AndGate>(count);
    if (type == "OR") return std::make_unique<OrGate>(count);
    if (type == "NAND") return std::make_unique<NandGate>(count);
    if (type == "NOR") return std::make_unique<NorGate>(count);
    if (type == "XOR") return std::make_unique<XorGate>(count);
    return std::make_unique<NotGate>();
}

// 使用内存剪贴板验证复制粘贴，不读写用户的系统剪贴板。
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

void TestLogic()
{
    for (const auto& type : { "AND", "OR", "NAND", "NOR", "XOR" })
    {
        for (int count : { 2, 3, 5, 16 })
        {
            auto gate = Gate(type, count);
            const unsigned combinations = 1u << count;
            for (unsigned mask = 0; mask < combinations; ++mask)
            {
                int high = 0;
                for (int i = 0; i < count; ++i)
                {
                    const bool bit = (mask & (1u << i)) != 0;
                    gate->inputs[i].value = bit ? LogicValue::High : LogicValue::Low;
                    high += bit;
                }
                const std::string name(type);
                const bool expected = name == "AND" ? high == count : name == "OR" ? high > 0 :
                    name == "NAND" ? high != count : name == "NOR" ? high == 0 : high % 2 == 1;
                gate->Evaluate();
                Check((gate->outputs[0].value == LogicValue::High) == expected, name + " truth table");
            }
            gate->inputs[0].value = LogicValue::High;
            Check(gate->SetInputCount(2), "shrink inputs");
            Check(gate->SetInputCount(16), "grow inputs");
            Check(gate->inputs[0].value == LogicValue::High, "keep existing signal");
            Check(gate->inputs[15].value == LogicValue::Low, "new pin defaults low");
            Check(!gate->SetInputCount(1) && !gate->SetInputCount(17), "reject invalid input count");
            Check(gate->inputs.size() == 16, "invalid count keeps component intact");
        }
    }
    NotGate single;
    Check(!single.SupportsVariableInputs() && !single.SetInputCount(5) && single.inputs.size() == 1,
        "NOT always has one input");
}

void TestCircuit()
{
    for (const auto& type : { "AND", "OR", "NOT", "NAND", "NOR", "XOR" })
    {
        for (int count : { 2, 3, 5, 16 })
        {
            auto gate = Gate(type, count);
            const int pins = static_cast<int>(gate->inputs.size());
            Circuit circuit;
            OutputComponent result, branch;
            NotGate inverse;
            std::vector<std::unique_ptr<InputComponent>> sources;
            // 故意将终点放在节点列表前面，验证计算不依赖元件放置顺序。
            std::vector<Component*> nodes{ &result, &branch, &inverse, gate.get() };
            for (int i = 0; i < pins; ++i)
            {
                sources.push_back(std::make_unique<InputComponent>());
                nodes.push_back(sources.back().get());
                Check(circuit.Connect({ gate.get(), PinType::Input, static_cast<size_t>(i) },
                    { sources.back().get(), PinType::Output, 0 }) == ConnectionResult::Added, "reverse connection order");
            }
            Check(circuit.Connect({ gate.get(), PinType::Output, 0 }, { &result, PinType::Input, 0 }) ==
                ConnectionResult::Added, "connect gate output");
            Check(circuit.Connect({ gate.get(), PinType::Output, 0 }, { &inverse, PinType::Input, 0 }) ==
                ConnectionResult::Added, "output fanout");
            Check(circuit.Connect({ &inverse, PinType::Output, 0 }, { &branch, PinType::Input, 0 }) ==
                ConnectionResult::Added, "multi stage propagation");
            for (unsigned mask : { 0u, 1u, (1u << pins) - 1, 1u << (pins - 1), 0x5555u & ((1u << pins) - 1) })
            {
                int high = 0;
                for (int i = 0; i < pins; ++i)
                {
                    const bool bit = (mask & (1u << i)) != 0;
                    sources[i]->outputs[0].value = bit ? LogicValue::High : LogicValue::Low;
                    high += bit;
                }
                const std::string name(type);
                const bool expected = name == "AND" ? high == pins : name == "OR" ? high > 0 :
                    name == "NOT" ? high == 0 : name == "NAND" ? high != pins : name == "NOR" ? high == 0 : high % 2 == 1;
                Check(circuit.Evaluate(nodes), "acyclic evaluation");
                Check((result.inputs[0].value == LogicValue::High) == expected, name + " network result");
                Check((branch.inputs[0].value == LogicValue::High) != expected, "branch downstream inversion");
                for (int i = 0; i < pins; ++i)
                    Check(gate->inputs[i].value == sources[i]->outputs[0].value, "every pin receives value");
            }
            Check(circuit.Connect({ gate.get(), PinType::Output, 0 }, { &result, PinType::Input, 0 }) ==
                ConnectionResult::Duplicate, "duplicate connection ignored");
            Check(circuit.Connect({ &inverse, PinType::Output, 0 }, { &result, PinType::Input, 0 }) ==
                ConnectionResult::InputOccupied, "reject two drivers");
            Check(circuit.Connect({ gate.get(), PinType::Output, 0 }, { &inverse, PinType::Output, 0 }) ==
                ConnectionResult::SameDirection, "reject output to output");
            Check(circuit.Connect({ gate.get(), PinType::Input, 100 }, { &inverse, PinType::Output, 0 }) ==
                ConnectionResult::InvalidPin, "reject missing pin");
            circuit.RemoveComponent(gate.get());
            Check(circuit.Evaluate(nodes) && result.inputs[0].value == LogicValue::Low, "disconnect clears stale output value");
        }
    }
    Circuit loop;
    NotGate a, b, c;
    Check(loop.Connect({ &a, PinType::Output, 0 }, { &b, PinType::Input, 0 }) == ConnectionResult::Added, "loop setup");
    Check(loop.Connect({ &b, PinType::Output, 0 }, { &a, PinType::Input, 0 }) == ConnectionResult::Cycle, "reject feedback");
    Check(loop.Connect({ &c, PinType::Output, 0 }, { &c, PinType::Input, 0 }) == ConnectionResult::Cycle, "reject self loop");
    Circuit resize;
    InputComponent source;
    AndGate gate(5);
    Check(resize.Connect({ &source, PinType::Output, 0 }, { &gate, PinType::Input, 4 }) == ConnectionResult::Added,
        "connect last configurable input");
    gate.SetInputCount(2);
    resize.PruneInvalidConnections();
    Check(resize.GetConnections().empty(), "removed pin clears attached wire");
}

struct CanvasRegressionAccess
{
static wxBitmap Render(Canvas& canvas)
{
    const wxSize size = canvas.GetClientSize();
    wxBitmap bitmap(size.x, size.y);
    wxMemoryDC dc(bitmap);
    canvas.DrawCanvas(dc, wxRect(wxPoint(0, 0), size));
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}

static void Compare(const wxBitmap& actual, const wxBitmap& expected, const std::string& message)
{
    const wxImage a = actual.ConvertToImage(), b = expected.ConvertToImage();
    Check(a.GetSize() == b.GetSize(), message + " size");
    const size_t bytes = static_cast<size_t>(a.GetWidth()) * a.GetHeight() * 3;
    Check(std::equal(a.GetData(), a.GetData() + bytes, b.GetData()), message);
}

static void RepaintRegion(Canvas& canvas, wxBitmap& bitmap, wxRect dirty)
{
    dirty.Inflate(3);
    wxMemoryDC dc(bitmap);
    wxDCClipper clip(dc, dirty);
    canvas.DrawCanvas(dc, dirty);
}

static void TestCanvas(Canvas& canvas)
{
    for (const auto& type : { "AND", "OR", "NOT", "NAND", "NOR", "XOR" })
    {
        for (int count : { 2, 5, 16 })
        {
            auto gate = Gate(type, count);
            struct BorrowedCleanup
            {
                Canvas& canvas;
                ~BorrowedCleanup() { canvas.m_components.clear(); canvas.m_selectedIndex = canvas.m_pressedIndex = -1; }
            } cleanup{ canvas };
            canvas.m_components = { { gate.get(), 200, 140 } };
            canvas.m_selectedIndex = 0;
            const auto& pc = canvas.m_components[0];
            const wxRect bounds = canvas.GetComponentRect(pc);
            const auto geometry = canvas.GetGateGeometry(*gate);
            Check(geometry.inputs.size() == gate->inputs.size(), "rendered input count");
            Check(bounds.Contains(pc.x + geometry.outputX, pc.y + geometry.height / 2), "output hit range");
            for (const auto& pin : geometry.inputs)
            {
                Check(pin.x % 20 == 0 && pin.y % 20 == 0, "pin grid alignment");
                Check(canvas.HitTest(pc.x + pin.x, pc.y + pin.y) == 0, "all input pins selectable");
            }
            Check(canvas.HitTest(pc.x + 30, pc.y + geometry.height - 1) == 0, "bottom of gate selectable");
            wxBitmap isolated(800, 700);
            {
                wxMemoryDC dc(isolated);
                dc.SetBackground(*wxWHITE_BRUSH);
                dc.Clear();
                canvas.DrawGate(dc, pc);
            }
            const wxImage image = isolated.ConvertToImage();
            int bluePins = 0;
            for (int y = 0; y < image.GetHeight(); ++y)
            {
                bool blueOnRow = false;
                for (int x = 0; x < image.GetWidth(); ++x)
                {
                    const bool ink = image.GetRed(x, y) != 255 || image.GetGreen(x, y) != 255 ||
                        image.GetBlue(x, y) != 255;
                    Check(!ink || bounds.Contains(x, y), std::string(type) + " ink outside hit/refresh range");
                    if (image.GetBlue(x, y) > 150 && image.GetBlue(x, y) > image.GetRed(x, y) + 60 && image.GetBlue(x, y) > image.GetGreen(x, y) + 60)
                        blueOnRow = true;
                }
                // 每个蓝点的中心行相隔至少一格，只在已知引脚中心行计数。
                if (blueOnRow && std::any_of(geometry.inputs.begin(), geometry.inputs.end(),
                    [&](const wxPoint& p) { return pc.y + p.y == y; })) ++bluePins;
            }
            Check(bluePins == gate->inputs.size(), "actual blue pin count");
            auto previous = Render(canvas);
            canvas.m_pressedIndex = 0;
            canvas.m_dragOffset = wxPoint(0, 0);
            canvas.MoveDraggedComponent(wxPoint(350, 220), false);
            RepaintRegion(canvas, previous, bounds.Union(canvas.GetComponentRect(canvas.m_components[0])));
            Compare(previous, Render(canvas), std::string(type) + " drag leaves stale pixels");
            if (gate->SupportsVariableInputs())
            {
                previous = Render(canvas);
                const wxRect old = canvas.GetComponentRect(canvas.m_components[0]);
                canvas.SetGateInputCount(count == 16 ? 2 : 16);
                RepaintRegion(canvas, previous, old.Union(canvas.GetComponentRect(canvas.m_components[0])));
                Compare(previous, Render(canvas), std::string(type) + " resize leaves stale pixels");
            }
            Check(canvas.CopySelection() && canvas.PasteSelection(), "parameterized copy/paste");
            Component* copy = canvas.m_components.back().comp;
            Check(copy->inputs.size() == gate->inputs.size() && copy->outputs.size() == gate->outputs.size(),
                "copy preserves pin counts");
            for (size_t i = 0; i < gate->inputs.size(); ++i)
                Check(copy->inputs[i].value == gate->inputs[i].value, "copy preserves signals");
            delete copy;
            canvas.m_components.clear();
            canvas.m_selectedIndex = canvas.m_pressedIndex = -1;
        }
    }
}

static void TestPorts(Canvas& canvas)
{
    struct Cleanup
    {
        Canvas& canvas;
        ~Cleanup()
        {
            canvas.m_circuit = Circuit{};
            for (const auto& pc : canvas.m_components) delete pc.comp;
            canvas.m_components.clear();
            canvas.m_pendingWire.reset();
            canvas.m_selectedIndex = canvas.m_pressedIndex = canvas.m_selectedWireIndex = -1;
            g_selectedType.Clear();
        }
    } cleanup{ canvas };
    auto* input = new InputComponent();
    auto* output = new OutputComponent();
    canvas.m_components = { { input, 100, 100 }, { output, 400, 180 } };
    const PinReference source{ input, PinType::Output, 0 }, sink{ output, PinType::Input, 0 };
    Check(canvas.GetPinPosition(source) == wxPoint(140, 120), "input node sits on right box edge");
    Check(canvas.GetPinPosition(sink) == wxPoint(400, 200), "output node sits on left box edge");
    for (const auto& pc : canvas.m_components)
    {
        const PinReference pin = pc.comp == input ? source : sink;
        for (const wxPoint offset : { wxPoint(20, 20), wxPoint(1, 1), wxPoint(39, 1),
            wxPoint(1, 39), wxPoint(39, 39) })
            Check(canvas.HitTestPin(wxPoint(pc.x, pc.y) + offset) == pin, "entire port box connects");
        Check(!canvas.HitTestPin(wxPoint(pc.x + 60, pc.y + 20)), "old protruding port endpoint removed");
        Check(canvas.HitTest(pc.x + 60, pc.y + 20) == -1, "old protruding port body hit removed");
        for (const LogicValue value : { LogicValue::Low, LogicValue::High })
        {
            wxBitmap isolated(800, 700);
            {
                wxMemoryDC dc(isolated);
                dc.SetBackground(*wxWHITE_BRUSH);
                dc.Clear();
                if (pc.comp == input) canvas.DrawInput(dc, pc.x, pc.y, value);
                else canvas.DrawOutput(dc, pc.x, pc.y, value);
            }
            const wxImage image = isolated.ConvertToImage();
            const wxRect bounds = canvas.GetComponentRect(pc);
            for (int y = 0; y < image.GetHeight(); ++y)
                for (int x = 0; x < image.GetWidth(); ++x)
                {
                    const bool ink = image.GetRed(x, y) != 255 || image.GetGreen(x, y) != 255 || image.GetBlue(x, y) != 255;
                    Check(!ink || bounds.Contains(x, y), "port ink stays within hit and refresh bounds");
                    if (x > pc.x + 44)
                        Check(!ink, "port renders no protruding lead");
                }
        }
    }
    g_selectedType = "WIRE";
    canvas.HandleWireClick(wxPoint(120, 120));
    Check(canvas.m_pendingWire == source && canvas.m_wirePreview == wxPoint(140, 120),
        "box center starts preview at box edge");
    canvas.HandleWireClick(wxPoint(420, 200));
    Check(!canvas.m_pendingWire && canvas.m_circuit.GetConnections().size() == 1,
        "output box center completes connection");
    input->outputs[0].value = LogicValue::High;
    canvas.RecomputeCircuit();
    Check(output->inputs[0].value == LogicValue::High, "box connection propagates signal");
    g_selectedType.Clear();
    canvas.m_selectedWireIndex = -1;
    for (const int index : { 0, 1 })
    {
        canvas.m_selectedIndex = canvas.m_pressedIndex = index;
        canvas.m_dragOffset = wxPoint();
        const wxRect before = canvas.GetComponentUpdateRect(canvas.m_components[index]);
        auto partial = Render(canvas);
        canvas.MoveDraggedComponent(index == 0 ? wxPoint(160, 260) : wxPoint(520, 340), false);
        RepaintRegion(canvas, partial, before.Union(canvas.GetComponentUpdateRect(canvas.m_components[index])));
        Compare(partial, Render(canvas), "dragging port leaves no box or wire residue");
        const auto path = canvas.GetWirePath(canvas.m_circuit.GetConnections()[0]);
        Check(path.front() == wxPoint(canvas.m_components[0].x + 40, canvas.m_components[0].y + 20) &&
            path.back() == wxPoint(canvas.m_components[1].x, canvas.m_components[1].y + 20),
            "dragged connection stays on both box edges");
    }
}

static void TestWiring(Canvas& canvas)
{
    struct Cleanup
    {
        Canvas& canvas;
        ~Cleanup()
        {
            canvas.m_circuit = Circuit{};
            for (const auto& pc : canvas.m_components) delete pc.comp;
            canvas.m_components.clear();
            canvas.m_pendingWire.reset();
            canvas.m_selectedIndex = canvas.m_pressedIndex = canvas.m_selectedWireIndex = -1;
        }
    } cleanup{ canvas };
    auto* a = new InputComponent();
    auto* b = new InputComponent();
    auto* gate = new AndGate(5);
    auto* output = new OutputComponent();
    canvas.m_components = { { a, 40, 80 }, { b, 40, 200 }, { gate, 300, 120 }, { output, 600, 160 } };
    g_selectedType = "WIRE";
    const auto connect = [&](PinReference first, PinReference second) {
        const wxPoint firstPoint = canvas.GetPinPosition(first), secondPoint = canvas.GetPinPosition(second);
        Check(canvas.HitTestPin(firstPoint) == first && canvas.HitTestPin(secondPoint) == second,
            "pin hit test resolves real endpoints");
        canvas.HandleWireClick(firstPoint);
        Check(canvas.m_pendingWire.has_value(), "wire starts at pin");
        canvas.HandleWireClick(secondPoint);
        Check(!canvas.m_pendingWire, "wire completes at pin");
    };
    connect({ a, PinType::Output, 0 }, { gate, PinType::Input, 4 });
    connect({ b, PinType::Output, 0 }, { gate, PinType::Input, 0 });
    for (size_t i : { 1u, 2u, 3u }) connect({ gate, PinType::Input, i }, { a, PinType::Output, 0 });
    connect({ gate, PinType::Output, 0 }, { output, PinType::Input, 0 });
    Check(canvas.m_circuit.GetConnections().size() == 6, "all configurable input pins connect");
    a->outputs[0].value = b->outputs[0].value = LogicValue::High;
    canvas.RecomputeCircuit();
    Check(output->inputs[0].value == LogicValue::High, "canvas receives propagated result");
    g_selectedType.Clear();
    // 走实际鼠标释放处理，验证点击输入端口触发重算而非只更新其标签。
    canvas.m_pressedIndex = 0;
    canvas.m_togglePending = true;
    canvas.m_pressPosition = wxPoint(60, 100);
    wxMouseEvent release(wxEVT_LEFT_UP);
    release.SetPosition(canvas.m_pressPosition);
    canvas.OnLeftUp(release);
    Check(a->outputs[0].value == LogicValue::Low && output->inputs[0].value == LogicValue::Low,
        "input click updates downstream result");
    const auto sourcePath = canvas.GetWirePath(canvas.m_circuit.GetConnections()[0]);
    Check(canvas.HitTestWire(wxPoint(150, 100)) >= 0, "wire selectable");
    Check(Wire::HitTest(sourcePath, sourcePath[1]), "wire bend selectable");
    for (const auto& connection : canvas.m_circuit.GetConnections())
    {
        const auto path = canvas.GetWirePath(connection);
        Check(path.front() == canvas.GetPinPosition(connection.output) &&
            path.back() == canvas.GetPinPosition(connection.input), "wire follows exact pin positions");
        for (size_t i = 1; i < path.size(); ++i)
            Check(path[i].x == path[i - 1].x || path[i].y == path[i - 1].y, "orthogonal wire path");
    }
    canvas.m_selectedIndex = canvas.m_pressedIndex = 2;
    canvas.m_selectedWireIndex = -1;
    canvas.m_dragOffset = wxPoint(0, 0);
    const wxRect before = canvas.GetComponentUpdateRect(canvas.m_components[2]);
    wxBitmap partial = Render(canvas);
    canvas.MoveDraggedComponent(wxPoint(400, 300), false);
    RepaintRegion(canvas, partial, before.Union(canvas.GetComponentUpdateRect(canvas.m_components[2])));
    Compare(partial, Render(canvas), "dragging wired gate leaves residual wire pixels");
    canvas.SelectWire(1);
    Check(canvas.DeleteSelection(), "Delete removes selected wire");
    Check(canvas.m_circuit.GetConnections().size() == 5 && gate->inputs[0].value == LogicValue::Low,
        "wire deletion disconnects and clears input");
    canvas.m_selectedIndex = 2;
    canvas.SetGateInputCount(2);
    Check(canvas.m_circuit.GetConnections().size() == 2, "pin count reduction cleans deleted connections");
    canvas.DeleteItem(2, -1);
    Check(canvas.m_circuit.GetConnections().empty() && output->inputs[0].value == LogicValue::Low,
        "component deletion removes all attached wires and stale values");
    canvas.m_pendingWire = PinReference{ a, PinType::Output, 0 };
    wxKeyEvent escape(wxEVT_CHAR_HOOK);
    escape.m_keyCode = WXK_ESCAPE;
    canvas.OnKeyDown(escape);
    Check(!canvas.m_pendingWire, "Esc cancels unfinished wire");
}

static void TestProperties(Canvas& canvas, PropertyPanel& properties)
{
    canvas.SetSelectionCallback([&](const Component* component, int x, int y) {
        if (component) properties.ShowComponent(component, x, y);
        else properties.ShowTool(g_selectedType, canvas.GetTextFontSize(), canvas.GetGateInputCount());
    });
    properties.SetGateInputCountCallback([&](int count) { canvas.SetGateInputCount(count); });
    g_selectedType = "AND";
    properties.ShowTool("AND", 14, 2);
    Check(properties.m_gateSettings->IsShown(), "tool parameter visible");
    wxCommandEvent edit(wxEVT_TEXT, properties.m_gateInputCount->GetId());
    edit.SetString("5");
    properties.m_gateInputCount->GetEventHandler()->ProcessEvent(edit);
    Check(canvas.GetGateInputCount() == 5 && properties.m_gateInputCount->GetValue() == 5,
        "editable default parameter and preview");
    auto gate = Gate("AND", 2);
    canvas.m_components = { { gate.get(), 200, 100 } };
    canvas.m_selectedIndex = 0;
    properties.ShowComponent(gate.get(), 200, 100);
    edit.SetString("16");
    properties.m_gateInputCount->GetEventHandler()->ProcessEvent(edit);
    Check(gate->inputs.size() == 16 && properties.m_gateInputCount->GetValue() == 16,
        "selected gate parameter");
    NotGate single;
    properties.ShowComponent(&single, 0, 0);
    Check(!properties.m_gateSettings->IsShown(), "NOT parameter hidden");
    properties.ShowTextBox(wxRect(0, 0, 100, 60), 14);
    Check(properties.m_textSettings->IsShown() && !properties.m_gateSettings->IsShown(),
        "text controls remain independent");
    canvas.m_components.clear();
    canvas.m_selectedIndex = -1;
    canvas.SetSelectionCallback({});
}

};

int main(int argc, char** argv)
{
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    int result = 0;
    try
    {
        TestLogic();
        TestCircuit();
        auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Regression", wxDefaultPosition, wxSize(1200, 800));
        MemoryClipboard clipboard;
        auto* canvas = new Canvas(frame.get(), &clipboard);
        canvas->SetSize(800, 700);
        auto* properties = new PropertyPanel(frame.get());
        CanvasRegressionAccess::TestCanvas(*canvas);
        CanvasRegressionAccess::TestPorts(*canvas);
        CanvasRegressionAccess::TestWiring(*canvas);
        CanvasRegressionAccess::TestProperties(*canvas, *properties);
        std::cout << "PASS: truth tables, pin resizing, hit bounds, drag/resize pixels, copy/paste, properties, port box endpoints and drag pixels, all pin wiring, fanout, propagation, disconnection and cycle rejection\n";
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
