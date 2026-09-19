#include "MainFrame.h"
#include "MenuBar.h"
#include "ToolBar.h"
#include "Toolbox.h"

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

    wxToolBar* toolBar = CreateMainToolBar(this);//创建工具栏
    if (toolBar) {
        this->SetToolBar(toolBar);//使toolbar停靠在窗口顶部，避免挤压下方树状图
    }

    // 这一部分是panel函数的初始化,
    // 创建了一个覆盖整个窗口的面板panel,
    // 在panel上创建了子面板anvas,
    // 并使用wxBoxSizer管理它们的布局
    wxPanel* panel = new wxPanel(this);
    wxPanel* canvas = new wxPanel(panel);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);//创建sizer管理区域大小

    Toolbox* toolbox = new Toolbox(panel);//内部还有各种内容，故设置为自定义类

    toolbox->SetMinSize(wxSize(220, -1));
    sizer->Add(toolbox, 0, wxEXPAND);//添加工具箱面板到sizer,工具箱在水平方向上不参与剩余空间的比例分配。
    sizer->Add(canvas, 1, wxEXPAND);//添加画布面板到sizer,随窗口大小变化
    panel->SetSizer(sizer);//设置sizer管理面板大小

    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);//创建垂直方向布局管理器，控件从上到下排列
    frameSizer->Add(panel, 1, wxEXPAND);//panel加入frameSizer，占满所有空间
    this->SetSizer(frameSizer);//把 frameSizer 正式设置为 MainFrame 的布局管理器
}
