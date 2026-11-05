#include "SimulationPanel.h"
#include "Canvas.h"
#include "WaveformPanel.h"
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/stopwatch.h>
#include <wx/wrapsizer.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <wx/filefn.h>

SimulationPanel::SimulationPanel(wxWindow* parent, Canvas* canvas)
    : wxPanel(parent, wxID_ANY), m_canvas(canvas), m_timer(this)
{
    SetBackgroundColour(wxColour(243, 246, 251));
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* buttons = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto button = [&](const wxString& text, auto action) {
        auto* control = new wxButton(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        control->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
        buttons->Add(control, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
        return control;
    };
    m_run = button(wxT("▶ 运行 / 继续"), [this] { Run(); });
    m_pause = button(wxT("暂停"), [this] { Pause(); });
    m_stop = button(wxT("停止"), [this] { Stop(); });
    m_reset = button(wxT("复位"), [this] { Reset(); });
    m_step = button(wxT("下一事件"), [this] { Step(); });
    m_period = button(wxT("一个周期"), [this] { StepPeriod(); });
    m_csv = button(wxT("导出 CSV"), [this] { Export(false); });
    m_vcd = button(wxT("导出 VCD"), [this] { Export(true); });
    button(wxT("限制设置"), [this] { Configure(); });
    button(wxT("分频示例"), [this] { if (m_exampleCallback) m_exampleCallback(); });
    root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    auto* settings = new wxBoxSizer(wxHORIZONTAL);
    settings->Add(new wxStaticText(this, wxID_ANY, wxT("停止时间 (ns)")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_stopTime = new wxTextCtrl(this, wxID_ANY, "1000", wxDefaultPosition, FromDIP(wxSize(105, -1)), wxTE_PROCESS_ENTER);
    settings->Add(m_stopTime, 0, wxRIGHT, FromDIP(12));
    settings->Add(new wxStaticText(this, wxID_ANY, wxT("周期步进时钟")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
    m_clock = new wxChoice(this, wxID_ANY);
    m_clock->SetMinSize(FromDIP(wxSize(125, -1)));
    settings->Add(m_clock, 0, wxRIGHT, FromDIP(12));
    m_state = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    settings->Add(m_state, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(settings, 0, wxEXPAND | wxALL, FromDIP(8));
    m_waveform = new WaveformPanel(this);
    root->Add(m_waveform, 1, wxEXPAND);
    m_log = new wxTextCtrl(this, wxID_ANY, wxT("连接 CLOCK → DFF.CLK，Q → NOT → D 可构成二分频；RESET 悬空默认为 0。"),
        wxDefaultPosition, FromDIP(wxSize(-1, 38)), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_SIMPLE);
    root->Add(m_log, 0, wxEXPAND | wxALL, FromDIP(4));
    SetSizer(root);
    Bind(wxEVT_TIMER, &SimulationPanel::OnTimer, this, m_timer.GetId());
    m_stopTime->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { ApplyStopTime(); });
    UpdateControls();
}

void SimulationPanel::InvalidateDocument()
{
    if (m_simulator.IsCompiled() && m_simulator.GetRevision() == m_canvas->GetDocumentRevision()) return;
    m_timer.Stop();
    m_periodTarget.reset();
    m_active = m_running = false;
    m_simulator = DigitalSimulator{};
    m_waveform->Clear();
    m_clockIds.clear(); m_clock->Clear();
    m_stopTime->ChangeValue(wxString::Format("%lld", static_cast<long long>(m_canvas->ExportDocument().simulation.stopTimeNs)));
    m_log->ChangeValue(wxT("设计已改变，旧波形已清除。运行或步进将编译新的独立仿真快照。"));
    UpdateControls();
}

bool SimulationPanel::ApplyStopTime()
{
    long long value = 0;
    if (!m_stopTime->GetValue().ToLongLong(&value) || value < 0)
    {
        ShowError(wxT("停止时间必须是非负整数纳秒。"));
        return false;
    }
    auto config = m_canvas->ExportDocument().simulation;
    config.stopTimeNs = value;
    wxString error;
    if (!m_canvas->SetSimulationConfig(config, error)) { ShowError(error); return false; }
    return true;
}

bool SimulationPanel::Prepare()
{
    if (!ApplyStopTime()) return false;
    if (m_active && m_simulator.GetRevision() == m_canvas->GetDocumentRevision()) return true;
    wxString error;
    if (!m_simulator.Compile(m_canvas->ExportDocument(), m_canvas->GetDocumentRevision(), error))
    { ShowError(error); return false; }
    m_active = true;
    m_clockIds = m_simulator.GetClockIds();
    m_clock->Clear();
    for (int id : m_clockIds) m_clock->Append(wxString::Format("CLOCK #%d", id));
    if (!m_clockIds.empty()) m_clock->SetSelection(0);
    wxString warnings;
    const auto design = m_canvas->ExportDocument();
    for (const auto& component : design.components)
        for (int pin = 0; pin < component.inputCount; ++pin)
        {
            if (component.type == "DFF" && pin == 2) continue;
            const bool connected = std::any_of(design.wires.begin(), design.wires.end(), [&](const auto& wire) {
                return wire.targetId == component.id && wire.targetPin == pin;
            });
            if (!connected)
                warnings += wxString::Format(wxT("警告：%s #%d 输入 %d 悬空，按 0 处理。\n"),
                    wxString::FromUTF8(component.type), component.id, pin);
        }
    m_log->ChangeValue(warnings.empty() ? wxString(wxT("快照已编译；二态零延迟仿真。同刻外部激励批量传播后采样；DFF 统一提交。")) : warnings);
    RefreshResult();
    return true;
}

void SimulationPanel::Run()
{
    if (!Prepare()) return;
    if (m_simulator.IsFinished()) { UpdateControls(); return; }
    m_periodTarget.reset();
    m_running = true;
    m_timer.Start(33);
    UpdateControls();
}

void SimulationPanel::Pause()
{
    m_running = false; m_periodTarget.reset(); m_timer.Stop(); UpdateControls();
}

void SimulationPanel::Stop()
{
    Pause(); m_active = false;
    m_canvas->EvaluateCircuit();
    m_log->AppendText(wxT("\n已停止，画布恢复设计初值；已记录波形仍可导出。"));
    UpdateControls();
}

void SimulationPanel::Reset()
{
    Pause();
    m_active = false;
    if (Prepare()) m_log->AppendText(wxT("\n已复位到 t=0 初始基线，运行激励及历史波形已清空。"));
}

void SimulationPanel::Step()
{
    Pause();
    if (!Prepare()) return;
    wxString error;
    if (!m_simulator.Step(error)) ShowError(error);
    RefreshResult();
}

void SimulationPanel::StepPeriod()
{
    Pause();
    if (!Prepare()) return;
    const int selection = m_clock->GetSelection();
    if (selection < 0 || static_cast<std::size_t>(selection) >= m_clockIds.size())
    { ShowError(wxT("电路中没有可选 CLOCK。请放置时钟后运行。")); return; }
    const auto period = m_simulator.GetClockPeriod(m_clockIds[static_cast<std::size_t>(selection)]);
    if (!period) { ShowError(wxT("所选 CLOCK 已失效，请复位后重试。")); return; }
    const auto remaining = m_simulator.GetConfig().stopTimeNs - m_simulator.GetTime();
    m_periodTarget = m_simulator.GetTime() + std::min(*period, remaining);
    m_running = true;
    // Small periods finish immediately; long periods share the run controller's
    // bounded batches so other clocks cannot freeze pause or editing actions.
    Pump();
    if (m_running) m_timer.Start(33);
}

bool SimulationPanel::ToggleInput(int componentId)
{
    if (!m_active || m_simulator.GetRevision() != m_canvas->GetDocumentRevision()) return false;
    const auto value = m_simulator.GetValue(componentId, PinType::Output, 0);
    if (!value) return false;
    wxString error;
    if (!m_simulator.SetInput(componentId, *value == LogicValue::High ? LogicValue::Low : LogicValue::High, error)) ShowError(error);
    RefreshResult();
    return true;
}

void SimulationPanel::OnTimer(wxTimerEvent&)
{
    Pump();
}

void SimulationPanel::Pump()
{
    if (!m_running) return;
    if (m_simulator.GetRevision() != m_canvas->GetDocumentRevision()) { InvalidateDocument(); return; }
    wxStopWatch budget;
    wxString error;
    for (int count = 0; count < 256 && budget.Time() < 12 && !m_simulator.IsFinished(); ++count)
    {
        const auto next = m_simulator.GetNextEventTime();
        if (m_periodTarget && (!next || *next > *m_periodTarget))
        {
            if (!m_simulator.AdvanceTo(*m_periodTarget, error)) ShowError(error);
            Pause();
            break;
        }
        if (!m_simulator.Step(error)) { ShowError(error); break; }
    }
    if (m_simulator.IsFinished()) Pause();
    RefreshResult();
}

void SimulationPanel::RefreshResult()
{
    if (m_simulator.IsCompiled())
    {
        m_canvas->ApplySimulationResult(m_simulator.GetResult());
        m_waveform->SetTraces(m_simulator.GetTraces(), m_simulator.GetRecordedUntil());
    }
    UpdateControls();
}

void SimulationPanel::UpdateControls()
{
    const wxString state = m_running ? (m_periodTarget ? wxT("周期步进中") : wxT("运行中")) : !m_active ? wxT("就绪") :
        m_simulator.HasFailed() ? wxT("已中止") : m_simulator.IsFinished() ? wxT("已完成") : wxT("已暂停");
    m_state->SetLabel(wxString::Format(wxT("%s · t=%lld ns · %llu 事件"), state,
        static_cast<long long>(m_simulator.GetTime()), static_cast<unsigned long long>(m_simulator.GetEventCount())));
    m_pause->Enable(m_running);
    m_run->Enable(!m_running);
    m_stop->Enable(m_active);
    m_reset->Enable(!m_running);
    m_step->Enable(!m_running); m_period->Enable(!m_running);
    const bool hasTraces = !m_simulator.GetTraces().empty();
    m_csv->Enable(hasTraces); m_vcd->Enable(hasTraces);
    m_stopTime->Enable(!m_running); m_clock->Enable(!m_running);
}

void SimulationPanel::ShowError(const wxString& error)
{
    Pause();
    m_log->AppendText(wxT("\n") + (error.empty() ? wxString(wxT("仿真已停止。")) : error));
}

void SimulationPanel::Export(bool vcd)
{
    Pause();
    if (!m_simulator.IsCompiled() || m_simulator.GetRevision() != m_canvas->GetDocumentRevision())
    { InvalidateDocument(); return; }
    wxFileDialog dialog(this, wxT("导出数字波形"), wxEmptyString, vcd ? "waveform.vcd" : "waveform.csv",
        vcd ? wxT("VCD 波形 (*.vcd)|*.vcd") : wxT("CSV 变化记录 (*.csv)|*.csv"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    wxString path = dialog.GetPath();
    if (!wxFileName(path).HasExt()) path += vcd ? ".vcd" : ".csv";
    // Write to a sibling file so a failed export cannot destroy an older trace.
    const auto temporary = wxFileName::CreateTempFileName(path + ".tmp");
    if (temporary.empty()) { ShowError(wxT("无法创建波形临时文件。")); return; }
    std::ofstream output(std::filesystem::path(temporary.ToStdWstring()), std::ios::binary | std::ios::trunc);
    wxString error;
    bool ok = output && (vcd ? m_simulator.ExportVcd(output, error) : m_simulator.ExportCsv(output, error));
    output.flush(); ok = ok && static_cast<bool>(output); output.close();
    ok = ok && static_cast<bool>(output);
    if (ok) ok = wxRenameFile(temporary, path, true);
    if (!ok)
    {
        wxRemoveFile(temporary);
        ShowError(error.empty() ? wxString(wxT("波形写入失败。")) : error);
        return;
    }
    m_log->AppendText(wxString::Format(wxT("\n已导出有效记录 0…%lld ns："),
        static_cast<long long>(m_simulator.GetRecordedUntil())) + path);
}

void SimulationPanel::Configure()
{
    Pause();
    wxDialog dialog(this, wxID_ANY, wxT("数字仿真限制"));
    auto config = m_canvas->ExportDocument().simulation;
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* grid = new wxFlexGridSizer(2, 10, 12);
    std::vector<wxTextCtrl*> inputs;
    const std::vector<std::pair<wxString, std::size_t>> rows{
        {wxT("最大事件数"), config.maxEvents}, {wxT("单时刻最大 delta"), config.maxDeltaCycles},
        {wxT("最大波形样本总数"), config.maxTraceSamples}};
    for (const auto& [name, value] : rows)
    {
        grid->Add(new wxStaticText(&dialog, wxID_ANY, name), 0, wxALIGN_CENTER_VERTICAL);
        auto* input = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%llu", static_cast<unsigned long long>(value)));
        grid->Add(input, 1, wxEXPAND); inputs.push_back(input);
    }
    root->Add(grid, 1, wxEXPAND | wxALL, FromDIP(20));
    root->Add(new wxStaticText(&dialog, wxID_ANY, wxT("达到上限将中止并保留完整有效记录，不淘汰旧样本。")),
        0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(20));
    root->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
    dialog.SetSizerAndFit(root); dialog.CentreOnParent();
    dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        unsigned long long values[3]{};
        for (std::size_t i = 0; i < inputs.size(); ++i)
            if (!inputs[i]->GetValue().ToULongLong(&values[i]) || values[i] > 10000000)
            { wxMessageBox(wxT("请输入范围内的正整数。"), wxT("参数无效"), wxOK | wxICON_WARNING, &dialog); return; }
        config.maxEvents = static_cast<std::size_t>(values[0]);
        config.maxDeltaCycles = static_cast<std::size_t>(values[1]);
        config.maxTraceSamples = static_cast<std::size_t>(values[2]);
        wxString error;
        if (!m_canvas->SetSimulationConfig(config, error))
        { wxMessageBox(error, wxT("参数无效"), wxOK | wxICON_WARNING, &dialog); return; }
        dialog.EndModal(wxID_OK);
    }, wxID_OK);
    dialog.ShowModal();
}
