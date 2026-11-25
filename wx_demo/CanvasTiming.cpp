#include "Canvas.h"
#include "ComponentRegistry.h"
#include <wx/choice.h>
#include <algorithm>

std::optional<CircuitComponentData> Canvas::GetSelectedComponentData() const
{
    if (!HasSelectedComponent()) return std::nullopt;
    return ExportDocument().components[static_cast<std::size_t>(m_selectedIndex)];
}

bool Canvas::CanEditTimingParameters() const
{
    if (!HasSelectedComponent()) return false;
    const auto& type = m_components[m_selectedIndex].comp->name;
    return ComponentRegistry::SupportsInitialLevel(type) || type == "PROBE";
}

bool Canvas::ApplyWaveformDocument(const CircuitDocument& document, wxString& error)
{
    error.clear();
    if (!wxIsMainThread())
    {
        error = wxS("请在主界面编辑输入波形。");
        return false;
    }
    if (!CircuitFile::Validate(document, error)) return false;
    const auto before = ExportDocument();
    if (before.components.size() != document.components.size())
    {
        error = wxS("波形编辑期间电路发生了变化，请重新打开编辑器。");
        return false;
    }
    bool changed = before.simulation != document.simulation;
    for (std::size_t i = 0; i < before.components.size() && !changed; ++i)
        if (before.components[i].waveform != document.components[i].waveform) changed = true;
    if (!changed) return true;

    RecordUndo(before);
    m_formatVersion = std::max(m_formatVersion, 3);
    m_simulationConfig = document.simulation;
    for (auto& placed : m_components)
    {
        const auto& type = placed.comp->name;
        if (type != "INPUT" && type != "BUTTON") continue;
        auto found = std::find_if(document.components.begin(), document.components.end(),
            [&](const auto& data) { return data.id == placed.id; });
        placed.parameters.waveform = found == document.components.end() ?
            std::vector<WaveformEdge>{} : found->waveform;
    }
    MarkModified();
    Refresh(false);
    return true;
}

bool Canvas::SetSimulationConfig(const SimulationConfig& config, wxString& error)
{
    if (config == m_simulationConfig) { error.clear(); return true; }
    auto document = ExportDocument();
    document.formatVersion = 3;
    document.simulation = config;
    if (!CircuitFile::Validate(document, error)) return false;
    RecordUndo();
    m_formatVersion = 3;
    m_simulationConfig = config;
    MarkModified();
    return true;
}

