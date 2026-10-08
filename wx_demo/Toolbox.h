#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <functional>
#include <vector>
#include <utility>

class ActiveToolTree;

class Toolbox : public wxPanel
{
public:
    Toolbox(wxWindow* parent);

    // 获取当前选中的元件类型（"AND" / "OR" / "NOT"，空表示未选中）
    wxString GetSelectedType() const { return m_selectedType; }
    void SetSelectionCallback(std::function<void(const wxString&)> cb) { m_selectionCallback = std::move(cb); }
    void SelectTool(const wxString& type);

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
    int m_imgAdder;
    int m_imgAnd;
    int m_imgButton;
    int m_imgClock;
    int m_imgComparator;
    int m_imgConstant;
    int m_imgCounter;
    int m_imgDelete;
    int m_imgDemux;
    int m_imgDff;
    int m_imgInput;
    int m_imgLed;
    int m_imgMux;
    int m_imgNand;
    int m_imgNor;
    int m_imgNot;
    int m_imgOr;
    int m_imgOutput;
    int m_imgPin;
    int m_imgProbe;
    int m_imgRegister;
    int m_imgSelect;
    int m_imgSplitter;
    int m_imgSubtractor;
    int m_imgText;
    int m_imgWire;
    int m_imgXor;

    void LoadIcons();
    void BuildTree();
    void AppendTool(const wxTreeItemId& parent, const wxString& label, int image, const wxString& type);
    void OnTreeSelect(wxTreeEvent& event);
};
