#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <functional>
#include <vector>
#include <utility>
#include <string>
#include <unordered_map>

class ActiveToolTree;

class Toolbox : public wxPanel
{
public:
    Toolbox(wxWindow* parent);

    // 获取当前选中的元件类型（"AND" / "OR" / "NOT"，空表示未选中）
    wxString GetSelectedType() const { return m_selectedType; }
    void SetSelectionCallback(std::function<void(const wxString&)> cb) { m_selectionCallback = std::move(cb); }
    void SelectTool(const wxString& type);
    bool IsToolAvailable(const wxString& type) const;

private:
    ActiveToolTree* m_treeCtrl;
    wxImageList* m_imageList;

    // 当前选中的元件类型
    wxString m_selectedType;
    std::function<void(const wxString&)> m_selectionCallback;
    std::vector<std::pair<wxString, wxTreeItemId>> m_tools;
    bool m_syncingSelection = false;

    // 图标索引
    int m_imgFolder;
    int m_imgDelete;
    int m_imgSelect;
    int m_imgText;
    int m_imgWire;
    std::unordered_map<std::string, int> m_componentIcons;

    void LoadIcons();
    void BuildTree();
    void AppendTool(const wxTreeItemId& parent, const wxString& label, int image, const wxString& type,
        bool available = true);
    void OnTreeSelect(wxTreeEvent& event);
};
