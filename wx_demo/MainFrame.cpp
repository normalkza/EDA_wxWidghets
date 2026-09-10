#include "MainFrame.h"

MainFrame::MainFrame()
    : wxFrame(
        nullptr,
        wxID_ANY,
        "Circuit Editor",
        wxDefaultPosition,
        wxSize(1200, 800)
    )
{
	wxPanel* panel = new wxPanel(this);//这是覆盖整个窗口的面板
	wxPanel* toolbox= new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxSize(200, 800));//左侧工具箱面板
	wxPanel* canvas = new wxPanel(panel);//右侧画布面板,不设置大小,自适应工具箱大小
    
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);//创建sizer管理区域大小
	sizer->Add(toolbox, 0, wxEXPAND);//添加工具箱面板到sizer,不随窗口大小变化
	sizer->Add(canvas, 1, wxEXPAND);//添加画布面板到sizer,随窗口大小变化
	panel->SetSizer(sizer);//设置sizer管理面板大小
}