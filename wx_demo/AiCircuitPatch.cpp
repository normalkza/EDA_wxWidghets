#include "AiCircuitExecutor.h"
#include "CircuitValidation.h"
#include "ComponentRegistry.h"
#include <algorithm>
#include <charconv>
#include <climits>
#include <optional>
#include <set>
#include <unordered_map>

namespace
{
using Fields = std::map<std::string, wxString>;
bool Only(const Fields& fields, std::initializer_list<const char*> names)
{
    for (const auto& [key, value] : fields)
        if (std::none_of(names.begin(), names.end(), [&](const auto name) { return key == name; })) return false;
    return true;
}
template<typename T> bool Integer(const Fields& fields, const char* name, T& value, bool required = false)
{
    const auto found = fields.find(name);
    if (found == fields.end()) return !required;
    const auto text = found->second.ToStdString(wxConvUTF8);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
}
bool Boolean(const Fields& fields, const char* name, bool& value)
{
    const auto found = fields.find(name);
    if (found == fields.end()) return true;
    if (found->second != "true" && found->second != "false") return false;
    value = found->second == "true"; return true;
}
bool Reference(const std::string& ref)
{
    if (ref.empty() || ref.size() > 32 || !((ref[0] >= 'A' && ref[0] <= 'Z') ||
        (ref[0] >= 'a' && ref[0] <= 'z') || ref[0] == '_')) return false;
    return std::all_of(ref.begin(), ref.end(), [](char c) { return
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}
template<typename T> struct Record { std::string ref; std::optional<T> value; };
template<typename T> T* Find(std::vector<Record<T>>& records, const std::string& ref)
{
    for (auto& record : records) if (record.ref == ref && record.value) return &*record.value;
    return nullptr;
}
template<typename T> bool Remove(std::vector<Record<T>>& records, const std::string& ref)
{
    for (auto& record : records) if (record.ref == ref && record.value) { record.value.reset(); return true; }
    return false;
}
bool ComponentFields(CircuitComponentData& component, const Fields& fields, bool adding)
{
    if (!Only(fields, {"type", "x", "y", "inputCount", "rotation", "high", "clockPeriodNs",
            "clockHighNs", "clockStartNs", "probeLabel", "fallingEdge"})) return false;
    if (adding)
    {
        const auto type = fields.find("type"); if (type == fields.end()) return false;
        component.type = type->second.ToStdString(wxConvUTF8);
    }
    else if (fields.contains("type")) return false;
    if (fields.contains("probeLabel")) component.probeLabel = fields.at("probeLabel").ToStdString(wxConvUTF8);
    return Integer(fields, "x", component.x, adding) && Integer(fields, "y", component.y, adding) &&
        Integer(fields, "inputCount", component.inputCount, adding) && Integer(fields, "rotation", component.rotation) &&
        Boolean(fields, "high", component.high) && Boolean(fields, "fallingEdge", component.fallingEdge) &&
        Integer(fields, "clockPeriodNs", component.clockPeriodNs) && Integer(fields, "clockHighNs", component.clockHighNs) &&
        Integer(fields, "clockStartNs", component.clockStartNs);
}
bool TextFields(CircuitTextData& text, const Fields& fields, bool adding)
{
    if (!Only(fields, {"x", "y", "width", "height", "pointSize", "text"})) return false;
    if (adding && !fields.contains("text")) return false;
    if (fields.contains("text")) text.text = fields.at("text");
    return Integer(fields, "x", text.rect.x, adding) && Integer(fields, "y", text.rect.y, adding) &&
        Integer(fields, "width", text.rect.width, adding) && Integer(fields, "height", text.rect.height, adding) &&
        Integer(fields, "pointSize", text.pointSize);
}
int FindNamedPin(const std::vector<Pin>& pins, const wxString& name)
{
    for (std::size_t index = 0; index < pins.size(); ++index)
        if (wxString::FromUTF8(pins[index].name) == name) return static_cast<int>(index);
    return -1;
}
}

bool AiCircuitExecutor::BuildPatchCandidate(const CircuitDocument& original, const AiCircuitPatch& patch,
    CircuitDocument& candidate, wxString& error, const wxSize& available)
{
    error.clear();
    if (patch.operations.empty() || patch.operations.size() > 128)
    { error = wxS("一次修改必须包含 1 至 128 项操作。"); return false; }
    if (!CircuitValidation::Validate(original, error)) return false;
    CircuitDocument staged = original;
    std::unordered_map<std::string, int> refs;
    std::set<std::string> declared;
    std::set<int> ids, touched;
    std::set<std::string> touchedTexts;
    for (const auto& part : staged.components) { refs.emplace("c" + std::to_string(part.id), part.id); ids.insert(part.id); }
    std::vector<Record<CircuitWireData>> wires;
    std::vector<Record<CircuitTextData>> texts;
    for (std::size_t i = 0; i < staged.wires.size(); ++i) wires.push_back({"w" + std::to_string(i), staged.wires[i]});
    for (std::size_t i = 0; i < staged.texts.size(); ++i) texts.push_back({"t" + std::to_string(i), staged.texts[i]});
    auto component = [&](const std::string& ref) -> CircuitComponentData* {
        const auto id = refs.find(ref); if (id == refs.end()) return nullptr;
        for (auto& part : staged.components) if (part.id == id->second) return &part;
        return nullptr;
    };
    int nextId = 1;
    for (std::size_t index = 0; index < patch.operations.size(); ++index)
    {
        const auto& operation = patch.operations[index]; const auto& fields = operation.fields;
        auto fail = [&](const wxString& detail) {
            error = wxString::Format(wxS("第 %zu 项操作（%s %s）未应用："), index + 1,
                wxString::FromUTF8(operation.kind), wxString::FromUTF8(operation.ref)) + detail;
            return false;
        };
        if (!Reference(operation.ref)) return fail(wxS("引用名无效。"));
        if (operation.kind != "route_wire" && !operation.points.empty()) return fail(wxS("此操作不接受路径点。"));
        if (operation.kind == "add_component")
        {
            if (operation.ref[0] != 'n' || refs.contains(operation.ref) || !declared.insert(operation.ref).second)
                return fail(wxS("新增引用名必须以 n 开头且不能重复。"));
            while (ids.contains(nextId) && nextId < INT_MAX) ++nextId;
            if (nextId == INT_MAX) return fail(wxS("无法分配元件 ID。"));
            CircuitComponentData part; part.id = nextId; ids.insert(nextId++);
            if (!ComponentFields(part, fields, true)) return fail(wxS("新增元件字段无效，须指定类型、输入数及 x、y。"));
            refs.emplace(operation.ref, part.id); touched.insert(part.id); staged.components.push_back(std::move(part));
        }
        else if (operation.kind == "update_component")
        {
            auto* part = component(operation.ref);
            if (!part || fields.empty() || !ComponentFields(*part, fields, false)) return fail(wxS("找不到元件，或修改字段无效。"));
            if (fields.contains("x") || fields.contains("y") || fields.contains("rotation") || fields.contains("inputCount")) touched.insert(part->id);
        }
        else if (operation.kind == "delete_component")
        {
            auto* part = component(operation.ref); if (!part || !fields.empty()) return fail(wxS("找不到要删除的元件。"));
            const int id = part->id;
            std::erase_if(staged.components, [&](const auto& item) { return item.id == id; });
            for (auto& wire : wires) if (wire.value && (wire.value->sourceId == id || wire.value->targetId == id)) wire.value.reset();
            refs.erase(operation.ref); touched.erase(id);
        }
        else if (operation.kind == "add_wire")
        {
            if (operation.ref[0] != 'n' || !declared.insert(operation.ref).second ||
                !Only(fields, {"from", "outPin", "to", "inPin"}) || fields.size() != 4)
                return fail(wxS("新增连线须使用唯一 n 引用，并指定 from、outPin、to、inPin。"));
            auto* from = component(fields.at("from").ToStdString(wxConvUTF8));
            auto* to = component(fields.at("to").ToStdString(wxConvUTF8));
            if (!from || !to) return fail(wxS("连线引用了不存在的元件。"));
            auto source = ComponentRegistry::Create(from->type, from->inputCount);
            auto target = ComponentRegistry::Create(to->type, to->inputCount);
            if (!source || !target) return fail(wxS("连线元件类型无效。"));
            const int out = FindNamedPin(source->outputs, fields.at("outPin")), in = FindNamedPin(target->inputs, fields.at("inPin"));
            if (out < 0 || in < 0) return fail(wxS("引脚名不存在，必须使用快照提供的输出、输入名称。"));
            wires.push_back({operation.ref, CircuitWireData{from->id, out, to->id, in, {}}});
        }
        else if (operation.kind == "delete_wire")
        {
            if (!fields.empty() || !Remove(wires, operation.ref)) return fail(wxS("找不到要删除的连线。"));
        }
        else if (operation.kind == "route_wire")
        {
            auto* wire = Find(wires, operation.ref);
            if (!wire || !fields.empty()) return fail(wxS("找不到要调整路径的连线。"));
            if (!CircuitValidation::ValidateRoute(operation.points, error)) return fail(error);
            wire->route = operation.points;
        }
        else if (operation.kind == "add_text")
        {
            CircuitTextData text;
            if (operation.ref[0] != 'n' || !declared.insert(operation.ref).second || !TextFields(text, fields, true))
                return fail(wxS("新增文本须指定唯一 n 引用、内容和矩形范围。"));
            texts.push_back({operation.ref, text}); touchedTexts.insert(operation.ref);
        }
        else if (operation.kind == "update_text")
        {
            auto* text = Find(texts, operation.ref);
            if (!text || fields.empty() || !TextFields(*text, fields, false)) return fail(wxS("找不到文本，或修改字段无效。"));
            touchedTexts.insert(operation.ref);
        }
        else if (operation.kind == "delete_text")
        {
            if (!fields.empty() || !Remove(texts, operation.ref)) return fail(wxS("找不到要删除的文本。"));
        }
        else return fail(wxS("不支持的编辑操作。"));
    }
    staged.wires.clear(); for (const auto& wire : wires) if (wire.value) staged.wires.push_back(*wire.value);
    staged.texts.clear(); for (const auto& text : texts) if (text.value) staged.texts.push_back(*text.value);
    staged.formatVersion = 3;
    if (!CircuitValidation::Validate(staged, error)) return false;
    const wxRect viewport(wxPoint(), available);
    std::vector<wxRect> bounds; bounds.reserve(staged.components.size());
    for (const auto& part : staged.components)
    {
        const auto box = ComponentBounds(part); bounds.push_back(box);
        if (touched.contains(part.id) && available.x > 0 && available.y > 0 && !viewport.Contains(box))
        { error = wxString::Format(wxS("元件 c%d 超出绘图区（%d×%d），请调整坐标或布局。"), part.id, available.x, available.y); return false; }
    }
    for (std::size_t a = 0; a < bounds.size(); ++a)
        for (std::size_t b = a + 1; b < bounds.size(); ++b)
            if ((touched.contains(staged.components[a].id) || touched.contains(staged.components[b].id)) && bounds[a].Intersects(bounds[b]))
            { error = wxString::Format(wxS("元件 c%d 与 c%d 重叠，请留出间距。"), staged.components[a].id, staged.components[b].id); return false; }
    for (const auto& text : texts)
    {
        if (!text.value || !touchedTexts.contains(text.ref)) continue;
        if (available.x > 0 && available.y > 0 && !viewport.Contains(text.value->rect))
        { error = wxS("新增或调整的文本超出当前绘图区。"); return false; }
    }
    candidate = std::move(staged);
    return true;
}
