

#include "Toolbox.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>

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

    toolBar->AddTool(wxID_ANY, "选择", selectBitmap);
    toolBar->AddTool(wxID_ANY, "连线", wireBitmap);
    toolBar->AddTool(wxID_ANY, "文字", textBitmap);
    toolBar->AddTool(wxID_ANY, "输入", inputBitmap);
    toolBar->AddTool(wxID_ANY, "输出", outputBitmap);
    toolBar->AddTool(wxID_ANY, "与门", andBitmap);
    toolBar->AddTool(wxID_ANY, "或门", orBitmap);
    toolBar->AddTool(wxID_ANY, "非门", notBitmap);

    toolBar->AddSeparator();
    toolBar->Realize();

    return toolBar;
}
