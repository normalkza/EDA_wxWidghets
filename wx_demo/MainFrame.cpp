#include "MainFrame.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "PropertyPanel.h"
MainFrame::MainFrame()
    : wxFrame(
        nullptr,
        wxID_ANY,
        wxT("数字电路编辑器"),
        wxDefaultPosition,
        wxSize(1200, 800)
    )
{
    // MenuBar模块加载  
    SetMenuBar(CreateMainMenuBar());

    // 这一部分是panel函数的初始化,
    // 创建了一个覆盖整个窗口的面板panel,
    // 在panel上创建了子面板anvas,
    // 并使用wxBoxSizer管理它们的布局
    wxPanel* panel = new wxPanel(this);
    Toolbox* toolbox = new Toolbox(panel);
    PropertyPanel* properties = new PropertyPanel(panel);
    wxToolBar* toolBar = CreateMainToolBar(this, [properties](const wxString& type) {
        properties->ShowTool(type);
        });//创建工具栏并同步属性栏
    if (toolBar) this->SetToolBar(toolBar);
    Canvas* canvas = new Canvas(panel);
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->Add(toolbox, 1, wxEXPAND);
    leftSizer->Add(properties, 0, wxEXPAND);

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);//创建sizer管理区域大小
    toolbox->SetMinSize(wxSize(220, -1));
    properties->SetMinSize(wxSize(220, 150));
    sizer->Add(leftSizer, 0, wxEXPAND);//左侧工具箱和属性栏
    sizer->Add(canvas, 1, wxEXPAND);//添加画布面板到sizer,随窗口大小变化
    canvas->SetSelectionCallback([properties](const Component* component, int x, int y) {
        if (component) properties->ShowComponent(component, x, y);
        else properties->Clear();
        });
    toolbox->SetSelectionCallback([properties](const wxString& type) {
        properties->ShowTool(type);
        });
    panel->SetSizer(sizer);//设置sizer管理面板大小

    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);//创建垂直方向布局管理器，控件从上到下排列
    frameSizer->Add(panel, 1, wxEXPAND);//panel加入frameSizer，占满所有空间
    this->SetSizer(frameSizer);//把 frameSizer 正式设置为 MainFrame 的布局管理器
    this->Layout();
}
