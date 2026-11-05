#include "PcbFrame.h"
#include "KiCadPcbImporter.h"
#include <wx/checklst.h>
#include <wx/config.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/wrapsizer.h>
#include <chrono>
#include <exception>
#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>

namespace
{
wxString Utf8(const std::string& value) { return wxString::FromUTF8(value); }
wxString Mm(double value) { return wxString::Format("%.4f mm", value); }
wxString Point(PcbPoint point) { return Mm(point.x) + ", " + Mm(point.y); }
wxString NetName(const PcbDocument& document, int id)
{
    const auto found = document.nets.find(id);
    return found == document.nets.end() || found->second.empty() ?
        wxString::Format(wxT("#%d（未命名）"), id) : Utf8(found->second);
}
wxString Joined(const std::vector<std::string>& layers)
{
    wxString result;
    for (const auto& layer : layers) { if (!result.empty()) result += ", "; result += Utf8(layer); }
    return result;
}
}

PcbFrame::PcbFrame(wxWindow* parent, const wxString& path)
    : wxFrame(parent, wxID_ANY, wxT("KiCad PCB 工作区"), wxDefaultPosition, wxSize(1300, 860)), m_timer(this)
{
    SetMinSize(FromDIP(wxSize(850, 600)));
    SetBackgroundColour(wxColour(243, 246, 251));
    SetFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI")));
    CreateStatusBar(2);
    int widths[]{-1, FromDIP(260)};
    GetStatusBar()->SetStatusWidths(2, widths);
    SetStatusText(wxT("滚轮缩放 · 中键/右键拖动平移 · 单击对象查看属性"));
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* tools = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto button = [&](const wxString& label, auto action) {
        auto* control = new wxButton(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        control->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
        tools->Add(control, 0, wxRIGHT | wxBOTTOM, FromDIP(5));
        return control;
    };
    m_importButton = button(wxT("导入 PCB…"), [this] { ChooseBoard(); });
    m_reloadButton = button(wxT("刷新源文件"), [this] { if (m_document) LoadBoard(m_document->sourcePath); });
    m_openButton = button(wxT("在 KiCad 中打开"), [this] { OpenInKiCad(); });
    m_copyButton = button(wxT("原样另存为…"), [this] { SaveCopy(); });
    button(wxT("适配全部"), [this] { m_canvas->Fit(); });
    button(wxT("＋"), [this] { m_canvas->Zoom(1.4); });
    button(wxT("－"), [this] { m_canvas->Zoom(1 / 1.4); });
    button(wxT("清除高亮"), [this] { m_canvas->SetHighlightedNet(std::nullopt); });
    m_autoRefresh = new wxCheckBox(this, wxID_ANY, wxT("KiCad 保存后自动刷新"));
    m_autoRefresh->SetValue(true);
    tools->Add(m_autoRefresh, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM, FromDIP(5));
    root->Add(tools, 0, wxEXPAND | wxALL, FromDIP(10));
    m_summary = new wxStaticText(this, wxID_ANY, wxT("导入 .kicad_pcb 查看板层、封装、焊盘、走线、过孔与网络。源文件保持只读。"));
    root->Add(m_summary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    auto* split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    auto* sidebar = new wxPanel(split);
    auto* side = new wxBoxSizer(wxVERTICAL);
    auto* notebook = new wxNotebook(sidebar, wxID_ANY);
    auto* layerPage = new wxPanel(notebook);
    auto* layerLayout = new wxBoxSizer(wxVERTICAL);
    auto* allLayers = new wxBoxSizer(wxHORIZONTAL);
    auto* show = new wxButton(layerPage, wxID_ANY, wxT("全显"));
    auto* hide = new wxButton(layerPage, wxID_ANY, wxT("全隐"));
    allLayers->Add(show, 1); allLayers->Add(hide, 1, wxLEFT, FromDIP(5));
    layerLayout->Add(allLayers, 0, wxEXPAND | wxALL, FromDIP(6));
    m_layers = new wxCheckListBox(layerPage, wxID_ANY);
    layerLayout->Add(m_layers, 1, wxEXPAND | wxALL, FromDIP(6)); layerPage->SetSizer(layerLayout);
    notebook->AddPage(layerPage, wxT("图层"));
    auto* netPage = new wxPanel(notebook);
    auto* netLayout = new wxBoxSizer(wxVERTICAL);
    m_netSearch = new wxTextCtrl(netPage, wxID_ANY);
    m_netSearch->SetHint(wxT("搜索网络名称 / 编号"));
    netLayout->Add(m_netSearch, 0, wxEXPAND | wxALL, FromDIP(6));
    m_nets = new wxListCtrl(netPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_nets->AppendColumn(wxT("编号"), wxLIST_FORMAT_LEFT, FromDIP(55));
    m_nets->AppendColumn(wxT("网络"), wxLIST_FORMAT_LEFT, FromDIP(185));
    netLayout->Add(m_nets, 1, wxEXPAND | wxALL, FromDIP(6)); netPage->SetSizer(netLayout);
    notebook->AddPage(netPage, wxT("网络"));
    auto* footprintPage = new wxPanel(notebook);
    auto* footprintLayout = new wxBoxSizer(wxVERTICAL);
    m_footprintSearch = new wxTextCtrl(footprintPage, wxID_ANY);
    m_footprintSearch->SetHint(wxT("搜索位号 / 数值 / 封装库"));
    footprintLayout->Add(m_footprintSearch, 0, wxEXPAND | wxALL, FromDIP(6));
    m_footprints = new wxListCtrl(footprintPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_footprints->AppendColumn(wxT("位号"), wxLIST_FORMAT_LEFT, FromDIP(70));
    m_footprints->AppendColumn(wxT("数值 / 型号"), wxLIST_FORMAT_LEFT, FromDIP(180));
    footprintLayout->Add(m_footprints, 1, wxEXPAND | wxALL, FromDIP(6)); footprintPage->SetSizer(footprintLayout);
    notebook->AddPage(footprintPage, wxT("元件"));
    m_report = new wxTextCtrl(notebook, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    notebook->AddPage(m_report, wxT("报告"));
    side->Add(notebook, 1, wxEXPAND);
    side->Add(new wxStaticText(sidebar, wxID_ANY, wxT("对象属性（毫米）")), 0, wxALL, FromDIP(8));
    m_inspector = new wxTextCtrl(sidebar, wxID_ANY,
        wxT("单击焊盘、走线或过孔查看属性与所属网络。\n此工作区用于导入、查看和检查；板图编辑在 KiCad 中完成。"),
        wxDefaultPosition, FromDIP(wxSize(-1, 185)), wxTE_MULTILINE | wxTE_READONLY);
    side->Add(m_inspector, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
    sidebar->SetSizer(side);
    m_canvas = new PcbCanvas(split);
    m_canvas->SetSelectionCallback([this](const PcbSelection& selection) { ShowSelection(selection); });
    m_canvas->SetStatusCallback([this](const wxString& message) { SetStatusText(message, 1); });
    split->SetMinimumPaneSize(FromDIP(200)); split->SetSashGravity(0);
    split->SplitVertically(sidebar, m_canvas, FromDIP(290));
    split->Bind(wxEVT_SPLITTER_DOUBLECLICKED, [](wxSplitterEvent& event) { event.Veto(); });
    root->Add(split, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    SetSizer(root); Layout();
    m_layers->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent& event) {
        const auto index = static_cast<std::size_t>(event.GetInt());
        if (index < m_layerNames.size()) m_canvas->SetLayerVisible(m_layerNames[index], m_layers->IsChecked(static_cast<unsigned int>(index)));
    });
    auto all = [this](bool visible) {
        for (unsigned int index = 0; index < m_layers->GetCount(); ++index) m_layers->Check(index, visible);
        m_canvas->ShowAllLayers(visible);
    };
    show->Bind(wxEVT_BUTTON, [all](wxCommandEvent&) { all(true); });
    hide->Bind(wxEVT_BUTTON, [all](wxCommandEvent&) { all(false); });
    m_netSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { PopulateNets(); });
    m_footprintSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { PopulateFootprints(); });
    m_nets->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) { SelectNet(static_cast<int>(event.GetData())); });
    m_footprints->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
        PcbSelection selection; selection.kind = PcbSelection::Kind::Footprint;
        selection.index = static_cast<std::size_t>(event.GetData()); ShowSelection(selection);
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); }, m_timer.GetId());
    m_timer.Start(200);
    SetLoading(false);
    if (!path.empty()) LoadBoard(path);
}

