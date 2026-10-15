#include "MainFrame.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "PropertyPanel.h"
#include "SelectionState.h"
#include <wx/splitter.h>

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
    for (const int command : {wxID_CUT, wxID_COPY, wxID_PASTE, wxID_DELETE})
    {
        Bind(wxEVT_MENU, [canvas](wxCommandEvent& event) {
            canvas->HandleEditCommand(event.GetId());
        }, command);
    }

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
}
