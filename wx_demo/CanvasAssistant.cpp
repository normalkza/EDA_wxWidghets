#include "Canvas.h"
#include "ComponentRegistry.h"
#include <wx/base64.h>
#include <wx/mstream.h>
#include <wx/xml/xml.h>
#include <algorithm>

namespace
{
wxXmlNode* Element(wxXmlNode* parent, const wxString& name)
{
    auto* node = new wxXmlNode(wxXML_ELEMENT_NODE, name); parent->AddChild(node); return node;
}
void Number(wxXmlNode& node, const wxString& name, long long value)
{
    node.AddAttribute(name, wxString::Format("%lld", value));
}
void Box(wxXmlNode& node, const wxRect& rect)
{
    Number(node, "x", rect.x); Number(node, "y", rect.y);
    Number(node, "width", rect.width); Number(node, "height", rect.height);
}
wxString ComponentRef(int id) { return wxString::Format("c%d", id); }
}

bool Canvas::CaptureAssistantContext(AiWorkspaceContext& context, wxString& error, bool includeImage)
{
    context.sceneXml.clear(); context.imageBase64.clear(); error.clear();
    const auto size = GetClientSize();
    if (size.x <= 0 || size.y <= 0 || size.x > 16384 || size.y > 16384)
    { error = wxS("当前绘图区不可用，请显示主工作区后重试。"); return false; }
    const auto document = ExportDocument();
    wxXmlDocument scene;
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "workspace"); scene.SetRoot(root);
    Number(*root, "width", size.x); Number(*root, "height", size.y);
    Number(*root, "revision", GetDocumentRevision()); Number(*root, "grid", m_gridSize);
    root->AddAttribute("selectedComponent", m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(document.components.size()) ?
        ComponentRef(document.components[m_selectedIndex].id) : wxString());
    root->AddAttribute("selectedWire", m_selectedWireIndex >= 0 ? wxString::Format("w%d", m_selectedWireIndex) : wxString());
    root->AddAttribute("selectedText", m_selectedTextIndex >= 0 ? wxString::Format("t%d", m_selectedTextIndex) : wxString());
    auto* capabilities = Element(root, "capabilities");
    for (const auto& definition : ComponentRegistry::All())
    {
        if (!definition.IsAvailable()) continue;
        auto* type = Element(capabilities, "type");
        type->AddAttribute("name", wxString::FromUTF8(definition.type));
        Number(*type, "minInputs", definition.minInputCount); Number(*type, "maxInputs", definition.maxInputCount);
        const auto model = ComponentRegistry::Create(definition.type);
        wxString inputs, outputs;
        for (const auto& pin : model->inputs) { if (!inputs.empty()) inputs += " "; inputs += wxString::FromUTF8(pin.name); }
        for (const auto& pin : model->outputs) { if (!outputs.empty()) outputs += " "; outputs += wxString::FromUTF8(pin.name); }
        type->AddAttribute("inputs", inputs); type->AddAttribute("outputs", outputs);
    }
    auto* components = Element(root, "components");
    for (std::size_t index = 0; index < document.components.size(); ++index)
    {
        const auto& data = document.components[index]; const auto& placed = m_components[index];
        auto* node = Element(components, "component");
        node->AddAttribute("ref", ComponentRef(data.id)); node->AddAttribute("type", wxString::FromUTF8(data.type));
        Number(*node, "x", data.x); Number(*node, "y", data.y);
        Number(*node, "inputCount", data.inputCount); Number(*node, "rotation", data.rotation);
        node->AddAttribute("high", data.high ? "true" : "false");
        Number(*node, "clockPeriodNs", data.clockPeriodNs); Number(*node, "clockHighNs", data.clockHighNs);
        Number(*node, "clockStartNs", data.clockStartNs); node->AddAttribute("fallingEdge", data.fallingEdge ? "true" : "false");
        node->AddAttribute("probeLabel", wxString::FromUTF8(data.probeLabel));
        node->AddAttribute("label", placed.tutorialLabel);
        auto* bounds = Element(node, "bounds"); Box(*bounds, GetComponentRect(placed));
        for (bool output : {false, true})
        {
            const auto& pins = output ? placed.comp->outputs : placed.comp->inputs;
            for (std::size_t pinIndex = 0; pinIndex < pins.size(); ++pinIndex)
            {
                auto* pin = Element(node, "pin");
                pin->AddAttribute("name", wxString::FromUTF8(pins[pinIndex].name));
                pin->AddAttribute("direction", output ? "output" : "input");
                pin->AddAttribute("value", pins[pinIndex].value == LogicValue::High ? "1" : "0");
                const auto position = GetPinPosition({placed.comp, output ? PinType::Output : PinType::Input, pinIndex});
                Number(*pin, "x", position.x); Number(*pin, "y", position.y);
            }
        }
    }
    auto* wires = Element(root, "wires");
    const auto& connections = m_circuit.GetConnections();
    for (std::size_t index = 0; index < document.wires.size(); ++index)
    {
        const auto& data = document.wires[index]; const auto& connection = connections[index];
        auto* node = Element(wires, "wire");
        node->AddAttribute("ref", wxString::Format("w%zu", index));
        node->AddAttribute("from", ComponentRef(data.sourceId)); node->AddAttribute("to", ComponentRef(data.targetId));
        node->AddAttribute("outPin", wxString::FromUTF8(connection.output.component->outputs[data.sourcePin].name));
        node->AddAttribute("inPin", wxString::FromUTF8(connection.input.component->inputs[data.targetPin].name));
        node->AddAttribute("manual", data.route.empty() ? "false" : "true");
        for (const auto point : GetWirePath(connection))
        {
            auto* vertex = Element(node, "point");
            Number(*vertex, "x", point.x); Number(*vertex, "y", point.y);
        }
    }
    auto* texts = Element(root, "texts");
    for (std::size_t index = 0; index < document.texts.size(); ++index)
    {
        const auto& data = document.texts[index];
        auto* node = Element(texts, "text");
        node->AddAttribute("ref", wxString::Format("t%zu", index)); Box(*node, data.rect);
        Number(*node, "pointSize", data.pointSize); node->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "text", data.text));
    }
    wxMemoryOutputStream xml;
    if (!scene.Save(xml, -1) || xml.GetSize() > 1536 * 1024)
    { error = wxS("画布数据过大，暂时无法发送给 AI 助手。"); return false; }
    std::string bytes(xml.GetSize(), '\0'); xml.CopyTo(bytes.data(), bytes.size());
    context.sceneXml = wxString::FromUTF8(bytes.data(), bytes.size());
    if (!includeImage) return true;
    const double scale = std::min(1.0, 1400.0 / std::max(size.x, size.y));
    wxBitmap bitmap(std::max(1, static_cast<int>(size.x * scale)), std::max(1, static_cast<int>(size.y * scale)));
    if (!bitmap.IsOk()) { error = wxS("无法生成画布图像。"); return false; }
    {
        wxMemoryDC dc(bitmap); dc.SetUserScale(scale, scale);
        DrawCanvas(dc, wxRect(wxPoint(), size));
    }
    wxMemoryOutputStream png;
    if (!bitmap.ConvertToImage().SaveFile(png, wxBITMAP_TYPE_PNG) || png.GetSize() > 512 * 1024)
    { error = wxS("画布图像过大，请关闭“附带画布图像”后重试。"); return false; }
    bytes.assign(png.GetSize(), '\0'); png.CopyTo(bytes.data(), bytes.size());
    context.imageBase64 = wxBase64Encode(bytes.data(), bytes.size());
    return true;
}
