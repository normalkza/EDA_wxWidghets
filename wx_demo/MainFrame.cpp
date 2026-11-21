#include "MainFrame.h"
#include "AiAssistantDialog.h"
#include "PetWebView.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "PropertyPanel.h"
#include "SelectionState.h"
#include "CircuitFile.h"
#include "SimulationPanel.h"
#include "PcbFrame.h"
#include "KiCadSchematicExporter.h"
#include <wx/utils.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/splitter.h>
#include <wx/filedlg.h>
#include <wx/choicdlg.h>
#include <algorithm>
#include <utility>
#include <vector>
#include <wx/dir.h>
#include <wx/arrstr.h>
#include "KiCadNetlistExporter.h"
#include "AiSession.h"
#include <wx/stdpaths.h>


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
    auto* editor = new wxPanel(workspace, wxID_ANY);
    auto* canvas = new Canvas(editor);
    m_editor = editor;
    m_simulation = new SimulationPanel(editor, canvas);
    // 仿真面板只在独立窗口中显示，默认不占用主编辑网格的空间。
    m_simulation->Hide();
    auto* editorSizer = new wxBoxSizer(wxVERTICAL);
    editorSizer->Add(canvas, 1, wxEXPAND);
    editorSizer->Add(m_simulation, 0, wxEXPAND);
    editor->SetSizer(editorSizer);
    m_canvas = canvas;
    m_workspace = workspace;
    m_sidebar = sidebar;
    m_toolbox = toolbox;
    m_properties = properties;
    m_sidebarWidth = FromDIP(320);
    canvas->SetModifiedCallback([this] { UpdateDocumentTitle(); });
    canvas->SetDocumentChangedCallback([this] { m_simulation->InvalidateDocument(); });
    canvas->SetDocumentResetCallback([this] {
        if (m_tutorialManager) m_tutorialManager->CancelTutorial();
    });
    canvas->SetRuntimeInputCallback([this](int id) { return m_simulation->ToggleInput(id); });
    m_simulation->SetExampleCallback([this] { LoadTimingExample(); });
    Bind(wxEVT_MENU, &MainFrame::OnNew, this, wxID_NEW);
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnImportKiCadPcb, this, ID_FILE_IMPORT_KICAD_PCB);
    // 单独导出 KiCad 原理图：只生成 .kicad_sch，不自动生成网表。
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (!m_canvas) return;
        wxFileDialog dialog(this, wxT("导出 KiCad 原理图"), wxEmptyString,
            wxT("circuit.kicad_sch"),
            wxT("KiCad 原理图文件 (*.kicad_sch)|*.kicad_sch"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) return;

        const wxString path = dialog.GetPath();
        const CircuitDocument document = m_canvas->ExportDocument();
        std::string error;
        if (!KiCadSchematicExporter::ExportSchematic(
            document, path.ToStdString(wxConvUTF8), std::string(), error, this))
        {
            const wxString message = wxString::FromUTF8(error);
            SetStatusText(wxT("KiCad 原理图导出失败：") + message);
            wxMessageBox(message, wxT("原理图导出失败"), wxOK | wxICON_ERROR, this);
            return;
        }

        SetStatusText(wxT("KiCad 原理图已导出：") + path);
        wxMessageBox(wxT("KiCad 原理图导出成功！\n\n") + path,
            wxT("导出完成"), wxOK | wxICON_INFORMATION, this);
    }, ID_EXPORT_KICAD_SCHEMATIC);


    // 单独导出 KiCad 网表：允许用户选择保存位置。
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (!m_canvas) return;

        // 默认文件名和目录
        wxString defaultDir;
        wxString defaultName = wxT("circuit.net");

        if (!m_filePath.IsEmpty())
        {
            wxFileName outputPath(m_filePath);
            defaultDir = outputPath.GetPath();
            defaultName = outputPath.GetName() + wxT(".net");
        }
        else
        {
            wxFileName outputPath(
                wxStandardPaths::Get().GetExecutablePath()
            );
            defaultDir = outputPath.GetPath();
        }

        // 弹出网表保存对话框
        wxFileDialog dialog(
            this,
            wxT("导出 KiCad 网表"),
            defaultDir,
            defaultName,
            wxT("KiCad 网表文件 (*.net)|*.net"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );

        if (dialog.ShowModal() != wxID_OK)
            return;

        wxString netlistPath = dialog.GetPath();

        // 确保文件扩展名为 .net
        wxFileName selectedFile(netlistPath);
        if (selectedFile.GetExt().CmpNoCase(wxT("net")) != 0)
        {
            selectedFile.SetExt(wxT("net"));
            netlistPath = selectedFile.GetFullPath();
        }

        std::string error;
        if (!KiCadNetlistExporter::ExportFullAdder(
            m_canvas->ExportDocument(),
            netlistPath.ToStdString(wxConvUTF8),
            error))
        {
            const wxString message = wxString::FromUTF8(error);
            SetStatusText(wxT("KiCad 网表导出失败：") + message);
            wxMessageBox(
                message,
                wxT("网表导出失败"),
                wxOK | wxICON_ERROR,
                this
            );
            return;
        }

        SetStatusText(wxT("KiCad 网表已导出：") + netlistPath);
        wxMessageBox(
            wxT("KiCad 网表导出成功！\n\n保存位置：\n") + netlistPath,
            wxT("导出完成"),
            wxOK | wxICON_INFORMATION,
            this
        );
        }, ID_EXPORT_KICAD_NETLIST);

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

        // 通知教程：用户选择了哪个工具
        if (m_tutorialManager)
            m_tutorialManager->NotifyToolSelected(type);
        };

    if (auto* toolbar = CreateMainToolBar(this, showTool)) SetToolBar(toolbar);
    toolbox->SetSelectionCallback(showTool);

    const std::vector<std::pair<int, wxString>> menuTools = {
        {ID_TOOL_SELECT, wxEmptyString}, {ID_TOOL_DELETE, "DELETE"},
        {ID_TOOL_INPUT, "INPUT"}, {ID_TOOL_OUTPUT, "OUTPUT"},
        {ID_TOOL_AND, "AND"}, {ID_TOOL_OR, "OR"}, {ID_TOOL_NOT, "NOT"},
        {ID_TOOL_NAND, "NAND"}, {ID_TOOL_NOR, "NOR"}, {ID_TOOL_XOR, "XOR"},
        {ID_TOOL_TEXT, "TEXT"}, {ID_TOOL_WIRE, "WIRE"},
        {ID_TOOL_CLOCK, "CLOCK"}, {ID_TOOL_DFF, "DFF"}, {ID_TOOL_MUX, "MUX"},
        {ID_TOOL_COMPARATOR, "COMPARATOR"}, {ID_TOOL_PROBE, "PROBE"},
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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenSimulationWindow(); }, ID_SIMULATE_WINDOW);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) { canvas->EditSelectedTimingParameters(); }, ID_COMPONENT_TIMING);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) { event.Enable(canvas->CanEditTimingParameters()); }, ID_COMPONENT_TIMING);
    Bind(wxEVT_MENU, [canvas](wxCommandEvent&) {
        canvas->ToggleSelectedInput();
        }, ID_SIMULATE_TOGGLE_INPUT);
    Bind(wxEVT_UPDATE_UI, [canvas](wxUpdateUIEvent& event) {
        event.Enable(canvas->HasSelectedInput());
        }, ID_SIMULATE_TOGGLE_INPUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowShortcuts(); }, wxID_HELP);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (m_petPlugin.get() && m_petPlugin->IsShown()) m_petPlugin->SetPageVisible(false);
        else ShowCompanion();
    }, ID_HELP_COMPANION);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
        event.Check(m_petPlugin.get() && m_petPlugin->IsShown());
    }, ID_HELP_COMPANION);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxMessageBox(wxT("数字电路编辑器\n\n逻辑电路绘制与实时模拟\n")
            wxT("支持 XML 保存、元件旋转、正交连线与撤销。"),
            wxT("关于"), wxOK | wxICON_INFORMATION, this);
        }, wxID_ABOUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenAiAssistant(); }, ID_HELP_AI_ASSISTANT);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(workspace, 1, wxEXPAND | wxALL, FromDIP(12));
    SetSizer(root);
    Layout();

    // 按实际可用高度初始化，让状态区占左栏约 45%。
    sidebar->SetSashPosition(sidebar->GetClientSize().GetHeight() * 55 / 100);
    m_sidebarSash = sidebar->GetSashPosition();
    UpdateDocumentTitle();

    // 必须放在 canvas->SetComponentPlacedCallback 之前，否则点击会崩溃
    m_tutorialManager = new TutorialManager(this);

    canvas->SetComponentPlacedCallback([this, toolbox](const wxString& type, int componentId) {
        // 普通放置后恢复选择工具；教程管理器会在需要时重新选择下一种工具。
        toolbox->SelectTool(wxEmptyString);
        UpdateMainToolBarSelection(GetToolBar(), wxEmptyString);
        if (m_tutorialManager) m_tutorialManager->NotifyComponentPlaced(type, componentId);
        });
    canvas->SetWireCreatedCallback(
        [this](const CircuitWireData& wire)
        {
            if (m_tutorialManager)
            {
                m_tutorialManager->NotifyWireCreated(wire);
            }
        }
    );
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


    Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartTutorial(); }, ID_START_TUTORIAL);
}

