#include "MainFrame.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "PropertyPanel.h"
#include "SelectionState.h"
#include "CircuitFile.h"
#include "SimulationPanel.h"
#include "PcbFrame.h"
#include <wx/splitter.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <algorithm>
#include <utility>
#include <vector>

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, wxT("数字电路编辑器"),
        wxDefaultPosition, wxSize(1400, 900))
{
    const wxColour background(243, 246, 251);
    const wxColour text(36, 50, 71);
    const wxColour secondary(100, 116, 139);
    const wxColour divider(220, 228, 239);
    const wxFont uiFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI")));
    SetFont(uiFont);
    SetBackgroundColour(background);
    SetForegroundColour(text);
    SetMenuBar(CreateMainMenuBar());
    auto* status = CreateStatusBar();
    status->SetFont(uiFont);
    status->SetBackgroundColour(*wxWHITE);
    status->SetForegroundColour(secondary);
    SetMinSize(FromDIP(wxSize(800, 600)));
    auto* workspace = new wxSplitterWindow(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    auto* sidebar = new wxSplitterWindow(workspace, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    workspace->SetBackgroundColour(divider);
    sidebar->SetBackgroundColour(divider);
    auto* toolbox = new Toolbox(sidebar);
    auto* properties = new PropertyPanel(sidebar);
    auto* editor = new wxSplitterWindow(workspace, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    auto* canvas = new Canvas(editor);
    m_editor = editor;
    m_simulation = new SimulationPanel(editor, canvas);
    editor->SetMinimumPaneSize(FromDIP(150));
    editor->SetSashGravity(0.62);
    editor->SplitHorizontally(canvas, m_simulation, FromDIP(450));
    editor->Bind(wxEVT_SPLITTER_DOUBLECLICKED, [](wxSplitterEvent& event) { event.Veto(); });
    m_canvas = canvas;
    m_workspace = workspace;
    m_sidebar = sidebar;
    m_toolbox = toolbox;
    m_properties = properties;
    m_sidebarWidth = FromDIP(320);
    canvas->SetModifiedCallback([this] { UpdateDocumentTitle(); });
    canvas->SetDocumentChangedCallback([this] { m_simulation->InvalidateDocument(); });
    canvas->SetRuntimeInputCallback([this](int id) { return m_simulation->ToggleInput(id); });
    m_simulation->SetExampleCallback([this] { LoadTimingExample(); });
    Bind(wxEVT_MENU, &MainFrame::OnNew, this, wxID_NEW);
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnImportKiCadPcb, this, ID_FILE_IMPORT_KICAD_PCB);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveDocument(); }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveDocument(true); }, wxID_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    for (const int command : {wxID_UNDO, wxID_CUT, wxID_COPY, wxID_PASTE, wxID_DELETE})
    {
        Bind(wxEVT_MENU, [canvas](wxCommandEvent& event) {
            canvas->HandleEditCommand(event.GetId());
        }, command);
    }
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Enable(canvas->CanUndo());
    }, wxID_UNDO);

    // 左栏加宽，状态区不再按全部内容高度挤压树状列表。
    workspace->SetMinimumPaneSize(FromDIP(220));
    workspace->SetSashGravity(0.0);
    sidebar->SetMinimumPaneSize(FromDIP(140));
    sidebar->SetSashGravity(0.55);
    sidebar->SplitHorizontally(toolbox, properties);
    workspace->SplitVertically(sidebar, editor, FromDIP(320));
    workspace->Bind(wxEVT_SPLITTER_DOUBLECLICKED, [](wxSplitterEvent& event) { event.Veto(); });
    sidebar->Bind(wxEVT_SPLITTER_DOUBLECLICKED, [](wxSplitterEvent& event) { event.Veto(); });

    auto showTool = [this, properties, canvas, toolbox](const wxString& type) {
        toolbox->SelectTool(type);
        UpdateMainToolBarSelection(GetToolBar(), type);
        canvas->OnToolChanged();
        properties->ShowTool(type, canvas->GetTextFontSize(), canvas->GetGateInputCount());
    };
    if (auto* toolbar = CreateMainToolBar(this, showTool)) SetToolBar(toolbar);
    toolbox->SetSelectionCallback(showTool);

    const std::vector<std::pair<int, wxString>> menuTools = {
        {ID_TOOL_SELECT, wxEmptyString}, {ID_TOOL_DELETE, "DELETE"},
        {ID_TOOL_INPUT, "INPUT"}, {ID_TOOL_OUTPUT, "OUTPUT"},
        {ID_TOOL_AND, "AND"}, {ID_TOOL_OR, "OR"}, {ID_TOOL_NOT, "NOT"},
        {ID_TOOL_NAND, "NAND"}, {ID_TOOL_NOR, "NOR"}, {ID_TOOL_XOR, "XOR"},
        {ID_TOOL_TEXT, "TEXT"}, {ID_TOOL_WIRE, "WIRE"},
        {ID_TOOL_CLOCK, "CLOCK"}, {ID_TOOL_DFF, "DFF"}, {ID_TOOL_PROBE, "PROBE"},
        {ID_TOOL_CONSTANT, "CONSTANT"}, {ID_TOOL_BUTTON, "BUTTON"}, {ID_TOOL_LED, "LED"}
    };
    for (const auto& [id, type] : menuTools)
    {
        Bind(wxEVT_MENU, [canvas, showTool, type](wxCommandEvent&) {
            g_selectedType = type;
            showTool(type);
            canvas->SetFocusIgnoringChildren();
        }, id);
        Bind(wxEVT_UPDATE_UI, [type](wxUpdateUIEvent& event) {
            event.Check(g_selectedType == type);
        }, id);
    }
    Bind(wxEVT_MENU, [canvas, showTool](wxCommandEvent&) {
        g_selectedType.Clear();
        showTool(wxEmptyString);
        canvas->DeselectAll();
    }, ID_EDIT_DESELECT);

    Bind(wxEVT_MENU, [canvas](wxCommandEvent& event) {
        canvas->SetGridVisible(event.IsChecked());
    }, ID_VIEW_GRID);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Check(canvas->IsGridVisible());
    }, ID_VIEW_GRID);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent& event) {
        canvas->SetSnapEnabled(event.IsChecked());
    }, ID_VIEW_SNAP);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Check(canvas->IsSnapEnabled());
    }, ID_VIEW_SNAP);
    Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
        m_showToolbox = event.IsChecked();
        UpdateSidebarVisibility();
    }, ID_VIEW_TOOLBOX);
    Bind(wxEVT_MENU, [this](wxCommandEvent& event) {
        m_showProperties = event.IsChecked();
        UpdateSidebarVisibility();
    }, ID_VIEW_PROPERTIES);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
        event.Check(m_showToolbox);
    }, ID_VIEW_TOOLBOX);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
        event.Check(m_showProperties);
    }, ID_VIEW_PROPERTIES);

    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) {
        canvas->RotateSelection(90);
    }, ID_COMPONENT_ROTATE_CW);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) {
        canvas->RotateSelection(-90);
    }, ID_COMPONENT_ROTATE_CCW);
    for (int id : {ID_COMPONENT_ROTATE_CW, ID_COMPONENT_ROTATE_CCW})
    {
        Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
            event.Enable(canvas->HasSelectedComponent());
        }, id);
    }
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) {
        canvas->ResetSelectedWireRoute();
    }, ID_WIRE_RESET_ROUTE);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Enable(canvas->CanResetWireRoute());
    }, ID_WIRE_RESET_ROUTE);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) {
        canvas->SetFocusIgnoringChildren();
        canvas->HandleEditCommand(wxID_DELETE);
    }, ID_WIRE_DELETE);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Enable(canvas->HasSelectedWire());
    }, ID_WIRE_DELETE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_simulation->Reset(); }, ID_SIMULATE_EVALUATE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_simulation->Run(); }, ID_SIMULATE_RUN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_simulation->Pause(); }, ID_SIMULATE_PAUSE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_simulation->Stop(); }, ID_SIMULATE_STOP);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_simulation->Step(); }, ID_SIMULATE_STEP);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_simulation->StepPeriod(); }, ID_SIMULATE_PERIOD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { LoadTimingExample(); }, ID_SIMULATE_EXAMPLE);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) { canvas->EditSelectedTimingParameters(); }, ID_COMPONENT_TIMING);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) { event.Enable(canvas->CanEditTimingParameters()); }, ID_COMPONENT_TIMING);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) {
        canvas->ToggleSelectedInput();
    }, ID_SIMULATE_TOGGLE_INPUT);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Enable(canvas->HasSelectedInput());
    }, ID_SIMULATE_TOGGLE_INPUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxMessageBox(
            wxT("Ctrl+N  新建\nCtrl+O  打开\nCtrl+S  保存\nCtrl+Shift+S  另存为\n\n")
            wxT("Ctrl+Z  撤销\nCtrl+X / C / V  剪切 / 复制 / 粘贴\n")
            wxT("Ctrl+D / Delete  删除\n\n")
            wxT("R  顺时针旋转 90°\nShift+R  逆时针旋转 90°\n")
            wxT("Esc  取消当前操作\nCtrl+Enter  完成文字编辑\n\n")
            wxT("F5  复位仿真\nF6  切换选中输入电平\nF7  下一事件\nF8  运行 / 继续\n")
            wxT("双击 CLOCK / DFF / PROBE  编辑参数\nF1  快捷键"),
            wxT("快捷键"), wxOK | wxICON_INFORMATION, this);
    }, wxID_HELP);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxMessageBox(wxT("数字电路编辑器\n\n逻辑电路绘制与实时模拟\n")
            wxT("支持 XML 保存、元件旋转、正交连线与撤销。"),
            wxT("关于"), wxOK | wxICON_INFORMATION, this);
    }, wxID_ABOUT);

    canvas->SetComponentPlacedCallback([this, toolbox] {
        toolbox->SelectTool(wxEmptyString);
        UpdateMainToolBarSelection(GetToolBar(), wxEmptyString);
    });
    canvas->SetSelectionCallback([properties, canvas](const Component* component, int x, int y) {
        if (component) properties->ShowComponent(component, x, y, canvas->GetSelectedRotation());
        else properties->ShowTool(g_selectedType, canvas->GetTextFontSize(), canvas->GetGateInputCount());
    });
    canvas->SetTextSelectionCallback([properties](const wxRect& rect, int pointSize) {
        properties->ShowTextBox(rect, pointSize);
    });
    properties->SetEditTimingCallback([canvas] { canvas->EditSelectedTimingParameters(); });
    properties->SetTextFontSizeCallback([canvas](int pointSize) {
        canvas->SetTextFontSize(pointSize);
    });
    properties->SetGateInputCountCallback([canvas](int count) {
        canvas->SetGateInputCount(count);
    });
    canvas->SetWireSelectionCallback([properties](const WireConnection* connection) {
        properties->ShowConnection(connection);
    });
    canvas->SetStatusCallback([this](const wxString& message) { SetStatusText(message); });

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(workspace, 1, wxEXPAND | wxALL, FromDIP(12));
    SetSizer(root);
    Layout();
    // Split only after the workspace receives its actual size. Gravity during
    // the initial layout otherwise leaves the waveform at its minimum height.
    editor->SetSashPosition(editor->GetClientSize().GetHeight() * 52 / 100);
    // 按实际可用高度初始化，让状态区占左栏约 45%。
    sidebar->SetSashPosition(sidebar->GetClientSize().GetHeight() * 55 / 100);
    m_sidebarSash = sidebar->GetSashPosition();
    UpdateDocumentTitle();
}

