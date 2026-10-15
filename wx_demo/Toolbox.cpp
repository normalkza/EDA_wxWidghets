#include "Toolbox.h"
#include "SelectionState.h"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <commctrl.h>
#endif

// 活动工具独立于键盘焦点；画布获得焦点后仍然保留粗边框。
class ActiveToolTree : public wxTreeCtrl
{
public:
    explicit ActiveToolTree(wxWindow* parent)
        : wxTreeCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxBORDER_NONE)
    {
        SetBackgroundColour(*wxWHITE);
        SetForegroundColour(wxColour(36, 50, 71));
        SetFont(wxFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI"))));
        SetIndent(FromDIP(16));
        SetSpacing(FromDIP(6));
    }

    void SetActiveItem(const wxTreeItemId& item)
    {
        if (m_activeItem == item) return;
        if (m_activeItem.IsOk())
        {
            SetItemBold(m_activeItem, false);
            SetItemBackgroundColour(m_activeItem, GetBackgroundColour());
            SetItemTextColour(m_activeItem, GetForegroundColour());
        }
        m_activeItem = item;
        if (item.IsOk())
        {
            SetItemBold(item, true);
            SetItemBackgroundColour(item, wxColour(235, 243, 255));
            SetItemTextColour(item, wxColour(53, 120, 229));
        }
        Refresh(false);
    }

#ifdef __WXMSW__
    bool MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM* result) override
    {
        const bool handled = wxTreeCtrl::MSWOnNotify(idCtrl, lParam, result);
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->code != NM_CUSTOMDRAW) return handled;
        const auto* draw = reinterpret_cast<const NMTVCUSTOMDRAW*>(lParam);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
        {
            *result |= CDRF_NOTIFYPOSTPAINT;
            return true;
        }
        if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT && m_activeItem.IsOk())
        {
            wxRect rect;
            if (GetBoundingRect(m_activeItem, rect, true))
            {
                rect.Inflate(FromDIP(4), 0);
                const HDC hdc = draw->nmcd.hdc;
                const int saved = SaveDC(hdc);
                const HPEN pen = CreatePen(PS_SOLID, FromDIP(2), RGB(53, 120, 229));
                SelectObject(hdc, pen);
                SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(hdc, rect.x, rect.y + 1, rect.GetRight() + 1, rect.GetBottom());
                RestoreDC(hdc, saved);
                DeleteObject(pen);
            }
            *result = CDRF_DODEFAULT;
            return true;
        }
        return handled;
    }
#endif

private:
    wxTreeItemId m_activeItem;
};

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

Toolbox::Toolbox(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(*wxWHITE);
    SetForegroundColour(wxColour(36, 50, 71));
    SetFont(wxFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI"))));

    m_imageList = new wxImageList(16, 16, true);
    LoadIcons();

    m_treeCtrl = new ActiveToolTree(this);

    m_treeCtrl->AssignImageList(m_imageList);
    BuildTree();

    // 绑定树节点选中事件
    m_treeCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &Toolbox::OnTreeSelect, this);
    SelectTool(wxEmptyString);

    auto* title = new wxStaticText(this, wxID_ANY, wxT("元件库"));
    title->SetFont(wxFont(wxFontInfo(10).FaceName(wxT("Microsoft YaHei UI")).Bold()));
    auto* divider = new wxPanel(this, wxID_ANY,
        wxDefaultPosition, wxSize(-1, FromDIP(1)));
    divider->SetBackgroundColour(wxColour(220, 228, 239));

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(title, 0, wxALL, FromDIP(10));
    sizer->Add(divider, 0, wxEXPAND);
    sizer->Add(m_treeCtrl, 1, wxEXPAND | wxALL, FromDIP(8));
    this->SetSizer(sizer);
}

void Toolbox::SelectTool(const wxString& type)
{
    m_selectedType = type;
    wxTreeItemId item;
    for (const auto& tool : m_tools)
    {
        if (tool.first != type) continue;
        if (!item.IsOk()) item = tool.second;
        if (tool.second == m_treeCtrl->GetSelection())
        {
            item = tool.second;
            break;
        }
    }
    m_treeCtrl->SetActiveItem(item);
    if (!item.IsOk()) return;
    m_syncingSelection = true;
    if (m_treeCtrl->GetSelection() != item) m_treeCtrl->SelectItem(item);
    m_treeCtrl->EnsureVisible(item);
    m_syncingSelection = false;
}

