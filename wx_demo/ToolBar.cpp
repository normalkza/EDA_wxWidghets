
#include "SelectionState.h" 
#include "Toolbox.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>


// 自定义工具 ID
enum {
    ID_TB_SELECT = wxID_HIGHEST + 1,
    ID_TB_WIRE,
    ID_TB_TEXT,
    ID_TB_INPUT,
    ID_TB_OUTPUT,
    ID_TB_AND,
    ID_TB_OR,
    ID_TB_NOT
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

// 创建主工具栏
wxToolBar* CreateMainToolBar(wxFrame* frame)
{
    wxToolBar* toolBar = new wxToolBar(frame, wxID_ANY);
    toolBar->SetToolBitmapSize(wxSize(24, 24));

    wxBitmap selectBitmap(ResPath("select.png"), wxBITMAP_TYPE_PNG);
    wxBitmap wireBitmap(ResPath("wire.png"), wxBITMAP_TYPE_PNG);
    wxBitmap textBitmap(ResPath("text.png"), wxBITMAP_TYPE_PNG);
    wxBitmap inputBitmap(ResPath("input.png"), wxBITMAP_TYPE_PNG);
    wxBitmap outputBitmap(ResPath("output.png"), wxBITMAP_TYPE_PNG);
    wxBitmap andBitmap(ResPath("and.png"), wxBITMAP_TYPE_PNG);
    wxBitmap orBitmap(ResPath("or.png"), wxBITMAP_TYPE_PNG);
    wxBitmap notBitmap(ResPath("not.png"), wxBITMAP_TYPE_PNG);

    toolBar->AddTool(ID_TB_SELECT, "选择", selectBitmap);
    toolBar->AddTool(ID_TB_WIRE, "连线", wireBitmap);
    toolBar->AddTool(ID_TB_TEXT, "文字", textBitmap);
    toolBar->AddTool(ID_TB_INPUT, "输入", inputBitmap);
    toolBar->AddTool(ID_TB_OUTPUT, "输出", outputBitmap);
    toolBar->AddTool(ID_TB_AND, "与门", andBitmap);
    toolBar->AddTool(ID_TB_OR, "或门", orBitmap);
    toolBar->AddTool(ID_TB_NOT, "非门", notBitmap);

    toolBar->AddSeparator();
    toolBar->Realize();

    // 绑定工具按钮点击事件
    frame->Bind(wxEVT_TOOL, [](wxCommandEvent& e) {
        switch (e.GetId()) {
        case ID_TB_SELECT: g_selectedType = "";       break;
        case ID_TB_WIRE:   g_selectedType = "WIRE";   break;
        case ID_TB_TEXT:   g_selectedType = "TEXT";   break;
        case ID_TB_INPUT:  g_selectedType = "INPUT";  break;
        case ID_TB_OUTPUT: g_selectedType = "OUTPUT"; break;
        case ID_TB_AND:    g_selectedType = "AND";    break;
        case ID_TB_OR:     g_selectedType = "OR";     break;
        case ID_TB_NOT:    g_selectedType = "NOT";    break;
        }
        });

    return toolBar;
}