void MainFrame::UpdateSidebarVisibility()
{
    Freeze();
    if (m_workspace->IsSplit()) m_sidebarWidth = m_workspace->GetSashPosition();
    if (m_sidebar->IsSplit())
    {
        m_sidebarSash = m_sidebar->GetSashPosition();
        m_sidebar->Unsplit(m_properties);
    }
    if (m_showToolbox || m_showProperties)
    {
        wxWindow* first = m_showToolbox ? static_cast<wxWindow*>(m_toolbox) :
            static_cast<wxWindow*>(m_properties);
        if (m_sidebar->GetWindow1() != first)
            m_sidebar->ReplaceWindow(m_sidebar->GetWindow1(), first);
        m_toolbox->Show(m_showToolbox);
        m_properties->Show(m_showProperties);
        if (m_showToolbox && m_showProperties)
            m_sidebar->SplitHorizontally(m_toolbox, m_properties, m_sidebarSash);
        m_sidebar->Show();
        if (!m_workspace->IsSplit())
            m_workspace->SplitVertically(m_sidebar, m_editor, m_sidebarWidth);
    }
    else
    {
        if (m_workspace->IsSplit()) m_workspace->Unsplit(m_sidebar);
        m_toolbox->Hide();
        m_properties->Hide();
        m_sidebar->Hide();
    }
    Layout();
    m_workspace->UpdateSize();
    m_sidebar->UpdateSize();
    Thaw();
}