PcbFrame::~PcbFrame()
{
    m_timer.Stop();
    // No worker retains a window pointer or dispatches events to a dead frame.
    if (m_pending.valid()) m_pending.wait();
}

std::string PcbFrame::FileStamp(const wxString& path)
{
    std::error_code error;
    const std::filesystem::path file(path.ToStdWstring());
    const auto time = std::filesystem::last_write_time(file, error);
    if (error) return {};
    const auto size = std::filesystem::file_size(file, error);
    if (error) return {};
    return std::to_string(time.time_since_epoch().count()) + ":" + std::to_string(size);
}

bool PcbFrame::LoadBoard(const wxString& path, bool automatic)
{
    if (m_pending.valid()) return false;
    if (path.empty()) return false;
    SetLoading(true);
    SetStatusText(wxT("正在导入 PCB，原有板图保留至加载成功…"));
    try
    {
        m_pending = std::async(std::launch::async, [path, automatic] {
            LoadResult result; result.path = path; result.automatic = automatic;
            const auto before = FileStamp(path);
            result.ok = KiCadPcbImporter::Load(path, result.document, result.error);
            result.stamp = FileStamp(path);
            if (before != result.stamp || (result.ok && before.empty()))
            { result.ok = false; result.retry = true; result.error = wxT("源文件在读取期间发生变化，请等待 KiCad 保存完成后刷新。"); }
            return result;
        });
    }
    catch (const std::exception& exception)
    {
        SetLoading(false); Report(wxT("无法启动导入任务：") + wxString::FromUTF8(exception.what())); return false;
    }
    return true;
}