MainFrame::~MainFrame()
{
    if (m_aiAssistant.get()) m_aiAssistant->SetStateCallback({});
    if (auto* pet = m_petPlugin.get())
    {
        pet->SetActionCallback({});
        pet->SetStatusCallback({});
        delete pet;
    }
    if (m_canvas) m_canvas->SetDocumentResetCallback({});
    delete m_tutorialManager;
    m_tutorialManager = nullptr;
}

void MainFrame::OpenAiAssistant()
{
    if (m_aiClosePending) return;
    if (m_tutorialManager) m_tutorialManager->CancelTutorial();
    if (!m_aiAssistant.get())
    {
        m_aiAssistant = wxWeakRef<AiAssistantDialog>(new AiAssistantDialog(this, *m_canvas));
        m_aiAssistant->SetStateCallback([this](AiAssistantState state) {
            switch (state)
            {
            case AiAssistantState::Working: m_assistantPetState = "thinking"; break;
            case AiAssistantState::Answered:
            case AiAssistantState::Applied: m_assistantPetState = "done"; break;
            case AiAssistantState::Failed: m_assistantPetState = "error"; break;
            case AiAssistantState::Cancelled: m_assistantPetState = "cancelled"; break;
            default: m_assistantPetState = "idle"; break;
            }
            if (m_petPlugin.get()) m_petPlugin->SetPetState(m_assistantPetState);
        });
    }
    m_aiAssistant->ShowAssistant();
}