void MainFrame::UpdateDocumentTitle()
{
    const wxString name = m_filePath.IsEmpty() ? wxString(wxT("未命名.xml")) :
        wxFileName(m_filePath).GetFullName();
    SetTitle((m_canvas->IsModified() ? wxString("* ") : wxString()) + name +
        wxT(" - 数字电路编辑器"));
}

bool MainFrame::SaveDocument(bool saveAs)
{
    wxString path = m_filePath;
    if (saveAs || path.IsEmpty())
    {
        const wxFileName current(path);
        wxFileDialog dialog(this, wxT("保存电路"), current.GetPath(),
            path.IsEmpty() ? wxString(wxT("未命名.xml")) : current.GetFullName(),
            wxT("电路 XML 文件 (*.xml)|*.xml"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) return false;
        path = dialog.GetPath();
        if (!wxFileName(path).HasExt()) path += ".xml";
    }

    wxString error;
    const auto document = m_canvas->ExportDocument();
    if (wxFileExists(path))
    {
        CircuitDocument previous;
        wxString readError;
        if (CircuitFile::Load(path, previous, readError) && previous.formatVersion < document.formatVersion)
        {
            const wxString backup = path + wxString::Format(".v%d.bak", previous.formatVersion);
            if (!wxFileExists(backup) && !wxCopyFile(path, backup, false))
            {
                wxMessageBox(wxT("升级保存前无法备份旧格式文件：") + backup,
                    wxT("无法保存电路"), wxOK | wxICON_ERROR, this);
                return false;
            }
        }
    }
    if (!CircuitFile::Save(path, document, error))
    {
        SetStatusText(wxT("保存失败：") + error);
        wxMessageBox(error, wxT("无法保存电路"), wxOK | wxICON_ERROR, this);
        return false;
    }
    m_filePath = path;
    m_canvas->MarkSaved();
    UpdateDocumentTitle();
    SetStatusText(wxT("已保存：") + path);
    return true;
}

bool MainFrame::ConfirmDiscardChanges()
{
    if (!m_canvas->IsModified()) return true;
    const int answer = wxMessageBox(wxT("当前电路有未保存的修改，是否先保存？"),
        wxT("保存修改"), wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxYES) return SaveDocument();
    return answer == wxNO;
}

void MainFrame::OnNew(wxCommandEvent&)
{
    if (!ConfirmDiscardChanges()) return;
    m_filePath.Clear();
    m_canvas->ClearDocument();
    m_simulation->InvalidateDocument();
    UpdateDocumentTitle();
    SetStatusText(wxT("已新建电路"));
}

void MainFrame::OnOpen(wxCommandEvent&)
{
    wxFileDialog dialog(this, wxT("打开电路"), wxFileName(m_filePath).GetPath(),
        wxEmptyString, wxT("支持的电路文件 (*.xml;*.kicad_pcb)|*.xml;*.kicad_pcb|电路 XML 文件 (*.xml)|*.xml|KiCad PCB 板图 (*.kicad_pcb)|*.kicad_pcb"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    if (wxFileName(dialog.GetPath()).GetExt().CmpNoCase("kicad_pcb") == 0)
    {
        (new PcbFrame(this, dialog.GetPath()))->Show();
        return;
    }
    if (!ConfirmDiscardChanges()) return;

    CircuitDocument document;
    wxString error;
    const wxString path = dialog.GetPath();
    if (!CircuitFile::Load(path, document, error) || !m_canvas->ImportDocument(document, error))
    {
        SetStatusText(wxT("打开失败：") + error);
        wxMessageBox(error, wxT("无法打开电路"), wxOK | wxICON_ERROR, this);
        return;
    }
    m_filePath = path;
    m_simulation->InvalidateDocument();
    UpdateDocumentTitle();
    SetStatusText(wxT("已打开：") + path);
}

void MainFrame::OnImportKiCadPcb(wxCommandEvent&)
{
    wxFileDialog dialog(this, wxT("从 KiCad 导入 PCB 板图"), wxEmptyString, wxEmptyString,
        wxT("KiCad PCB 板图 (*.kicad_pcb)|*.kicad_pcb"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    (new PcbFrame(this, dialog.GetPath()))->Show();
}

void MainFrame::OnClose(wxCloseEvent& event)
{
    if (event.CanVeto() && !ConfirmDiscardChanges())
    {
        event.Veto();
        return;
    }
    event.Skip();
}

void MainFrame::LoadTimingExample()
{
    if (!ConfirmDiscardChanges()) return;
    CircuitDocument design;
    design.formatVersion = 3;
    design.simulation.stopTimeNs = 200;
    design.components = {
        {1, "CLOCK", 60, 60, 0, false}, {2, "DFF", 340, 100, 3, false},
        {3, "NOT", 180, 240, 1, false}, {4, "PROBE", 580, 60, 1, false},
        {5, "PROBE", 580, 200, 1, false}, {6, "INPUT", 60, 360, 0, false}};
    design.components[0].clockPeriodNs = 20;
    design.components[0].clockHighNs = 10;
    design.components[3].probeLabel = "CLK";
    design.components[4].probeLabel = "Q_div2";
    design.wires = {{1, 0, 2, 1}, {1, 0, 4, 0}, {2, 0, 3, 0},
        {3, 0, 2, 0}, {2, 0, 5, 0}, {6, 0, 2, 2}};
    design.texts = {{wxRect(40, 10, 660, 35), 12, wxT("DFF 二分频 · Q 经 NOT 反馈到 D · INPUT 控制高有效异步 RESET")}};
    wxString error;
    if (!m_canvas->ImportDocument(design, error))
    { wxMessageBox(error, wxT("无法载入示例"), wxOK | wxICON_ERROR, this); return; }
    m_filePath.Clear();
    m_simulation->InvalidateDocument();
    UpdateDocumentTitle();
    SetStatusText(wxT("已载入二分频示例，点击“运行”查看 CLK 与 Q_div2；双击时钟可修改参数。"));
}
