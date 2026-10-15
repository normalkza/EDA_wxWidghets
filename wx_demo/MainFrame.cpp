#include "MainFrame.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "PropertyPanel.h"
#include "SelectionState.h"
#include "CircuitFile.h"
#include <wx/splitter.h>
#include <wx/filedlg.h>
#include <wx/filename.h>

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, wxT("数字电路编辑器"),
        wxDefaultPosition, wxSize(1200, 800))
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
    auto* canvas = new Canvas(workspace);
    m_canvas = canvas;
    canvas->SetModifiedCallback([this] { UpdateDocumentTitle(); });
    Bind(wxEVT_MENU, &MainFrame::OnNew, this, wxID_NEW);
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
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
    workspace->SplitVertically(sidebar, canvas, FromDIP(320));
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
    canvas->SetComponentPlacedCallback([this, toolbox] {
        toolbox->SelectTool(wxEmptyString);
        UpdateMainToolBarSelection(GetToolBar(), wxEmptyString);
    });
    canvas->SetSelectionCallback([properties, canvas](const Component* component, int x, int y) {
        if (component) properties->ShowComponent(component, x, y);
        else properties->ShowTool(g_selectedType, canvas->GetTextFontSize(), canvas->GetGateInputCount());
    });
    canvas->SetTextSelectionCallback([properties](const wxRect& rect, int pointSize) {
        properties->ShowTextBox(rect, pointSize);
    });
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
    // 按实际可用高度初始化，让状态区占左栏约 45%。
    sidebar->SetSashPosition(sidebar->GetClientSize().GetHeight() * 55 / 100);
    UpdateDocumentTitle();
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
    if (!CircuitFile::Save(path, m_canvas->ExportDocument(), error))
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
    UpdateDocumentTitle();
    SetStatusText(wxT("已新建电路"));
}

void MainFrame::OnOpen(wxCommandEvent&)
{
    wxFileDialog dialog(this, wxT("打开电路"), wxFileName(m_filePath).GetPath(),
        wxEmptyString, wxT("电路 XML 文件 (*.xml)|*.xml"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK || !ConfirmDiscardChanges()) return;

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
    UpdateDocumentTitle();
    SetStatusText(wxT("已打开：") + path);
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
