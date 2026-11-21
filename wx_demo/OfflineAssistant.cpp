#include "OfflineAssistant.h"
#include "AiCircuitExecutor.h"
#include "CircuitValidation.h"
#include "ComponentRegistry.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

namespace
{
using Result = OfflineAssistant::Result;
Result Answer(const wxString& text, const wxString& topic = wxString())
{ Result result; result.text = text; result.topic = topic; return result; }
Result Error(const wxString& text)
{ auto result = Answer(text); result.kind = Result::Kind::Error; return result; }
Result Edit(CircuitDocument document, const wxString& text, const wxString& topic)
{
    auto result = Answer(text, topic); result.kind = Result::Kind::Edit;
    result.document = std::move(document); return result;
}
bool Has(const wxString& text, std::initializer_list<const wchar_t*> phrases)
{
    return std::any_of(phrases.begin(), phrases.end(), [&](const auto phrase) { return text.Contains(phrase); });
}
struct Template
{
    wxString name;
    wxString steps;
    AiCircuitAction action;
    std::vector<std::pair<std::string, wxString>> labels;
};
Template CircuitTemplate(const wxString& text)
{
    Template model;
    if (Has(text, {L"全加器", L"full adder", L"fulladder"}))
    {
        model.name = wxS("全加器");
        model.steps = wxS("输入 A、B、CIN，输出 S、COUT。\n1. X = A XOR B。\n2. S = X XOR CIN。\n3. COUT = (A AND B) OR (X AND CIN)。");
        model.action = {{{"a", "INPUT", 0}, {"b", "INPUT", 0}, {"cin", "INPUT", 0},
            {"x", "XOR", 2}, {"sum", "XOR", 2}, {"ab", "AND", 2}, {"cx", "AND", 2},
            {"carry", "OR", 2}, {"s", "OUTPUT", 1}, {"cout", "OUTPUT", 1}},
            {{"a", "OUT", "x", "A"}, {"b", "OUT", "x", "B"}, {"x", "OUT", "sum", "A"}, {"cin", "OUT", "sum", "B"},
             {"a", "OUT", "ab", "A"}, {"b", "OUT", "ab", "B"}, {"x", "OUT", "cx", "A"}, {"cin", "OUT", "cx", "B"},
             {"ab", "OUT", "carry", "A"}, {"cx", "OUT", "carry", "B"}, {"sum", "OUT", "s", "IN"}, {"carry", "OUT", "cout", "IN"}}};
        model.labels = {{"a", "A"}, {"b", "B"}, {"cin", "CIN"}, {"s", "S"}, {"cout", "COUT"}};
    }
    else if (Has(text, {L"半加器", L"half adder", L"halfadder"}))
    {
        model.name = wxS("半加器");
        model.steps = wxS("输入 A、B，输出 S、COUT。\n1. A、B 分别连接异或门和与门的两个输入。\n2. 异或门输出 S = A XOR B。\n3. 与门输出 COUT = A AND B。");
        model.action = {{{"a", "INPUT", 0}, {"b", "INPUT", 0}, {"sum", "XOR", 2}, {"carry", "AND", 2},
            {"s", "OUTPUT", 1}, {"cout", "OUTPUT", 1}},
            {{"a", "OUT", "sum", "A"}, {"b", "OUT", "sum", "B"}, {"a", "OUT", "carry", "A"},
             {"b", "OUT", "carry", "B"}, {"sum", "OUT", "s", "IN"}, {"carry", "OUT", "cout", "IN"}}};
        model.labels = {{"a", "A"}, {"b", "B"}, {"s", "S"}, {"cout", "COUT"}};
    }
    else
    {
        std::string type;
        if (Has(text, {L"与非", L"nand"})) { type = "NAND"; model.name = wxS("双输入与非门"); }
        else if (Has(text, {L"或非", L"nor"})) { type = "NOR"; model.name = wxS("双输入或非门"); }
        else if (Has(text, {L"异或", L"xor"})) { type = "XOR"; model.name = wxS("双输入异或门"); }
        else if (Has(text, {L"非门", L"反相器", L"not gate"}) || text == "not") { type = "NOT"; model.name = wxS("非门"); }
        else if (Has(text, {L"与门", L"and gate"}) || text == "and") { type = "AND"; model.name = wxS("双输入与门"); }
        else if (Has(text, {L"或门", L"or gate"}) || text == "or") { type = "OR"; model.name = wxS("双输入或门"); }
        if (type.empty()) return model;
        model.action.components = {{"a", "INPUT", 0}, {"gate", type, type == "NOT" ? 1 : 2}, {"y", "OUTPUT", 1}};
        model.action.wires = {{"a", "OUT", "gate", type == "NOT" ? "IN" : "A"}, {"gate", "OUT", "y", "IN"}};
        model.labels = {{"a", "A"}, {"y", "Y"}};
        if (type != "NOT")
        {
            model.action.components.insert(model.action.components.begin() + 1, {"b", "INPUT", 0});
            model.action.wires.push_back({"b", "OUT", "gate", "B"}); model.labels.emplace_back("b", "B");
        }
        model.steps = wxS("将输入引脚连接到") + model.name + wxS("的输入，门的 OUT 连接输出引脚 Y 的 IN。切换输入电平查看输出，按 F5 复位仿真。");
    }
    return model;
}

bool PlaceLabel(CircuitDocument& document, const CircuitComponentData& part, const wxString& name,
    const wxSize& available, bool& changed)
{
    const auto body = AiCircuitExecutor::ComponentBounds(part);
    const auto centre = wxPoint(body.x + body.width / 2, body.y + body.height / 2);
    for (const auto& text : document.texts)
    {
        const auto delta = wxPoint(text.rect.x + text.rect.width / 2, text.rect.y + text.rect.height / 2) - centre;
        if (text.text == name && std::int64_t(delta.x) * delta.x + std::int64_t(delta.y) * delta.y < 160 * 160) return true;
    }
    std::vector<wxRect> occupied;
    for (const auto& component : document.components) occupied.push_back(AiCircuitExecutor::ComponentBounds(component));
    for (const auto& text : document.texts) occupied.push_back(text.rect);
    const wxRect viewport(wxPoint(), available);
    const auto free = [&](const wxRect& rect) {
        return viewport.Contains(rect) && std::none_of(occupied.begin(), occupied.end(), [&](const auto& area) { return area.Intersects(rect); });
    };
    wxRect chosen;
    for (const auto point : {wxPoint(part.x - 4, body.y - 28), wxPoint(body.GetRight() + 8, part.y + 4),
                            wxPoint(part.x - 4, body.GetBottom() + 8), wxPoint(body.x - 96, part.y + 4)})
    {
        const wxRect rect(point, wxSize(88, 24));
        if (free(rect)) { chosen = rect; break; }
    }
    if (chosen.IsEmpty()) return false;
    document.texts.push_back({chosen, 12, name}); changed = true; return true;
}

wxString Inspect(const CircuitDocument& current)
{
    std::set<std::pair<int, int>> connected;
    for (const auto& wire : current.wires) connected.emplace(wire.targetId, wire.targetPin);
    std::size_t missing = 0;
    for (const auto& component : current.components)
        for (int pin = 0; pin < component.inputCount; ++pin) if (!connected.contains({component.id, pin})) ++missing;
    return wxString::Format(wxS("当前画布：%zu 个元件、%zu 条连线、%zu 个标注。\n连接结构校验通过；未连接的输入引脚有 %zu 个（默认按 0 处理）。"),
        current.components.size(), current.wires.size(), current.texts.size(), missing);
}
}