PetWebView* MainFrame::EnsurePetPlugin()
{
    if (!m_petPlugin.get())
    {
        m_petPlugin = new PetWebView(this, m_canvas);
        m_petPlugin->SetActionCallback([this](const wxString& action) {
            if (m_aiClosePending) return;
            if (action == "open-ai") OpenAiAssistant();
            else if (action == "hide-pet") m_petPlugin->SetPageVisible(false);
            else if (action == "reload-pet") m_petPlugin->Reload();
        });
        m_petPlugin->SetStatusCallback([this](const wxString& status) {
            SetStatusText(status);
        });
        m_petPlugin->SetPetState(m_assistantPetState);
    }
    return m_petPlugin.get();
}

void MainFrame::ShowCompanion()
{
    if (!m_aiClosePending) EnsurePetPlugin()->SetPageVisible(true);
}

void MainFrame::ShowShortcuts()
{
    wxMessageBox(
        wxT("Ctrl+N  新建\nCtrl+O  打开\nCtrl+S  保存\nCtrl+Shift+S  另存为\n\n")
        wxT("Ctrl+Z  撤销\nCtrl+X / C / V  剪切 / 复制 / 粘贴\n")
        wxT("Ctrl+D / Delete  删除\n\n")
        wxT("R  顺时针旋转 90°\nShift+R  逆时针旋转 90°\n")
        wxT("Esc  取消当前操作\nCtrl+Enter  完成文字编辑\n\n")
        wxT("F5  复位仿真\nF6  切换选中输入电平\nF7  下一事件\nF8  运行 / 继续\n")
        wxT("双击 CLOCK / DFF / PROBE  编辑参数\nCtrl+T  新手教程"),
        wxT("快捷键"), wxOK | wxICON_INFORMATION, this);
}

void MainFrame::StartTutorial()
{
    if (!m_tutorialManager || m_aiClosePending) return;
    wxArrayString choices;
    choices.Add(wxT("半加器教程"));
    choices.Add(wxT("全加器教程"));
    wxSingleChoiceDialog dialog(this, wxT("请选择要学习的教程："), wxT("新手教程"), choices);
    if (dialog.ShowModal() != wxID_OK) return;
    if (!ConfirmDiscardChanges()) return;
    if (m_petPlugin.get()) m_petPlugin->SetPageVisible(false);
    if (!m_showToolbox)
    {
        m_showToolbox = true;
        UpdateSidebarVisibility();
    }
    m_filePath.Clear();
    m_tutorialManager->StartTutorial(dialog.GetSelection() == 0 ?
        wxT("半加器 (Half Adder)") : wxT("全加器 (Full Adder)"));
    m_simulation->InvalidateDocument();
    UpdateDocumentTitle();
}

