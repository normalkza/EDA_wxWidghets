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
    SetMenuBar(CreateMainMenuBar());
    SetMinSize(FromDIP(wxSize(800, 600)));
    auto* workspace = new wxSplitterWindow(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    auto* sidebar = new wxSplitterWindow(workspace, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
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
        properties->ShowTool(type, canvas->GetTextFontSize());
    };
    if (auto* toolbar = CreateMainToolBar(this, showTool)) SetToolBar(toolbar);
    toolbox->SetSelectionCallback(showTool);
    canvas->SetComponentPlacedCallback([this, toolbox] {
        toolbox->SelectTool(wxEmptyString);
        UpdateMainToolBarSelection(GetToolBar(), wxEmptyString);
    });
    canvas->SetSelectionCallback([properties, canvas](const Component* component, int x, int y) {
        if (component) properties->ShowComponent(component, x, y);
        else properties->ShowTool(g_selectedType, canvas->GetTextFontSize());
    });
    canvas->SetTextSelectionCallback([properties](const wxRect& rect, int pointSize) {
        properties->ShowTextBox(rect, pointSize);
    });
    properties->SetTextFontSizeCallback([canvas](int pointSize) {
        canvas->SetTextFontSize(pointSize);
    });
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(workspace, 1, wxEXPAND);
    SetSizer(root);
    Layout();
    // 按实际可用高度初始化，让状态区占左栏约 45%。
    sidebar->SetSashPosition(sidebar->GetClientSize().GetHeight() * 55 / 100);
}
