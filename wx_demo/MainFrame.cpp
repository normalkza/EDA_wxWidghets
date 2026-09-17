#include "MainFrame.h"
#include "MenuBar.h"
#include "ToolBar.h"

MainFrame::MainFrame()
    : wxFrame(
        nullptr,
        wxID_ANY,
        wxT("数字电路编辑器"),
        wxDefaultPosition,
        wxSize(1200, 800)
    )
{
    // MenuBar和ToolBar模块加载   
    SetMenuBar(CreateMainMenuBar());
    CreateMainToolBar(this);
    
    // 这一部分是panel函数的初始化,
    // 创建了一个覆盖整个窗口的面板panel,
    // 在panel上创建了两个子面板toolbar和canvas,
    // 并使用wxBoxSizer管理它们的布局
    wxPanel* panel = new wxPanel(this);//这是覆盖整个窗口的面板
    wxPanel* toolbox = new wxPanel(panel);//左侧工具箱面板
    wxPanel* canvas = new wxPanel(panel);//右侧画布面板,不设置大小,自适应工具箱大小

    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);//创建sizer管理区域大小

    toolbox->SetMinSize(wxSize(220, -1));
    sizer->Add(toolbox, 0, wxEXPAND);//添加工具箱面板到sizer,工具箱在水平方向上不参与剩余空间的比例分配。
    sizer->Add(canvas, 1, wxEXPAND);//添加画布面板到sizer,随窗口大小变化
    panel->SetSizer(sizer);//设置sizer管理面板大小
}