bool Canvas::EditSelectedTimingParameters()
{
    if (!CanEditTimingParameters()) return false;
    CancelMouseInteraction(true);
    const auto original = *GetSelectedComponentData();
    wxDialog dialog(this, wxID_ANY, wxT("元件参数 · ") + wxString::FromUTF8(original.type),
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* grid = new wxFlexGridSizer(2, 12, 14);
    grid->AddGrowableCol(1);
    auto addText = [&](const wxString& title, const wxString& value) {
        grid->Add(new wxStaticText(&dialog, wxID_ANY, title), 0, wxALIGN_CENTER_VERTICAL);
        auto* input = new wxTextCtrl(&dialog, wxID_ANY, value);
        input->SetMinSize(FromDIP(wxSize(230, -1)));
        grid->Add(input, 1, wxEXPAND);
        return input;
    };
    wxChoice* initial = nullptr;
    if (ComponentRegistry::SupportsInitialLevel(original.type))
    {
        grid->Add(new wxStaticText(&dialog, wxID_ANY,
            original.type == "DFF" ? wxT("初始 Q") : wxT("初始电平")), 0, wxALIGN_CENTER_VERTICAL);
        initial = new wxChoice(&dialog, wxID_ANY);
        initial->Append("0"); initial->Append("1"); initial->SetSelection(original.high ? 1 : 0);
        grid->Add(initial, 1, wxEXPAND);
    }
    wxChoice* edge = nullptr;
    if (original.type == "DFF" && original.inputCount == 3)
    {
        grid->Add(new wxStaticText(&dialog, wxID_ANY, wxT("触发沿")), 0, wxALIGN_CENTER_VERTICAL);
        edge = new wxChoice(&dialog, wxID_ANY);
        edge->Append(wxT("上升沿"));
        edge->Append(wxT("下降沿"));
        edge->SetSelection(original.fallingEdge ? 1 : 0);
        grid->Add(edge, 1, wxEXPAND);
    }
    wxTextCtrl *period = nullptr, *high = nullptr, *start = nullptr, *label = nullptr;
    if (original.type == "CLOCK")
    {
        period = addText(wxT("周期 (ns)"), wxString::Format("%lld", static_cast<long long>(original.clockPeriodNs)));
        high = addText(wxT("高电平时长 (ns)"), wxString::Format("%lld", static_cast<long long>(original.clockHighNs)));
        start = addText(wxT("首次跳变时间 (ns)"), wxString::Format("%lld", static_cast<long long>(original.clockStartNs)));
    }
    if (original.type == "PROBE") label = addText(wxT("波形名称"), wxString::FromUTF8(original.probeLabel));
    root->Add(grid, 1, wxEXPAND | wxALL, FromDIP(20));
    const wxString hint = original.type == "CLOCK" ? wxT("整数纳秒；高、低电平持续时间均至少 1 ns。\n首次跳变可为 0，初值本身不产生边沿。") :
        original.type == "DFF" && original.inputCount == 2 ?
            wxT("理想 D 触发器：上升沿采样 D；Q 保持到下一次上升沿，QN 始终与 Q 互反。") :
        original.type == "DFF" && original.inputCount == 4 ?
            wxT("上升沿采样 D；Q 与 QN 互反。SET / CLR 为低有效异步控制端，悬空视为高电平；同时拉低时 CLR 优先。") :
        original.type == "DFF" ? wxT("旧版 DFF：D / CLK / RESET → Q；RESET 为高有效异步复位。") :
        original.type == "PROBE" ? wxT("连接 IN 引脚后，运行仿真查看该信号的波形。") :
        wxT("这里设置保存的初值；仿真中点击 INPUT / BUTTON 可施加运行激励。");
    root->Add(new wxStaticText(&dialog, wxID_ANY, hint), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(20));
    root->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
    dialog.SetSizerAndFit(root);
    dialog.CentreOnParent();
    dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        CircuitComponentData edited = original;
        edited.high = initial && initial->GetSelection() == 1;
        edited.fallingEdge = edge && edge->GetSelection() == 1;
        long long p = 0, h = 0, s = 0;
        if (period && (!period->GetValue().ToLongLong(&p) || !high->GetValue().ToLongLong(&h) ||
            !start->GetValue().ToLongLong(&s)))
        {
            wxMessageBox(wxT("时间必须是整数纳秒。"), wxT("参数无效"), wxOK | wxICON_WARNING, &dialog);
            return;
        }
        if (period) { edited.clockPeriodNs = p; edited.clockHighNs = h; edited.clockStartNs = s; }
        if (label) edited.probeLabel = label->GetValue().ToStdString(wxConvUTF8);
        auto candidate = ExportDocument();
        candidate.formatVersion = 3;
        candidate.components[static_cast<std::size_t>(m_selectedIndex)] = edited;
        wxString error;
        if (!CircuitFile::Validate(candidate, error))
        {
            wxMessageBox(error, wxT("参数无效"), wxOK | wxICON_WARNING, &dialog);
            return;
        }
        RecordUndo();
        auto& placed = m_components[m_selectedIndex];
        placed.parameters = edited;
        placed.initialHigh = edited.high;
        if (ComponentRegistry::SupportsInitialLevel(edited.type))
            placed.comp->outputs[0].value = edited.high ? LogicValue::High : LogicValue::Low;
        m_formatVersion = 3;
        MarkModified();
        RecomputeCircuit();
        dialog.EndModal(wxID_OK);
    }, wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}
