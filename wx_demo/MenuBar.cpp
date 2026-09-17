#include "MenuBar.h"

// 菜单栏
// 菜单栏是一个三级结构,本身是一个wxMenuBar对象,它包含多个wxMenu对象,每个wxMenu对象包含多个wxMenuItem对象。
wxMenuBar* CreateMainMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append(wxID_NEW, wxT("新建"));
    fileMenu->Append(wxID_OPEN, wxT("打开"));
    fileMenu->Append(wxID_SAVE, wxT("保存"));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, wxT("退出"));
    menuBar->Append(fileMenu, wxT("文件"));

    wxMenu* editMenu = new wxMenu();
    editMenu->Append(wxID_UNDO, wxT("撤销"));
    editMenu->AppendSeparator();
    editMenu->Append(wxID_DELETE, wxT("删除"));   
    menuBar->Append(editMenu, wxT("编辑"));

    wxMenu* drawMenu = new wxMenu();
    drawMenu->Append(wxID_ANY, wxT("选择"));
    drawMenu->Append(wxID_ANY, wxT("导线"));
    menuBar->Append(drawMenu, wxT("绘图"));

    return menuBar;
}