wxString OfflineAssistant::Help()
{
    return wxS("离线工具箱使用本地规则和预设模板，不需要 API、网络服务或 Python。\n可输入：新建全加器、新建半加器、新建与门/或门/非门/与非门/或非门/异或门、整理当前布局、标注输入输出、检查当前电路、旋转选中元件、恢复自动走线。\n“给步骤”只解释，“直接完成”修改画布；其它开放式问答请使用 DeepSeek 模式。");
}

OfflineAssistant::Result OfflineAssistant::Plan(const CircuitDocument& current, const wxSize& available,
    const wxString& request, bool apply, int selectedComponentId, const wxString& previousTopic)
{
    wxString text = request; text.Trim().Trim(false); text = text.Lower();
    if (text.empty() || text.length() > 4000 || !CircuitValidation::IsValidXmlText(text)) return Error(wxS("请输入不超过 4000 字的有效需求。"));
    if (text == wxS("直接新建") || text == wxS("直接完成") || text == wxS("照做") || text == wxS("执行"))
    {
        if (previousTopic.empty()) return Answer(wxS("请先选择具体操作，例如“新建全加器”。"));
        text = previousTopic.Lower();
    }
    if (Has(text, {L"不要执行", L"只解释", L"仅说明", L"只需要步骤", L"不改画布", L"explain only", L"do not execute"})) apply = false;
    wxString error;
    if (!CircuitValidation::Validate(current, error)) return Error(error);
    const bool create = Has(text, {L"新建", L"创建", L"生成", L"添加", L"画一个", L"绘制", L"搭建"});
    const bool layout = Has(text, {L"布局", L"排版", L"排列", L"整理电路"});
    const bool labels = Has(text, {L"标注", L"标签"});
    const bool inspect = Has(text, {L"检查", L"分析当前", L"查看当前"});
    const bool rotate = text.Contains(wxS("旋转"));
    const bool route = Has(text, {L"自动走线", L"恢复走线"});
    const auto model = CircuitTemplate(text);
    if (!model.name.empty() && (create || !(layout || labels || inspect || rotate || route)))
    {
        const wxString topic = wxS("新建") + model.name;
        if (!apply || Has(text, {L"不要新建", L"不要创建", L"不要生成", L"别新建", L"别创建"})) return Answer(model.steps, topic);
        if (available.x <= 120 || available.y <= 120) return Error(wxS("当前绘图区太小，请先显示或放大工作区。"));
        const bool replace = Has(text, {L"清空并", L"清空后", L"替换当前电路", L"替换现有电路"}) &&
            !Has(text, {L"不要清空", L"不清空", L"保留现有"});
        CircuitDocument base = replace ? CircuitDocument() : current, candidate;
        if (replace) { base.formatVersion = 3; base.simulation = current.simulation; }
        if (!AiCircuitExecutor::BuildCandidate(base, model.action, candidate, error, wxSize(available.x - 48, available.y - 28))) return Error(error);
        bool changed = false;
        for (const auto& [ref, name] : model.labels)
        {
            const auto found = std::find_if(model.action.components.begin(), model.action.components.end(), [&](const auto& component) { return component.ref == ref; });
            const auto index = base.components.size() + std::distance(model.action.components.begin(), found);
            const auto component = candidate.components[index];
            if (!PlaceLabel(candidate, component, name, available, changed)) return Error(wxS("绘图区没有足够空间放置端口标注，请放大窗口后重试。"));
        }
        if (!CircuitValidation::Validate(candidate, error)) return Error(error);
        return Edit(std::move(candidate), (replace ? wxString(wxS("已替换为")) : wxString(wxS("已追加"))) + model.name + wxS("，并完成接线及端口标注。"), topic);
    }
    if (inspect) return Answer(Inspect(current));
    if (layout)
    {
        const wxString topic = wxS("整理当前布局");
        if (!apply) return Answer(wxS("按信号依赖关系从左向右分层排列元件，保留连接和参数，重算自动走线；邻近元件的标注随之移动。"), topic);
        CircuitDocument candidate;
        if (!AiCircuitExecutor::ArrangeDocument(current, candidate, error, available)) return Error(error);
        bool changed = false;
        for (std::size_t i = 0; i < current.components.size(); ++i)
            changed |= current.components[i].x != candidate.components[i].x || current.components[i].y != candidate.components[i].y;
        for (std::size_t i = 0; i < current.wires.size(); ++i) changed |= current.wires[i].route != candidate.wires[i].route;
        for (std::size_t i = 0; i < current.texts.size(); ++i) changed |= current.texts[i].rect != candidate.texts[i].rect;
        return changed ? Edit(std::move(candidate), wxS("已整理元件布局并恢复自动走线，原有连接和参数保留。"), topic) : Answer(wxS("当前布局已符合本地分层规则，没有更改画布。"), topic);
    }
    if (labels)
    {
        const wxString topic = wxS("标注输入输出");
        if (!apply) return Answer(wxS("为输入、输出端口添加可保存的 IN1、IN2…和 OUT1、OUT2…文本标注；已有邻近标注保留。"), topic);
        if (current.components.size() > 128) return Error(wxS("离线自动标注一次支持最多 128 个元件。"));
        CircuitDocument candidate = current; int input = 0, output = 0; bool changed = false;
        for (const auto& part : current.components)
        {
            if (part.type != "INPUT" && part.type != "OUTPUT") continue;
            const auto body = AiCircuitExecutor::ComponentBounds(part);
            const bool named = std::any_of(current.texts.begin(), current.texts.end(), [&](const auto& label) {
                auto region = body; region.Inflate(45); return label.text.length() <= 16 && region.Intersects(label.rect);
            });
            const auto name = part.type == "INPUT" ? wxString::Format("IN%d", ++input) : wxString::Format("OUT%d", ++output);
            if (!named && !PlaceLabel(candidate, part, name, available, changed)) return Error(wxS("端口附近放不下标注，请整理布局或放大窗口后再试。"));
        }
        if (!CircuitValidation::Validate(candidate, error)) return Error(error);
        return changed ? Edit(std::move(candidate), wxS("已为尚未命名的输入输出添加标注，已有标注保留。"), topic) : Answer(wxS("没有需要新增标注的输入输出端口。"), topic);
    }
    if (rotate)
    {
        const wxString topic = wxS("旋转选中元件");
        if (!apply) return Answer(wxS("先选中元件，离线旋转会顺时针转动 90°，连线端点跟随元件。也可直接按 R。"), topic);
        const auto part = std::find_if(current.components.begin(), current.components.end(), [&](const auto& item) { return item.id == selectedComponentId; });
        if (part == current.components.end()) return Error(wxS("请先在画布中选中要旋转的元件。"));
        AiCircuitPatch patch;
        patch.operations = {{"update_component", "c" + std::to_string(part->id), {{"rotation", wxString::Format("%d", (part->rotation + 90) % 360)}}, {}}};
        CircuitDocument candidate;
        if (!AiCircuitExecutor::BuildPatchCandidate(current, patch, candidate, error, available)) return Error(error);
        return Edit(std::move(candidate), wxS("已将选中元件顺时针旋转 90°。"), topic);
    }
    if (route)
    {
        const wxString topic = wxS("恢复自动走线");
        if (!apply) return Answer(wxS("保留每根线的两个端点，清除手动折点，让画布重新计算直角走线路径。"), topic);
        CircuitDocument candidate = current; bool changed = false;
        for (auto& wire : candidate.wires) { changed |= !wire.route.empty(); wire.route.clear(); }
        return changed ? Edit(std::move(candidate), wxS("已恢复全部导线的自动走线路径。"), topic) : Answer(wxS("当前导线已经使用自动走线。"), topic);
    }
    return Answer(Help());
}
