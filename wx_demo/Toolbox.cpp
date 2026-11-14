#include "Toolbox.h"
#include "SelectionState.h"
#include "ToolBar.h"
#include "ComponentRegistry.h"

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
    m_treeCtrl->Bind(wxEVT_TREE_SEL_CHANGING, [this](wxTreeEvent& event) {
        for (const auto& tool : m_tools)
            if (tool.second == event.GetItem() && !IsToolAvailable(tool.first))
            {
                event.Veto();
                return;
            }
    });
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
    if (!IsToolAvailable(type)) return;
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

bool Toolbox::IsToolAvailable(const wxString& type) const
{
    if (type.IsEmpty() || type == "TEXT" || type == "DELETE" || type == "WIRE") return true;
    const auto* definition = ComponentRegistry::Find(type.ToStdString(wxConvUTF8));
    return definition && definition->IsAvailable();
}

void Toolbox::OnTreeSelect(wxTreeEvent& event)
{
    if (m_syncingSelection) return;
    const wxTreeItemId item = event.GetItem();
    for (const auto& tool : m_tools)
    {
        if (tool.second != item) continue;
        if (!IsToolAvailable(tool.first)) return;
        g_selectedType = tool.first;
        SelectTool(g_selectedType);
        if (m_selectionCallback) m_selectionCallback(g_selectedType);
        return;
    }
    // 浏览分类时保留当前工具，只有点击工具节点才切换模式。
}

void Toolbox::AppendTool(const wxTreeItemId& parent, const wxString& label,
    int image, const wxString& type, bool available)
{
    const auto item = m_treeCtrl->AppendItem(parent,
        available ? label : label + wxT("（待实现）"), image, image);
    if (!available) m_treeCtrl->SetItemTextColour(item, wxColour(148, 156, 169));
    m_tools.emplace_back(type, item);
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
    m_imgDelete = Load("delete");
    m_imgSelect = Load("select");
    m_imgText = Load("text");
    m_imgWire = Load("wire");
    for (const auto& definition : ComponentRegistry::All())
    {
        const int image = definition.type == "INPUT" || definition.type == "OUTPUT" ?
            m_imageList->Add(CreatePortToolBitmap(wxSize(16, 16), definition.type == "INPUT")) :
            Load(wxString::FromUTF8(definition.icon.data(), definition.icon.size()));
        m_componentIcons.emplace(std::string(definition.type), image);
    }
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

    std::unordered_map<std::string, wxTreeItemId> categories;
    for (const auto& definition : ComponentRegistry::All())
    {
        const std::string category(definition.category);
        auto [position, inserted] = categories.try_emplace(category);
        if (inserted)
        {
            position->second = appendCategory(wxString::FromUTF8(category));
            if (category == "线路") AppendTool(position->second, wxT("导线"), m_imgWire, "WIRE");
        }
        AppendTool(position->second,
            wxString::FromUTF8(definition.displayName.data(), definition.displayName.size()),
            m_componentIcons.at(std::string(definition.type)),
            wxString::FromUTF8(definition.type.data(), definition.type.size()), definition.IsAvailable());
    }
    m_treeCtrl->Expand(baseId);
    m_treeCtrl->Expand(categories.at("输入 / 输出"));
    m_treeCtrl->Expand(categories.at("逻辑门"));

    
}
wxRect Toolbox::GetToolRect(const wxString& type)
{
    for (const auto& tool : m_tools)
    {
        if (tool.first != type)
            continue;

        wxTreeItemId parent = m_treeCtrl->GetItemParent(tool.second);

        if (parent.IsOk())
            m_treeCtrl->Expand(parent);

        m_treeCtrl->EnsureVisible(tool.second);

        wxRect rect;

        if (m_treeCtrl->GetBoundingRect(tool.second, rect, true))
        {
            wxPoint screenPos =
                m_treeCtrl->ClientToScreen(rect.GetPosition());

            return wxRect(screenPos, rect.GetSize());
        }
    }

    return wxRect();
}
