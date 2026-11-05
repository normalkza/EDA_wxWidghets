#include "MenuBar.h"

wxMenuBar* CreateMainMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar();

    wxMenu* fileMenu = new wxMenu();
    fileMenu->Append(wxID_NEW, wxT("新建\tCtrl+N"));
    fileMenu->Append(wxID_OPEN, wxT("打开...\tCtrl+O"));
    fileMenu->Append(ID_FILE_IMPORT_KICAD_PCB, wxT("从 KiCad 导入 PCB…"));
    fileMenu->Append(wxID_SAVE, wxT("保存\tCtrl+S"));
    fileMenu->Append(wxID_SAVEAS, wxT("另存为...\tCtrl+Shift+S"));
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, wxT("退出"));
    menuBar->Append(fileMenu, wxT("文件"));

    wxMenu* editMenu = new wxMenu();
    editMenu->Append(wxID_UNDO, wxT("撤销\tCtrl+Z"));
    editMenu->AppendSeparator();
    editMenu->Append(wxID_CUT, wxT("剪切\tCtrl+X"));
    editMenu->Append(wxID_COPY, wxT("复制\tCtrl+C"));
    editMenu->Append(wxID_PASTE, wxT("粘贴\tCtrl+V"));
    editMenu->Append(wxID_DELETE, wxT("删除\tCtrl+D"));
    editMenu->AppendSeparator();
    editMenu->AppendCheckItem(ID_TOOL_SELECT, wxT("选择工具"));
    editMenu->AppendCheckItem(ID_TOOL_DELETE, wxT("删除工具"));
    editMenu->Append(ID_EDIT_DESELECT, wxT("取消选择"));
    menuBar->Append(editMenu, wxT("编辑"));

    auto* viewMenu = new wxMenu();
    viewMenu->AppendCheckItem(ID_VIEW_GRID, wxT("网格"));
    viewMenu->AppendCheckItem(ID_VIEW_SNAP, wxT("吸附到网格"));
    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_VIEW_TOOLBOX, wxT("元件库"));
    viewMenu->AppendCheckItem(ID_VIEW_PROPERTIES, wxT("属性栏"));
    menuBar->Append(viewMenu, wxT("视图"));

    auto* componentMenu = new wxMenu();
    componentMenu->AppendCheckItem(ID_TOOL_INPUT, wxT("输入引脚"));
    componentMenu->AppendCheckItem(ID_TOOL_OUTPUT, wxT("输出引脚"));
    componentMenu->AppendSeparator();
    componentMenu->AppendCheckItem(ID_TOOL_AND, wxT("与门"));
    componentMenu->AppendCheckItem(ID_TOOL_OR, wxT("或门"));
    componentMenu->AppendCheckItem(ID_TOOL_NOT, wxT("非门"));
    componentMenu->AppendCheckItem(ID_TOOL_NAND, wxT("与非门"));
    componentMenu->AppendCheckItem(ID_TOOL_NOR, wxT("或非门"));
    componentMenu->AppendCheckItem(ID_TOOL_XOR, wxT("异或门"));
    componentMenu->AppendSeparator();
    componentMenu->AppendCheckItem(ID_TOOL_CLOCK, wxT("时钟 CLOCK"));
    componentMenu->AppendCheckItem(ID_TOOL_DFF, wxT("D 触发器 DFF"));
    componentMenu->AppendCheckItem(ID_TOOL_PROBE, wxT("探针 PROBE"));
    componentMenu->AppendCheckItem(ID_TOOL_CONSTANT, wxT("常量 CONSTANT"));
    componentMenu->AppendCheckItem(ID_TOOL_BUTTON, wxT("按钮 BUTTON"));
    componentMenu->AppendCheckItem(ID_TOOL_LED, wxT("数字指示灯 LED"));
    componentMenu->Append(ID_COMPONENT_TIMING, wxT("编辑时序参数…"));
    componentMenu->AppendSeparator();
    componentMenu->AppendCheckItem(ID_TOOL_TEXT, wxT("文本工具"));
    componentMenu->AppendSeparator();
    componentMenu->Append(ID_COMPONENT_ROTATE_CW, wxT("顺时针旋转 90°"));
    componentMenu->Append(ID_COMPONENT_ROTATE_CCW, wxT("逆时针旋转 90°"));
    menuBar->Append(componentMenu, wxT("元件"));

    auto* wireMenu = new wxMenu();
    wireMenu->AppendCheckItem(ID_TOOL_WIRE, wxT("连线工具"));
    wireMenu->AppendSeparator();
    wireMenu->Append(ID_WIRE_RESET_ROUTE, wxT("恢复自动走线"));
    wireMenu->Append(ID_WIRE_DELETE, wxT("删除选中线路"));
    menuBar->Append(wireMenu, wxT("连线"));

    auto* simulationMenu = new wxMenu();
    simulationMenu->Append(ID_SIMULATE_RUN, wxT("运行 / 继续\tF8"));
    simulationMenu->Append(ID_SIMULATE_PAUSE, wxT("暂停"));
    simulationMenu->Append(ID_SIMULATE_STOP, wxT("停止"));
    simulationMenu->Append(ID_SIMULATE_EVALUATE, wxT("复位到初值\tF5"));
    simulationMenu->Append(ID_SIMULATE_STEP, wxT("推进下一事件\tF7"));
    simulationMenu->Append(ID_SIMULATE_PERIOD, wxT("推进所选时钟一个周期"));
    simulationMenu->AppendSeparator();
    simulationMenu->Append(ID_SIMULATE_TOGGLE_INPUT, wxT("切换输入电平\tF6"));
    simulationMenu->Append(ID_SIMULATE_EXAMPLE, wxT("载入 DFF 二分频示例"));
    menuBar->Append(simulationMenu, wxT("仿真"));

    auto* helpMenu = new wxMenu();
    helpMenu->Append(wxID_HELP, wxT("快捷键\tF1"));
    helpMenu->AppendSeparator();
    helpMenu->Append(wxID_ABOUT, wxT("关于"));
    menuBar->Append(helpMenu, wxT("帮助"));

    return menuBar;
}