void PcbFrame::Poll()
{
    if (m_pending.valid())
    {
        if (m_pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        try { Commit(m_pending.get()); }
        catch (const std::exception& exception)
        { SetLoading(false); Report(wxT("导入失败，原板图已保留：") + wxString::FromUTF8(exception.what())); }
        return;
    }
    if (++m_pollCount % 5 != 0 || !m_document || !m_autoRefresh->GetValue()) return;
    const auto stamp = FileStamp(m_document->sourcePath);
    if (stamp.empty())
    { SetStatusText(wxT("源文件暂不可读取；保留当前板图，等待下次检查。")); m_candidateStamp.clear(); return; }
    if (stamp == m_loadedStamp || stamp == m_failedStamp) { m_candidateStamp.clear(); return; }
    if (stamp != m_candidateStamp) { m_candidateStamp = stamp; return; }
    m_candidateStamp.clear();
    LoadBoard(m_document->sourcePath, true);
}

void PcbFrame::Commit(LoadResult result)
{
    SetLoading(false);
    if (!result.ok)
    {
        if (m_document && m_document->sourcePath.CmpNoCase(result.path) == 0)
            m_failedStamp = result.retry ? std::string() : result.stamp;
        Report(wxT("导入失败，已保留当前板图：") + result.error);
        if (!result.automatic)
            wxMessageBox(result.error, wxT("无法导入 KiCad PCB"), wxOK | wxICON_ERROR, this);
        return;
    }
    const bool sameSource = m_document && m_document->sourcePath.CmpNoCase(result.path) == 0;
    result.document.sourcePath = result.path;
    m_document = std::make_shared<PcbDocument>(std::move(result.document));
    m_loadedStamp = result.stamp; m_failedStamp.clear(); m_candidateStamp.clear();
    m_canvas->SetDocument(m_document, sameSource);
    PopulateLists();
    SetTitle(wxFileName(m_document->sourcePath).GetFullName() + wxT(" - KiCad PCB 工作区"));
    m_summary->SetLabel(wxString::Format(wxT("%s  ·  %llu 个封装  %llu 个焊盘  %llu 条走线  %llu 个过孔  ·  源文件只读"),
        wxFileName(m_document->sourcePath).GetFullName(), static_cast<unsigned long long>(m_document->footprints.size()),
        static_cast<unsigned long long>([&] { std::size_t count = 0; for (const auto& footprint : m_document->footprints) count += footprint.pads.size(); return count; }()),
        static_cast<unsigned long long>(m_document->tracks.size()), static_cast<unsigned long long>(m_document->vias.size())));
    m_report->ChangeValue(wxT("源文件：") + m_document->sourcePath +
        wxString::Format(wxT("\n格式版本：%d\n\n"), m_document->version) +
        wxT("导入、另存快照均不修改 KiCad 源文件。联动读取已保存的文件；KiCad 中未保存的更改不会同步。\n") +
        wxT("本工作区用于板图查看和检查，布线、封装编辑和 DRC 请在 KiCad 中完成。\n"));
    if (m_document->nets.size() > 5000 || m_document->footprints.size() > 5000)
        m_report->AppendText(wxT("\n网络/元件列表每次最多显示 5000 项，请使用搜索缩小范围；板图数据和绘制不受此列表上限影响。\n"));
    if (m_document->warnings.empty()) m_report->AppendText(wxT("\n没有导入警告。"));
    else
    {
        m_report->AppendText(wxT("\n导入提示：\n"));
        for (const auto& warning : m_document->warnings) m_report->AppendText(wxT("• ") + Utf8(warning) + "\n");
    }
    m_inspector->ChangeValue(wxT("导入成功。点击对象检查位置、图层、网络和封装信息；选择“网络”页中的条目可高亮整条网络。"));
    Report(result.automatic ? wxT("已同步 KiCad 保存后的最新板图。") : wxT("PCB 导入完成。"), false);
    SetLoading(false); Layout();
}

void PcbFrame::SetLoading(bool loading)
{
    m_importButton->Enable(!loading);
    const bool ready = !loading && static_cast<bool>(m_document);
    m_reloadButton->Enable(ready); m_openButton->Enable(ready); m_copyButton->Enable(ready);
}

void PcbFrame::ChooseBoard()
{
    wxFileDialog dialog(this, wxT("导入 KiCad PCB 板图"),
        m_document ? wxFileName(m_document->sourcePath).GetPath() : wxString(), wxEmptyString,
        wxT("KiCad PCB 板图 (*.kicad_pcb)|*.kicad_pcb"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) LoadBoard(dialog.GetPath());
}

void PcbFrame::PopulateLists()
{
    m_layers->Clear(); m_layerNames.clear();
    for (const auto& layer : m_document->layers)
    {
        m_layerNames.push_back(layer.name);
        const auto index = m_layers->Append(Utf8(layer.name) +
            (layer.userName.empty() || layer.userName == layer.name ? wxString() : " · " + Utf8(layer.userName)));
        m_layers->Check(index, m_canvas->IsLayerVisible(layer.name));
    }
    PopulateNets();
    PopulateFootprints();
}

void PcbFrame::PopulateFootprints()
{
    m_footprints->DeleteAllItems();
    if (!m_document) return;
    const auto filter = m_footprintSearch->GetValue().Lower();
    for (std::size_t index = 0; index < m_document->footprints.size(); ++index)
    {
        const auto& footprint = m_document->footprints[index];
        if (!filter.empty() && !Utf8(footprint.reference).Lower().Contains(filter) &&
            !Utf8(footprint.value).Lower().Contains(filter) && !Utf8(footprint.libraryId).Lower().Contains(filter)) continue;
        if (m_footprints->GetItemCount() >= 5000) break;
        const long item = m_footprints->InsertItem(m_footprints->GetItemCount(), Utf8(footprint.reference));
        m_footprints->SetItem(item, 1, Utf8(footprint.value));
        m_footprints->SetItemData(item, static_cast<wxUIntPtr>(index));
    }
}

void PcbFrame::PopulateNets()
{
    m_nets->DeleteAllItems();
    if (!m_document) return;
    const auto filter = m_netSearch->GetValue().Lower();
    for (const auto& [id, name] : m_document->nets)
    {
        const auto number = wxString::Format("%d", id);
        if (!filter.empty() && !Utf8(name).Lower().Contains(filter) && !number.Contains(filter)) continue;
        if (m_nets->GetItemCount() >= 5000) break;
        const long item = m_nets->InsertItem(m_nets->GetItemCount(), number);
        m_nets->SetItem(item, 1, name.empty() ? wxString(wxT("未连接")) : Utf8(name));
        m_nets->SetItemData(item, static_cast<wxUIntPtr>(id));
    }
}

void PcbFrame::SelectNet(int id)
{
    if (!m_document) return;
    // Net zero denotes independent unconnected pads, not one shared net.
    m_canvas->SetHighlightedNet(id == 0 ? std::nullopt : std::optional<int>(id));
    m_inspector->ChangeValue(wxT("网络：") + NetName(*m_document, id) +
        wxString::Format(wxT("\n网络编号：%d\n"), id) +
        (id == 0 ? wxT("0 代表未连接对象，不会作为共同网络高亮。") : wxT("同一网络的焊盘、走线和过孔已高亮。")));
}

void PcbFrame::ShowSelection(const PcbSelection& selection)
{
    if (!m_document) return;
    const auto& document = *m_document;
    wxString text;
    int net = 0;
    switch (selection.kind)
    {
    case PcbSelection::Kind::Footprint:
        if (selection.index < document.footprints.size())
        {
            const auto& footprint = document.footprints[selection.index];
            text = wxT("封装：") + Utf8(footprint.reference) + wxT("\n数值：") + Utf8(footprint.value) +
                wxT("\n库：") + Utf8(footprint.libraryId) + wxT("\n位置：") + Point({footprint.x, footprint.y}) +
                wxString::Format(wxT("\n旋转：%.3f°\n图层："), footprint.rotation) + Utf8(footprint.layer);
        }
        break;
    case PcbSelection::Kind::Pad:
        if (selection.index < document.footprints.size() && selection.subIndex < document.footprints[selection.index].pads.size())
        {
            const auto& footprint = document.footprints[selection.index]; const auto& pad = footprint.pads[selection.subIndex]; net = pad.net;
            text = wxT("焊盘：") + Utf8(footprint.reference) + "." + Utf8(pad.number) + wxT("\n类型：") + Utf8(pad.type) +
                " / " + Utf8(pad.shape) + wxT("\n位置：") + Point(pad.position) + wxT("\n尺寸：") + Point(pad.size) +
                wxString::Format(wxT("\n旋转：%.3f°\n图层："), pad.rotation) + Joined(pad.layers);
        }
        break;
    case PcbSelection::Kind::Track:
        if (selection.index < document.tracks.size())
        {
            const auto& track = document.tracks[selection.index]; net = track.net;
            text = (track.mid ? wxString(wxT("弧形走线")) : wxString(wxT("走线"))) + wxT("\n起点：") + Point(track.start) +
                wxT("\n终点：") + Point(track.end) + wxT("\n线宽：") + Mm(track.width) + wxT("\n图层：") + Utf8(track.layer);
        }
        break;
    case PcbSelection::Kind::Via:
        if (selection.index < document.vias.size())
        {
            const auto& via = document.vias[selection.index]; net = via.net;
            text = wxT("过孔\n位置：") + Point(via.position) + wxT("\n外径：") + Mm(via.diameter) +
                wxT("\n钻孔：") + Mm(via.drill) + wxT("\n图层：") + Joined(via.layers);
        }
        break;
    case PcbSelection::Kind::Zone:
        if (selection.index < document.zones.size())
        {
            const auto& zone = document.zones[selection.index]; net = zone.net;
            text = (zone.keepout ? wxString(wxT("禁止区域")) : wxString(wxT("覆铜区域"))) + wxT("\n图层：") + Joined(zone.layers) +
                wxString::Format(wxT("\n轮廓 / 填充多边形：%llu"), static_cast<unsigned long long>(zone.polygons.size()));
        }
        break;
    case PcbSelection::Kind::Graphic:
    {
        const PcbGraphic* graphic = nullptr;
        if (selection.footprintGraphic && selection.index < document.footprints.size() &&
            selection.subIndex < document.footprints[selection.index].graphics.size())
            graphic = &document.footprints[selection.index].graphics[selection.subIndex];
        else if (!selection.footprintGraphic && selection.index < document.graphics.size()) graphic = &document.graphics[selection.index];
        if (graphic) text = wxT("图形 / 文字\n图层：") + Utf8(graphic->layer) + wxT("\n内容：") + Utf8(graphic->text);
        break;
    }
    default: break;
    }
    if (net != 0) text += wxT("\n网络：") + NetName(document, net) + wxString::Format(" (#%d)", net);
    if (selection.kind == PcbSelection::Kind::Pad || selection.kind == PcbSelection::Kind::Track ||
        selection.kind == PcbSelection::Kind::Via || selection.kind == PcbSelection::Kind::Zone)
        m_canvas->SetHighlightedNet(net == 0 ? std::nullopt : std::optional<int>(net));
    if (text.empty()) text = wxT("未选中对象。滚轮缩放，中键或右键拖动平移。");
    m_inspector->ChangeValue(text);
}

bool PcbFrame::WriteCopy(const wxString& path, wxString& error) const
{
    error.clear();
    if (!m_document) { error = wxT("尚未导入板图。"); return false; }
    wxFileName source(m_document->sourcePath), destination(path);
    source.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    destination.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    if (source.GetFullPath().CmpNoCase(destination.GetFullPath()) == 0)
    { error = wxT("请为快照选择新文件；KiCad 源文件由 KiCad 管理。"); return false; }
    std::error_code sameFileError;
    if (std::filesystem::equivalent(std::filesystem::path(source.GetFullPath().ToStdWstring()),
        std::filesystem::path(destination.GetFullPath().ToStdWstring()), sameFileError))
    { error = wxT("目标与源文件指向同一个文件，请选择新的快照路径。"); return false; }
    wxTempFile output(path);
    if (!output.IsOpened() || !output.Write(m_document->originalSource.data(), m_document->originalSource.size()) ||
        !output.Flush() || !output.Commit())
    { error = wxT("无法完整写入 PCB 快照，目标文件已保留。"); return false; }
    return true;
}

void PcbFrame::SaveCopy()
{
    if (!m_document) return;
    const wxFileName source(m_document->sourcePath);
    wxFileDialog dialog(this, wxT("原样另存已导入的 PCB 快照"), source.GetPath(), source.GetName() + "_imported.kicad_pcb",
        wxT("KiCad PCB 板图 (*.kicad_pcb)|*.kicad_pcb"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    wxString path = dialog.GetPath();
    if (!wxFileName(path).HasExt()) path += ".kicad_pcb";
    wxString error;
    if (!WriteCopy(path, error)) wxMessageBox(error, wxT("无法保存 PCB 快照"), wxOK | wxICON_ERROR, this);
    else Report(wxT("已原样保存 PCB 快照：") + path);
}

void PcbFrame::OpenInKiCad()
{
    if (!m_document) return;
    if (!wxFileExists(m_document->sourcePath))
    { wxMessageBox(wxT("源文件已移动或删除，请重新导入现有板图。"), wxT("KiCad 联动"), wxOK | wxICON_ERROR, this); return; }
    wxString executable;
    wxConfigBase::Get()->Read("KiCad/PcbnewPath", &executable);
    if (!wxFileExists(executable))
    {
        const wxString programFiles = wxGetenv("ProgramFiles");
        for (const auto& candidate : {wxString("D:\\KiCad\\bin\\pcbnew.exe"),
            programFiles + "\\KiCad\\10.0\\bin\\pcbnew.exe", programFiles + "\\KiCad\\9.0\\bin\\pcbnew.exe",
            programFiles + "\\KiCad\\8.0\\bin\\pcbnew.exe"})
            if (wxFileExists(candidate)) { executable = candidate; break; }
    }
    if (!wxFileExists(executable))
    {
        wxFileDialog dialog(this, wxT("选择 KiCad PCB 编辑器 pcbnew.exe"), wxEmptyString, "pcbnew.exe",
            wxT("KiCad PCB 编辑器 (pcbnew.exe)|pcbnew.exe"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) return;
        executable = dialog.GetPath();
    }
    wxConfigBase::Get()->Write("KiCad/PcbnewPath", executable);
    const wxString path = m_document->sourcePath;
    const wchar_t* arguments[]{executable.wc_str(), path.wc_str(), nullptr};
    if (wxExecute(arguments, wxEXEC_ASYNC) == 0)
        wxMessageBox(wxT("无法启动 KiCad PCB 编辑器，请检查程序路径。"), wxT("KiCad 联动"), wxOK | wxICON_ERROR, this);
    else Report(wxT("已在 KiCad 打开源板图；保存后本工作区会按自动刷新设置同步。"));
}

void PcbFrame::Report(const wxString& text, bool append)
{
    SetStatusText(text);
    if (append) m_report->AppendText("\n" + text);
}
