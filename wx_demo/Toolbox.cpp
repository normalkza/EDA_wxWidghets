#include "Toolbox.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>

// 获取 exe 所在目录下的 res 文件夹里的文件路径
namespace
{
    wxString ResPath(const wxString& file)
    {
        wxFileName fn(wxStandardPaths::Get().GetExecutablePath());
        fn.AppendDir("res");
        fn.SetFullName(file);
        return fn.GetFullPath();
    }
}

Toolbox::Toolbox(wxWindow* parent)//构造函数
    : wxPanel(parent, wxID_ANY)//成员初始化列表
{
    this->SetBackgroundColour(wxColour(240, 240, 240));//设置背景色

    m_imageList = new wxImageList(16, 16, true);//创建图标列表
    LoadIcons();//加载所有图标

    // 创建树状图控件
    m_treeCtrl = new wxTreeCtrl(
        this,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HAS_BUTTONS
    );

    m_treeCtrl->AssignImageList(m_imageList);// 绑定图标列表

    BuildTree();// 构建树结构

    // 内部布局：树状图填满整个面板
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);//创建垂直布局管理器
    sizer->Add(m_treeCtrl, 1, wxEXPAND | wxALL, 2);//把树状图加入布局
    this->SetSizer(sizer);//把布局应用到 Toolbox 面板
}


// 加载所有图标到 m_imageList
void Toolbox::LoadIcons()
{
    auto Load = [&](const wxString& name) -> int {
        wxString path = ResPath(wxString::Format(wxT("%s.png"), name));
        wxImage img;
        if (img.LoadFile(path, wxBITMAP_TYPE_PNG)) {
            img.Rescale(16, 16, wxIMAGE_QUALITY_HIGH);
            return m_imageList->Add(wxBitmap(img));
        }
        return -1;
        };


    m_imgFolder = Load("folder");
    m_imgAdder = Load("adder");
    m_imgAnd = Load("and");
    m_imgButton = Load("button");
    m_imgClock = Load("clock");
    m_imgComparator = Load("comparator");
    m_imgConstant = Load("constant");
    m_imgCounter = Load("counter");
    m_imgDelete = Load("delete");
    m_imgDemux = Load("demux");
    m_imgDff = Load("dff");
    m_imgInput = Load("input");
    m_imgLed = Load("led");
    m_imgMux = Load("mux");
    m_imgNand = Load("nand");
    m_imgNor = Load("nor");
    m_imgNot = Load("not");
    m_imgOr = Load("or");
    m_imgOutput = Load("output");
    m_imgPin = Load("pin");
    m_imgProbe = Load("probe");
    m_imgRegister = Load("register");
    m_imgSelect = Load("select");
    m_imgSplitter = Load("splitter");
    m_imgSubtractor = Load("subtractor");
    m_imgText = Load("text");
    m_imgWire = Load("wire");
    m_imgXor = Load("xor");
}

// 构建树结构
void Toolbox::BuildTree()
{
    wxTreeItemId rootId = m_treeCtrl->AddRoot(wxT("电路元件"));

    // 线路
    wxTreeItemId wiringId = m_treeCtrl->AppendItem(rootId, wxT("线路"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(wiringId, wxT("引脚"), m_imgPin, m_imgPin);
    m_treeCtrl->AppendItem(wiringId, wxT("导线"), m_imgWire, m_imgWire);
    m_treeCtrl->AppendItem(wiringId, wxT("分线器"), m_imgSplitter, m_imgSplitter);
    m_treeCtrl->AppendItem(wiringId, wxT("探针"), m_imgProbe, m_imgProbe);
    m_treeCtrl->AppendItem(wiringId, wxT("时钟"), m_imgClock, m_imgClock);
    m_treeCtrl->AppendItem(wiringId, wxT("常量"), m_imgConstant, m_imgConstant);

    // 逻辑门
    wxTreeItemId gatesId = m_treeCtrl->AppendItem(rootId, wxT("逻辑门"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(gatesId, wxT("非门"), m_imgNot, m_imgNot);
    m_treeCtrl->AppendItem(gatesId, wxT("与门"), m_imgAnd, m_imgAnd);
    m_treeCtrl->AppendItem(gatesId, wxT("或门"), m_imgOr, m_imgOr);
    m_treeCtrl->AppendItem(gatesId, wxT("与非门"), m_imgNand, m_imgNand);
    m_treeCtrl->AppendItem(gatesId, wxT("或非门"), m_imgNor, m_imgNor);
    m_treeCtrl->AppendItem(gatesId, wxT("异或门"), m_imgXor, m_imgXor);

    // 复用器
    wxTreeItemId plexersId = m_treeCtrl->AppendItem(rootId, wxT("复用器"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(plexersId, wxT("数据选择器"), m_imgMux, m_imgMux);
    m_treeCtrl->AppendItem(plexersId, wxT("解复用器"), m_imgDemux, m_imgDemux);

    // 运算器
    wxTreeItemId arithmeticId = m_treeCtrl->AppendItem(rootId, wxT("运算器"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(arithmeticId, wxT("加法器"), m_imgAdder, m_imgAdder);
    m_treeCtrl->AppendItem(arithmeticId, wxT("减法器"), m_imgSubtractor, m_imgSubtractor);
    m_treeCtrl->AppendItem(arithmeticId, wxT("比较器"), m_imgComparator, m_imgComparator);

    // 存储
    wxTreeItemId memoryId = m_treeCtrl->AppendItem(rootId, wxT("存储"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(memoryId, wxT("D触发器"), m_imgDff, m_imgDff);
    m_treeCtrl->AppendItem(memoryId, wxT("寄存器"), m_imgRegister, m_imgRegister);
    m_treeCtrl->AppendItem(memoryId, wxT("计数器"), m_imgCounter, m_imgCounter);

    // 输入/输出
    wxTreeItemId ioId = m_treeCtrl->AppendItem(rootId, wxT("输入/输出"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(ioId, wxT("按钮"), m_imgButton, m_imgButton);
    m_treeCtrl->AppendItem(ioId, wxT("发光二极管"), m_imgLed, m_imgLed);
    m_treeCtrl->AppendItem(ioId, wxT("输入引脚"), m_imgInput, m_imgInput);
    m_treeCtrl->AppendItem(ioId, wxT("输出引脚"), m_imgOutput, m_imgOutput);

    // 基本
    wxTreeItemId baseId = m_treeCtrl->AppendItem(rootId, wxT("基本"), m_imgFolder, m_imgFolder);
    m_treeCtrl->AppendItem(baseId, wxT("选择工具"), m_imgSelect, m_imgSelect);
    m_treeCtrl->AppendItem(baseId, wxT("文本工具"), m_imgText, m_imgText);
    m_treeCtrl->AppendItem(baseId, wxT("删除工具"), m_imgDelete, m_imgDelete);


}
