
#include "SelectionState.h" 
#include "ToolBar.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <vector>


// 自定义工具 ID
enum {
    ID_TB_SELECT = wxID_HIGHEST + 1,
    ID_TB_WIRE,
    ID_TB_TEXT,
    ID_TB_INPUT,
    ID_TB_OUTPUT,
    ID_TB_AND,
    ID_TB_OR,
    ID_TB_NOT,
    ID_TB_NAND,
    ID_TB_NOR,
    ID_TB_XOR,
    ID_TB_DELETE
};

//用一个函数来获取资源文件的路径
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

// 同时保留原生按下状态和显式的粗边框，失去焦点后仍能识别当前工具。
class ActiveToolBar : public wxToolBar
{
public:
    explicit ActiveToolBar(wxFrame* frame)
        : wxToolBar(frame, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT | wxTB_NODIVIDER)
    {
        SetBackgroundColour(*wxWHITE);
        SetForegroundColour(wxColour(36, 50, 71));
        SetFont(wxFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI"))));
        SetToolBitmapSize(FromDIP(wxSize(32, 32)));
        SetMargins(FromDIP(10), FromDIP(7));
        SetToolPacking(FromDIP(4));
        SetToolSeparation(FromDIP(12));
    }

    void AddChoice(int id, const wxString& label, const wxString& file,
        const wxString& type)
    {
        wxImage image;
        const bool loaded = image.LoadFile(ResPath(file), wxBITMAP_TYPE_PNG);
        const wxSize bitmapSize = GetToolBitmapSize();
        const int iconSize = FromDIP(24);
        const int inset = (bitmapSize.x - iconSize) / 2;
        if (loaded) image.Rescale(iconSize, iconSize, wxIMAGE_QUALITY_HIGH);
        wxBitmap normal(bitmapSize);
        {
            wxMemoryDC dc(normal);
            dc.SetBackground(wxBrush(GetBackgroundColour()));
            dc.Clear();
            if (loaded) dc.DrawBitmap(wxBitmap(image), inset, inset, true);
        }
        wxBitmap active(bitmapSize);
        {
            wxMemoryDC dc(active);
            dc.SetBackground(wxBrush(GetBackgroundColour()));
            dc.Clear();
            dc.SetPen(wxPen(wxColour(53, 120, 229), FromDIP(2)));
            dc.SetBrush(wxBrush(wxColour(235, 243, 255)));
            const int border = FromDIP(2);
            dc.DrawRoundedRectangle(border, border,
                bitmapSize.x - border * 2, bitmapSize.y - border * 2, FromDIP(5));
            if (loaded) dc.DrawBitmap(wxBitmap(image), inset, inset, true);
        }
        AddCheckTool(id, label, normal, wxNullBitmap, label);
        m_choices.push_back({id, type, normal, active});
    }

    void SelectTool(const wxString& type)
    {
        for (const auto& choice : m_choices)
        {
            const bool selected = choice.type == type;
            // 点击 check tool 会先切换原生状态，所以每次重新统一状态及位图。
            ToggleTool(choice.id, selected);
            SetToolNormalBitmap(choice.id, selected ? choice.active : choice.normal);
        }
        Refresh(false);
    }

private:
    struct Choice
    {
        int id;
        wxString type;
        wxBitmap normal;
        wxBitmap active;
    };
    std::vector<Choice> m_choices;
};

void UpdateMainToolBarSelection(wxToolBar* toolbar, const wxString& type)
{
    if (toolbar) static_cast<ActiveToolBar*>(toolbar)->SelectTool(type);
}

wxToolBar* CreateMainToolBar(wxFrame* frame, std::function<void(const wxString&)> selectionCallback)
{
    auto* toolBar = new ActiveToolBar(frame);
    toolBar->AddChoice(ID_TB_SELECT, wxT("选择"), "select.png", "");
    toolBar->AddChoice(ID_TB_WIRE, wxT("连线"), "wire.png", "WIRE");
    toolBar->AddChoice(ID_TB_TEXT, wxT("文字"), "text.png", "TEXT");
    toolBar->AddSeparator();
    toolBar->AddChoice(ID_TB_INPUT, wxT("输入"), "input.png", "INPUT");
    toolBar->AddChoice(ID_TB_OUTPUT, wxT("输出"), "output.png", "OUTPUT");
    toolBar->AddSeparator();
    toolBar->AddChoice(ID_TB_AND, wxT("与门"), "and.png", "AND");
    toolBar->AddChoice(ID_TB_OR, wxT("或门"), "or.png", "OR");
    toolBar->AddChoice(ID_TB_NOT, wxT("非门"), "not.png", "NOT");
    toolBar->AddChoice(ID_TB_NAND, wxT("与非"), "nand.png", "NAND");
    toolBar->AddChoice(ID_TB_NOR, wxT("或非"), "nor.png", "NOR");
    toolBar->AddChoice(ID_TB_XOR, wxT("异或"), "xor.png", "XOR");
    toolBar->AddSeparator();
    toolBar->AddChoice(ID_TB_DELETE, wxT("删除"), "delete.png", "DELETE");
    toolBar->Realize();
    toolBar->SelectTool(g_selectedType);

    frame->Bind(wxEVT_TOOL, [toolBar, selectionCallback](wxCommandEvent& e) {
        switch (e.GetId()) {
        case ID_TB_SELECT: g_selectedType = "";       break;
        case ID_TB_WIRE:   g_selectedType = "WIRE";   break;
        case ID_TB_TEXT:   g_selectedType = "TEXT";   break;
        case ID_TB_INPUT:  g_selectedType = "INPUT";  break;
        case ID_TB_OUTPUT: g_selectedType = "OUTPUT"; break;
        case ID_TB_AND:    g_selectedType = "AND";    break;
        case ID_TB_OR:     g_selectedType = "OR";     break;
        case ID_TB_NOT:    g_selectedType = "NOT";    break;
        case ID_TB_NAND:   g_selectedType = "NAND";   break;
        case ID_TB_NOR:    g_selectedType = "NOR";    break;
        case ID_TB_XOR:    g_selectedType = "XOR";    break;
        case ID_TB_DELETE: g_selectedType = "DELETE"; break;
        default: e.Skip(); return;
        }
        toolBar->SelectTool(g_selectedType);
        if (selectionCallback) selectionCallback(g_selectedType);
    });
    return toolBar;
}