void Toolbox::OnTreeSelect(wxTreeEvent& event)
{
    if (m_syncingSelection) return;
    const wxTreeItemId item = event.GetItem();
    for (const auto& tool : m_tools)
    {
        if (tool.second != item) continue;
        g_selectedType = tool.first;
        SelectTool(g_selectedType);
        if (m_selectionCallback) m_selectionCallback(g_selectedType);
        return;
    }
    // 浏览分类时保留当前工具，只有点击工具节点才切换模式。
}

void Toolbox::AppendTool(const wxTreeItemId& parent, const wxString& label,
    int image, const wxString& type)
{
    m_tools.emplace_back(type, m_treeCtrl->AppendItem(parent, label, image, image));
}
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

void Toolbox::BuildTree()
{
    wxTreeItemId rootId = m_treeCtrl->AddRoot(wxT("电路元件"));
    auto appendCategory = [this, rootId](const wxString& label) {
        const wxTreeItemId item = m_treeCtrl->AppendItem(rootId, label, m_imgFolder, m_imgFolder);
        m_treeCtrl->SetItemBold(item, true);
        m_treeCtrl->SetItemTextColour(item, wxColour(100, 116, 139));
        return item;
    };

    // 常用工具优先展示；分类节点仍然只负责浏览，不改变当前绘图模式。
    wxTreeItemId baseId = appendCategory(wxT("编辑工具"));
    AppendTool(baseId, wxT("选择工具"), m_imgSelect, "");
    AppendTool(baseId, wxT("文本工具"), m_imgText, "TEXT");
    AppendTool(baseId, wxT("删除工具"), m_imgDelete, "DELETE");

    wxTreeItemId ioId = appendCategory(wxT("输入 / 输出"));
    AppendTool(ioId, wxT("输入引脚"), m_imgInput, "INPUT");
    AppendTool(ioId, wxT("输出引脚"), m_imgOutput, "OUTPUT");
    AppendTool(ioId, wxT("按钮"), m_imgButton, "BUTTON");
    AppendTool(ioId, wxT("发光二极管"), m_imgLed, "LED");

    wxTreeItemId gatesId = appendCategory(wxT("逻辑门"));
    AppendTool(gatesId, wxT("与门"), m_imgAnd, "AND");
    AppendTool(gatesId, wxT("或门"), m_imgOr, "OR");
    AppendTool(gatesId, wxT("非门"), m_imgNot, "NOT");
    AppendTool(gatesId, wxT("与非门"), m_imgNand, "NAND");
    AppendTool(gatesId, wxT("或非门"), m_imgNor, "NOR");
    AppendTool(gatesId, wxT("异或门"), m_imgXor, "XOR");

    // 线路
    wxTreeItemId wiringId = appendCategory(wxT("线路"));
    AppendTool(wiringId, wxT("引脚"), m_imgPin, "INPUT");
    AppendTool(wiringId, wxT("导线"), m_imgWire, "WIRE");
    AppendTool(wiringId, wxT("分线器"), m_imgSplitter, "SPLITTER");
    AppendTool(wiringId, wxT("探针"), m_imgProbe, "PROBE");
    AppendTool(wiringId, wxT("时钟"), m_imgClock, "CLOCK");
    AppendTool(wiringId, wxT("常量"), m_imgConstant, "CONSTANT");

    // 复用器
    wxTreeItemId plexersId = appendCategory(wxT("复用器"));
    AppendTool(plexersId, wxT("数据选择器"), m_imgMux, "MUX");
    AppendTool(plexersId, wxT("解复用器"), m_imgDemux, "DEMUX");

    // 运算器
    wxTreeItemId arithmeticId = appendCategory(wxT("运算器"));
    AppendTool(arithmeticId, wxT("加法器"), m_imgAdder, "ADDER");
    AppendTool(arithmeticId, wxT("减法器"), m_imgSubtractor, "SUBTRACTOR");
    AppendTool(arithmeticId, wxT("比较器"), m_imgComparator, "COMPARATOR");

    // 存储
    wxTreeItemId memoryId = appendCategory(wxT("存储"));
    AppendTool(memoryId, wxT("D触发器"), m_imgDff, "DFF");
    AppendTool(memoryId, wxT("寄存器"), m_imgRegister, "REGISTER");
    AppendTool(memoryId, wxT("计数器"), m_imgCounter, "COUNTER");

    m_treeCtrl->Expand(baseId);
    m_treeCtrl->Expand(ioId);
    m_treeCtrl->Expand(gatesId);
}