void MainFrame::OpenSimulationWindow()
{
    if (m_simulationFrame)
    {
        m_simulationFrame->Raise();
        return;
    }
    m_simulationFrame = new wxFrame(this, wxID_ANY, wxT("数字时序仿真"),
        wxDefaultPosition, FromDIP(wxSize(1100, 650)),
        wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER);
    if (auto* sizer = m_editor->GetSizer()) sizer->Detach(m_simulation);
    m_simulation->Reparent(m_simulationFrame);
    m_simulation->Show();
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_simulation, 1, wxEXPAND);
    m_simulationFrame->SetSizer(sizer);
    m_simulationFrame->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        RestoreSimulationPanel();
        event.Skip();
        });
    m_simulationFrame->CentreOnParent();
    m_simulationFrame->Show();
}

void MainFrame::RestoreSimulationPanel()
{
    if (!m_simulationFrame) return;
    if (auto* frameSizer = m_simulationFrame->GetSizer())
        frameSizer->Detach(m_simulation);
    m_simulation->Reparent(m_editor);
    m_simulation->Hide();
    if (auto* sizer = m_editor->GetSizer()) sizer->Add(m_simulation, 0, wxEXPAND);
    m_editor->Layout();
    m_simulationFrame->SetSizer(nullptr);
    m_simulationFrame->Destroy();
    m_simulationFrame = nullptr;
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
        wxT(" - 数字电路编辑器") + (AiSession::Get().UserName().empty() ? wxString() :
            wxS(" · ") + AiSession::Get().UserName()) +
        (AiSession::Get().IsOffline() ? wxString(wxS(" · 离线模式")) : wxString()));
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
    if (m_tutorialManager) m_tutorialManager->CancelTutorial();
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
    if (!CircuitFile::Load(path, document, error))
    {
        SetStatusText(wxT("打开失败：") + error);
        wxMessageBox(error, wxT("无法打开电路"), wxOK | wxICON_ERROR, this);
        return;
    }
    if (m_tutorialManager) m_tutorialManager->CancelTutorial();
    if (!m_canvas->ImportDocument(document, error))
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
    if (m_tutorialManager) m_tutorialManager->CancelTutorial();

    // WinHTTP reports cancellation on the application event queue. Leave that
    // queue running until the assistant has stopped before closing the frame.
    if (event.CanVeto() && m_aiAssistant.get() && m_aiAssistant->IsBusy())
    {
        event.Veto();
        if (!m_aiClosePending)
        {
            m_aiClosePending = true;
            wxWeakRef<MainFrame> frame(this);
            m_aiAssistant->BeginOwnerShutdown([frame] {
                if (auto* window = frame.get()) window->Close();
            });
        }
        return;
    }
    const bool wasAiClosePending = std::exchange(m_aiClosePending, false);
    if (event.CanVeto() && !ConfirmDiscardChanges())
    {
        if (wasAiClosePending && m_aiAssistant.get()) m_aiAssistant->ResumeAfterOwnerShutdown();
        event.Veto();
        return;
    }
    if (m_aiAssistant.get()) m_aiAssistant->BeginOwnerShutdown({});
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
        {5, "PROBE", 580, 200, 1, false}, {6, "INPUT", 60, 360, 0, false} };
    design.components[0].clockPeriodNs = 20;
    design.components[0].clockHighNs = 10;
    design.components[3].probeLabel = "CLK";
    design.components[4].probeLabel = "Q_div2";
    design.wires = { {1, 0, 2, 1}, {1, 0, 4, 0}, {2, 0, 3, 0},
        {3, 0, 2, 0}, {2, 0, 5, 0}, {6, 0, 2, 2} };
    design.texts = { {wxRect(40, 10, 660, 35), 12, wxT("DFF 二分频 · Q 经 NOT 反馈到 D · INPUT 控制高有效异步 RESET")} };
    wxString error;
    if (m_tutorialManager) m_tutorialManager->CancelTutorial();
    if (!m_canvas->ImportDocument(design, error))
    {
        wxMessageBox(error, wxT("无法载入示例"), wxOK | wxICON_ERROR, this); return;
    }
    m_filePath.Clear();
    m_simulation->InvalidateDocument();
    UpdateDocumentTitle();
    SetStatusText(wxT("已载入二分频示例，点击“运行”查看 CLK 与 Q_div2；双击时钟可修改参数。"));
}
