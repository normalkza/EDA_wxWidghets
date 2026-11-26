#include "VerilogFrame.h"
#include <wx/stc/stc.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/wrapsizer.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/listctrl.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/menu.h>
#include <algorithm>

namespace
{
enum { IdCheck = wxID_HIGHEST + 12000, IdRun, IdStop, IdTools, IdSaveWave, IdCloseTab, IdTemplateModule, IdTemplateAdder,
    IdTemplateDff, IdTemplateBench, IdReplace, IdComplete };
const char* Keywords = "always always_comb always_ff always_latch and assign automatic begin buf case casex casez cell cmos config deassign default defparam design disable edge else end endcase endconfig endfunction endgenerate endmodule endprimitive endspecify endtable endtask event for force forever fork function generate genvar highz0 highz1 if ifnone initial inout input integer join localparam logic macromodule module nand negedge nmos nor not or output parameter pmos posedge primitive pull0 pull1 pulldown pullup real realtime reg release repeat rnmos rpmos rtran signed small specify specparam strong0 strong1 supply0 supply1 table task time tran tri tri0 tri1 triand trior trireg unsigned vectored wait wand weak0 weak1 while wire wor xnor xor";
wxString SettingsPath()
{
    const auto folder = wxStandardPaths::Get().GetUserLocalDataDir();
    return wxFileName(folder, "verilog-tools.ini").GetFullPath();
}
void ConfigureEditor(wxStyledTextCtrl& editor)
{
    editor.SetCodePage(wxSTC_CP_UTF8); editor.SetLexer(wxSTC_LEX_VERILOG); editor.SetKeyWords(0, Keywords);
    editor.StyleSetFont(wxSTC_STYLE_DEFAULT, wxFontInfo(11).FaceName("Consolas"));
    editor.StyleSetForeground(wxSTC_STYLE_DEFAULT, wxColour(36, 50, 71));
    editor.StyleSetBackground(wxSTC_STYLE_DEFAULT, *wxWHITE); editor.StyleClearAll();
    for (const int style : {wxSTC_V_COMMENT, wxSTC_V_COMMENTLINE, wxSTC_V_COMMENTLINEBANG})
        editor.StyleSetForeground(style, wxColour(95, 135, 100));
    editor.StyleSetForeground(wxSTC_V_WORD, wxColour(52, 92, 198)); editor.StyleSetBold(wxSTC_V_WORD, true);
    editor.StyleSetForeground(wxSTC_V_NUMBER, wxColour(151, 83, 177));
    editor.StyleSetForeground(wxSTC_V_STRING, wxColour(161, 91, 42));
    editor.StyleSetForeground(wxSTC_V_PREPROCESSOR, wxColour(166, 65, 111));
    editor.StyleSetForeground(wxSTC_V_OPERATOR, wxColour(40, 90, 120));
    editor.StyleSetBackground(wxSTC_STYLE_BRACELIGHT, wxColour(220, 235, 255));
    editor.SetMarginType(0, wxSTC_MARGIN_NUMBER); editor.SetMarginWidth(0, editor.TextWidth(wxSTC_STYLE_LINENUMBER, " 99999 "));
    editor.StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour(130, 143, 162));
    editor.StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(245, 248, 252));
    editor.SetMarginType(1, wxSTC_MARGIN_SYMBOL); editor.SetMarginMask(1, wxSTC_MASK_FOLDERS);
    editor.SetMarginWidth(1, editor.FromDIP(16)); editor.SetMarginSensitive(1, true);
    editor.SetProperty("fold", "1"); editor.SetProperty("fold.verilog.flags", "0");
    for (const auto marker : {wxSTC_MARKNUM_FOLDER, wxSTC_MARKNUM_FOLDEROPEN})
        editor.MarkerDefine(marker, marker == wxSTC_MARKNUM_FOLDER ? wxSTC_MARK_BOXPLUS : wxSTC_MARK_BOXMINUS,
            wxColour(110, 130, 155), *wxWHITE);
    editor.MarkerDefine(1, wxSTC_MARK_BACKGROUND, wxColour(255, 230, 230), wxColour(255, 230, 230));
    editor.SetTabWidth(4); editor.SetIndent(4); editor.SetUseTabs(false); editor.SetTabIndents(true);
    editor.SetBackSpaceUnIndents(true); editor.SetIndentationGuides(wxSTC_IV_LOOKBOTH);
    editor.SetEOLMode(wxSTC_EOL_LF); editor.SetWrapMode(wxSTC_WRAP_NONE);
    editor.SetCaretLineVisible(true); editor.SetCaretLineBackground(wxColour(247, 250, 255));
    editor.AutoCompSetIgnoreCase(false);
}
}

VerilogFrame::VerilogFrame(wxWindow* parent)
    : wxFrame(parent, wxID_ANY, wxS("Verilog 代码工作区"), wxDefaultPosition, parent->FromDIP(wxSize(1100, 760))), m_timer(this)
{
    SetMinSize(FromDIP(wxSize(760, 550))); SetBackgroundColour(wxColour(243, 246, 251));
    SetFont(wxFontInfo(9).FaceName("Microsoft YaHei UI"));
    auto* menus = new wxMenuBar; auto* file = new wxMenu;
    file->Append(wxID_NEW, wxS("新建代码\tCtrl+N")); file->Append(wxID_OPEN, wxS("打开代码…\tCtrl+O"));
    file->Append(wxID_SAVE, wxS("保存\tCtrl+S")); file->Append(wxID_SAVEAS, wxS("另存为…\tCtrl+Shift+S"));
    file->Append(IdCloseTab, wxS("关闭当前文件\tCtrl+W")); file->AppendSeparator(); file->Append(wxID_EXIT, wxS("关闭工作区"));
    menus->Append(file, wxS("文件"));
    auto* edit = new wxMenu;
    const std::pair<int, const wchar_t*> editItems[] = {
        {wxID_UNDO, L"撤销\tCtrl+Z"}, {wxID_REDO, L"重做\tCtrl+Y"}, {wxID_CUT, L"剪切\tCtrl+X"},
        {wxID_COPY, L"复制\tCtrl+C"}, {wxID_PASTE, L"粘贴\tCtrl+V"}, {wxID_SELECTALL, L"全选\tCtrl+A"},
        {wxID_FIND, L"查找\tCtrl+F"}, {IdReplace, L"替换\tCtrl+H"}, {IdComplete, L"关键字补全\tCtrl+Space"}};
    for (const auto& entry : editItems) edit->Append(entry.first, entry.second);
    menus->Append(edit, wxS("编辑"));
    auto* examples = new wxMenu;
    examples->Append(IdTemplateModule, wxS("组合逻辑模块")); examples->Append(IdTemplateAdder, wxS("全加器与 Testbench"));
    examples->Append(IdTemplateDff, wxS("D 触发器")); examples->Append(IdTemplateBench, wxS("Testbench 模板")); menus->Append(examples, wxS("模板"));
    auto* build = new wxMenu;
    build->Append(IdCheck, wxS("编译检查\tF9")); build->Append(IdRun, wxS("运行仿真\tCtrl+F9")); build->Append(IdStop, wxS("停止\tShift+F9"));
    build->Append(IdSaveWave, wxS("另存 VCD 波形…")); build->AppendSeparator(); build->Append(IdTools, wxS("设置 Icarus 工具…"));
    menus->Append(build, wxS("构建")); SetMenuBar(menus); CreateStatusBar(2);
    auto* root = new wxBoxSizer(wxVERTICAL); auto* commands = new wxWrapSizer(wxHORIZONTAL);
    const auto button = [&](const wxString& label, int id) {
        auto* control = new wxButton(this, id, label); commands->Add(control, 0, wxRIGHT | wxBOTTOM, FromDIP(5)); return control;
    };
    button(wxS("新建"), wxID_NEW); button(wxS("打开"), wxID_OPEN); button(wxS("保存"), wxID_SAVE);
    m_check = button(wxS("编译检查"), IdCheck); m_run = button(wxS("运行仿真"), IdRun); m_stop = button(wxS("停止"), IdStop);
    m_wave = button(wxS("另存波形"), IdSaveWave); m_tools = button(wxS("工具设置"), IdTools);
    root->Add(commands, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    auto* settings = new wxBoxSizer(wxHORIZONTAL);
    settings->Add(new wxStaticText(this, wxID_ANY, wxS("顶层模块")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_top = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(160, -1)));
    m_top->SetHint(wxS("留空自动选择")); settings->Add(m_top, 1, wxRIGHT, FromDIP(12));
    m_scope = new wxChoice(this, wxID_ANY); m_scope->Append(wxS("全部打开的文件")); m_scope->Append(wxS("当前文件及已打开头文件")); m_scope->SetSelection(0);
    settings->Add(m_scope, 0, wxRIGHT, FromDIP(12));
    settings->Add(new wxStaticText(this, wxID_ANY, wxS("超时（秒）")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_timeout = new wxSpinCtrl(this, wxID_ANY, "10", wxDefaultPosition, FromDIP(wxSize(75, -1)), wxSP_ARROW_KEYS, 1, 120, 10);
    settings->Add(m_timeout); root->Add(settings, 0, wxEXPAND | wxALL, FromDIP(10));
    auto* split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    m_notebook = new wxNotebook(split, wxID_ANY); auto* reports = new wxNotebook(split, wxID_ANY);
    m_output = new wxTextCtrl(reports, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_output->SetFont(wxFontInfo(10).FaceName("Consolas"));
    m_diagnostics = new wxListCtrl(reports, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    m_diagnostics->InsertColumn(0, wxS("文件"), wxLIST_FORMAT_LEFT, FromDIP(180));
    m_diagnostics->InsertColumn(1, wxS("行"), wxLIST_FORMAT_LEFT, FromDIP(65));
    m_diagnostics->InsertColumn(2, wxS("诊断（双击定位）"), wxLIST_FORMAT_LEFT, FromDIP(680));
    reports->AddPage(m_output, wxS("构建输出")); reports->AddPage(m_diagnostics, wxS("错误与警告"));
    split->SetMinimumPaneSize(FromDIP(100)); split->SetSashGravity(0.75); split->SplitHorizontally(m_notebook, reports, FromDIP(430));
    split->Bind(wxEVT_SPLITTER_DOUBLECLICKED, [](wxSplitterEvent& event) { event.Veto(); });
    root->Add(split, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)); SetSizer(root);
    wxFileConfig config(wxEmptyString, wxEmptyString, SettingsPath(), wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    config.Read("compiler", &m_compiler); config.Read("runtime", &m_runtime);
    if (!wxFileExists(m_compiler)) m_compiler = VerilogRunner::FindTool("iverilog.exe");
    if (!wxFileExists(m_runtime)) m_runtime = VerilogRunner::FindTool("vvp.exe", m_compiler);
    const auto command = [this](int id) {
        if (id == wxID_NEW) AddDocument(wxString::Format("untitled_%u.v", m_nextId));
        else if (id == wxID_OPEN) ChooseSources();
        else if (id == wxID_SAVE || id == wxID_SAVEAS) { if (auto* current = Current()) Save(*current, id == wxID_SAVEAS); }
        else if (id == IdCloseTab) this->CloseTab(); else if (id == wxID_EXIT) Close();
        else if (id == IdCheck || id == IdRun) Build(id == IdRun); else if (id == IdStop) this->Stop();
        else if (id == IdTools) ConfigureTools(); else if (id == IdSaveWave) this->SaveWave();
        else if (id >= IdTemplateModule && id <= IdTemplateBench) InsertTemplate(id);
        else if (id == wxID_FIND || id == IdReplace) ShowFind(id == IdReplace);
        else if (auto* current = Current())
        {
            auto& editor = *current->editor;
            if (id == wxID_UNDO) editor.Undo(); else if (id == wxID_REDO) editor.Redo();
            else if (id == wxID_CUT) editor.Cut(); else if (id == wxID_COPY) editor.Copy();
            else if (id == wxID_PASTE) editor.Paste(); else if (id == wxID_SELECTALL) editor.SelectAll();
            else if (id == IdComplete) { const int pos = editor.GetCurrentPos(); editor.AutoCompShow(pos - editor.WordStartPosition(pos, true), Keywords); }
        }
    };
    Bind(wxEVT_MENU, [command](wxCommandEvent& event) { command(event.GetId()); });
    Bind(wxEVT_BUTTON, [command](wxCommandEvent& event) { command(event.GetId()); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) { if (event.CanVeto() && !PrepareClose()) event.Veto(); else { Stop(); event.Skip(); } });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); }, m_timer.GetId()); m_timer.Start(75);
    m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) { RefreshTitles(); event.Skip(); });
    m_diagnostics->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) { JumpToDiagnostic(event.GetIndex()); });
    for (const auto type : {wxEVT_FIND, wxEVT_FIND_NEXT, wxEVT_FIND_REPLACE, wxEVT_FIND_REPLACE_ALL}) Bind(type, &VerilogFrame::FindText, this);
    Bind(wxEVT_FIND_CLOSE, [this](wxFindDialogEvent&) { if (m_find.get()) m_find->Destroy(); m_find = nullptr; });
    AddDocument("untitled_1.v");
    Report(wxFileExists(m_compiler) ? wxS("Icarus Verilog 已就绪。F9 检查，Ctrl+F9 仿真。") :
        wxS("代码编辑、模板和保存可以直接使用。编译/仿真需要 Icarus Verilog，请在“工具设置”中选择 iverilog.exe 和 vvp.exe。"));
    CentreOnParent(); UpdateActions();
}

VerilogFrame::~VerilogFrame()
{
    m_timer.Stop(); Stop(); if (m_task.valid()) m_task.wait();
}
VerilogFrame::Document* VerilogFrame::Current() const
{
    const int selected = m_notebook->GetSelection();
    return selected >= 0 && selected < static_cast<int>(m_documents.size()) ? m_documents[selected].get() : nullptr;
}
VerilogFrame::Document* VerilogFrame::Find(unsigned id) const
{ for (const auto& document : m_documents) if (document->id == id) return document.get(); return nullptr; }

VerilogFrame::Document* VerilogFrame::AddDocument(const wxString& name, const wxString& code, const wxString& path)
{
    if (m_documents.size() >= 32) { Report(wxS("最多同时打开 32 个代码文件。")); return nullptr; }
    auto document = std::make_unique<Document>(); document->id = m_nextId++; document->name = name; document->path = path;
    auto* editor = new wxStyledTextCtrl(m_notebook, wxID_ANY); document->editor = editor; ConfigureEditor(*editor);
    auto* result = document.get(); const unsigned id = result->id;
    m_documents.push_back(std::move(document)); m_notebook->AddPage(editor, name, true);
    editor->SetText(code);
    if (!path.empty() || code.empty()) { editor->EmptyUndoBuffer(); editor->SetSavePoint(); }
    editor->Bind(wxEVT_STC_MODIFIED, [this, id](wxStyledTextEvent& event) {
        if ((event.GetModificationType() & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) && Find(id))
        { ++m_generation; RefreshTitles(); }
    });
    editor->Bind(wxEVT_STC_SAVEPOINTLEFT, [this](wxStyledTextEvent&) { RefreshTitles(); });
    editor->Bind(wxEVT_STC_SAVEPOINTREACHED, [this](wxStyledTextEvent&) { RefreshTitles(); });
    editor->Bind(wxEVT_STC_MARGINCLICK, [editor](wxStyledTextEvent& event) { if (event.GetMargin() == 1) editor->ToggleFold(editor->LineFromPosition(event.GetPosition())); });
    editor->Bind(wxEVT_STC_CHARADDED, [editor](wxStyledTextEvent& event) {
        if (event.GetKey() != '\n') return;
        const int line = editor->GetCurrentLine(); if (line <= 0) return;
        auto previous = editor->GetLine(line - 1); previous.Trim().Trim(false);
        int indent = editor->GetLineIndentation(line - 1);
        if (previous.EndsWith("begin")) indent += 4;
        if (previous == "end" || previous == "endcase" || previous == "endmodule")
        { indent = std::max(0, indent - 4); editor->SetLineIndentation(line - 1, indent); }
        editor->SetLineIndentation(line, indent); editor->GotoPos(editor->GetLineIndentPosition(line));
    });
    editor->Bind(wxEVT_STC_UPDATEUI, [this, editor](wxStyledTextEvent&) {
        const int pos = editor->GetCurrentPos();
        int brace = pos > 0 && wxString("()[]{}").Find(static_cast<wxChar>(editor->GetCharAt(pos - 1))) != wxNOT_FOUND ? pos - 1 : -1;
        if (brace >= 0) { const int match = editor->BraceMatch(brace); if (match >= 0) editor->BraceHighlight(brace, match); else editor->BraceBadLight(brace); }
        else editor->BraceHighlight(-1, -1);
        if (Current() && Current()->editor == editor) SetStatusText(wxString::Format(wxS("行 %d，列 %d · UTF-8 · Verilog/SystemVerilog"),
            editor->GetCurrentLine() + 1, editor->GetColumn(pos) + 1), 1);
    });
    ++m_generation; RefreshTitles(); editor->SetFocus(); return result;
}
void VerilogFrame::RefreshTitles()
{
    for (std::size_t i = 0; i < m_documents.size(); ++i)
        m_notebook->SetPageText(i, (m_documents[i]->editor->GetModify() ? "* " : "") + m_documents[i]->name);
    const auto* current = Current(); SetTitle(wxS("Verilog 工作区") + (current ? wxS(" · ") + current->name : wxString()));
    SetStatusText(current ? (current->path.empty() ? wxString(wxS("尚未保存")) : current->path) : wxString());
}
bool VerilogFrame::LoadSource(const wxString& path, wxString& error)
{
    wxFileName file(path); file.MakeAbsolute(); const auto full = file.GetFullPath();
    for (std::size_t i = 0; i < m_documents.size(); ++i)
        if (!m_documents[i]->path.empty() && m_documents[i]->path.CmpNoCase(full) == 0) { m_notebook->SetSelection(i); return true; }
    wxString code; if (!VerilogWorkspace::ReadUtf8(full, code, error)) return false;
    if (!AddDocument(file.GetFullName(), code, full)) { error = wxS("打开的文件过多。"); return false; }
    return true;
}
void VerilogFrame::ChooseSources()
{
    wxFileDialog dialog(this, wxS("打开 Verilog / SystemVerilog"), wxEmptyString, wxEmptyString,
        "Verilog/SystemVerilog (*.v;*.sv;*.vh;*.svh)|*.v;*.sv;*.vh;*.svh|All files (*.*)|*.*", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;
    wxArrayString paths; dialog.GetPaths(paths); wxString error;
    for (const auto& path : paths) if (!LoadSource(path, error)) { wxMessageBox(path + "\n" + error, wxS("无法打开代码"), wxOK | wxICON_ERROR, this); break; }
}
bool VerilogFrame::Save(Document& document, bool saveAs)
{
    wxString path = document.path;
    if (path.empty() || saveAs)
    {
        wxFileDialog dialog(this, wxS("保存 Verilog 代码"), wxFileName(path).GetPath(), document.name,
            "Verilog (*.v)|*.v|SystemVerilog (*.sv)|*.sv|Header (*.vh)|*.vh|All files (*.*)|*.*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() != wxID_OK) return false;
        path = dialog.GetPath(); if (!wxFileName(path).HasExt()) path += dialog.GetFilterIndex() == 1 ? ".sv" : dialog.GetFilterIndex() == 2 ? ".vh" : ".v";
    }
    wxFileName normalized(path); normalized.MakeAbsolute(); path = normalized.GetFullPath();
    for (const auto& other : m_documents)
        if (other.get() != &document && !other->path.empty() && other->path.CmpNoCase(path) == 0)
        { Report(wxS("该路径已在另一个标签中打开，请先关闭或保存那个文件。")); return false; }
    wxString error;
    if (!VerilogWorkspace::SaveUtf8(path, document.editor->GetText(), error))
    { wxMessageBox(error, wxS("保存失败"), wxOK | wxICON_ERROR, this); return false; }
    if (document.path != path) ++m_generation;
    document.path = path; document.name = wxFileName(path).GetFullName(); document.editor->SetSavePoint(); RefreshTitles(); return true;
}
bool VerilogFrame::ConfirmSave(Document& document)
{
    if (!document.editor->GetModify()) return true;
    wxMessageDialog dialog(this, wxS("是否保存代码文件的修改？\n") + document.name, wxS("未保存的 Verilog 代码"), wxYES_NO | wxCANCEL | wxICON_QUESTION);
    const int result = dialog.ShowModal(); return result == wxID_NO || (result == wxID_YES && Save(document));
}
bool VerilogFrame::PrepareClose()
{ for (const auto& document : m_documents) if (!ConfirmSave(*document)) return false; Stop(); return true; }
void VerilogFrame::CloseTab()
{
    const int index = m_notebook->GetSelection(); auto* document = Current();
    if (!document || !ConfirmSave(*document)) return;
    m_documents.erase(m_documents.begin() + index); m_notebook->DeletePage(index); ++m_generation;
    if (m_documents.empty()) AddDocument(wxString::Format("untitled_%u.v", m_nextId)); else RefreshTitles();
}
void VerilogFrame::InsertTemplate(int kind)
{
    if (kind == IdTemplateAdder)
    {
        for (std::size_t i = 0; i < m_documents.size(); ++i)
            if (m_documents[i]->name == "full_adder_tb.v") { m_notebook->SetSelection(i); m_top->SetValue("full_adder_tb"); return; }
        if (m_documents.size() > 30) { Report(wxS("请先关闭部分文件，再载入双文件示例。")); return; }
        AddDocument("full_adder.v", VerilogWorkspace::FullAdder());
        AddDocument("full_adder_tb.v", VerilogWorkspace::FullAdderTestbench()); m_top->SetValue("full_adder_tb");
    }
    else
    {
        const wxString name = wxString::Format(kind == IdTemplateDff ? "dff_%u" : kind == IdTemplateBench ? "testbench_%u" : "module_%u", m_nextId);
        auto code = kind == IdTemplateDff ? VerilogWorkspace::DffTemplate() : kind == IdTemplateBench ? VerilogWorkspace::TestbenchTemplate() : VerilogWorkspace::ModuleTemplate();
        code.Replace(kind == IdTemplateDff ? "module dff" : kind == IdTemplateBench ? "module testbench" : "module top", "module " + name);
        AddDocument(name + ".v", code); m_top->SetValue(name);
    }
}
bool VerilogFrame::AddFromCircuit(const CircuitDocument& document, wxString& error)
{
    wxString code; if (!VerilogWorkspace::FromCircuit(document, code, error)) return false;
    if (!AddDocument(wxString::Format("circuit_%u.v", m_nextId), code)) { error = wxS("打开的文件过多。"); return false; }
    m_top->SetValue("circuit_top"); return true;
}

void VerilogFrame::ConfigureTools()
{
    if (m_task.valid()) return;
    wxDialog dialog(this, wxID_ANY, wxS("Icarus Verilog 工具设置")); auto* layout = new wxBoxSizer(wxVERTICAL);
    const auto field = [&](const wxString& label, const wxString& initial) {
        layout->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
        auto* row = new wxBoxSizer(wxHORIZONTAL); auto* text = new wxTextCtrl(&dialog, wxID_ANY, initial, wxDefaultPosition, FromDIP(wxSize(470, -1)));
        auto* browse = new wxButton(&dialog, wxID_ANY, wxS("选择…")); row->Add(text, 1, wxRIGHT, FromDIP(6)); row->Add(browse);
        layout->Add(row, 0, wxEXPAND | wxALL, FromDIP(12));
        browse->Bind(wxEVT_BUTTON, [&dialog, text, label](wxCommandEvent&) {
            wxFileDialog file(&dialog, label, wxEmptyString, wxEmptyString, "Executable (*.exe)|*.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (file.ShowModal() == wxID_OK) text->SetValue(file.GetPath());
        }); return text;
    };
    auto* compiler = field(wxS("编译器 iverilog.exe"), m_compiler); auto* runtime = field(wxS("仿真器 vvp.exe"), m_runtime);
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12)); dialog.SetSizerAndFit(layout); dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return;
    if (!wxFileExists(compiler->GetValue()) || !wxFileExists(runtime->GetValue()))
    { Report(wxS("工具路径不存在，设置未保存。请选择已安装的 iverilog.exe 和 vvp.exe。")); return; }
    m_compiler = compiler->GetValue(); m_runtime = runtime->GetValue();
    wxLogNull quiet; wxFileName::Mkdir(wxFileName(SettingsPath()).GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileConfig config(wxEmptyString, wxEmptyString, SettingsPath(), wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    config.Write("compiler", m_compiler); config.Write("runtime", m_runtime); config.Flush(); Report(wxS("Icarus 工具路径已更新。"));
}
void VerilogFrame::Build(bool simulate)
{
    if (m_task.valid()) return;
    if (!wxFileExists(m_compiler)) m_compiler = VerilogRunner::FindTool("iverilog.exe");
    if (!wxFileExists(m_runtime)) m_runtime = VerilogRunner::FindTool("vvp.exe", m_compiler);
    if (!wxFileExists(m_compiler) || (simulate && !wxFileExists(m_runtime)))
    { Report(wxS("未找到 Icarus Verilog。请使用“工具设置”选择安装目录中的 iverilog.exe 和 vvp.exe；代码编辑和保存仍可使用。")); m_tools->SetFocus(); return; }
    VerilogRunner::Request request; request.compiler = m_compiler; request.runtime = m_runtime;
    request.top = m_top->GetValue(); request.top.Trim().Trim(false); request.simulate = simulate; request.timeoutSeconds = m_timeout->GetValue();
    if (!VerilogRunner::IsModuleName(request.top)) { Report(wxS("顶层模块名只能包含字母、数字、下划线和 $，且不能以数字开头。")); return; }
    std::size_t sourceBytes = 0;
    for (const auto& document : m_documents)
    {
        if (document->editor->GetText().empty()) continue;
        const auto ext = wxFileName(document->name).GetExt().Lower();
        if (m_scope->GetSelection() == 1 && document.get() != Current() && ext != "vh" && ext != "svh") continue;
        sourceBytes += document->editor->GetTextLength();
        if (sourceBytes > VerilogWorkspace::MaxSourceBytes) { Report(wxS("一次构建的代码总量不能超过 8 MB。")); return; }
        request.sources.push_back({document->id, document->name, document->path, document->editor->GetText()});
        document->editor->MarkerDeleteAll(1);
    }
    m_cancel = std::make_shared<std::atomic_bool>(false); m_buildGeneration = m_generation;
    m_lastResult = {}; m_diagnostics->DeleteAllItems(); Report(simulate ? wxS("正在编译并仿真当前代码快照…") : wxS("正在检查当前代码快照…"));
    const auto cancelled = m_cancel;
    try { m_task = std::async(std::launch::async, [request, cancelled] { return VerilogRunner::Run(request, *cancelled); }); }
    catch (const std::exception& error) { Report(wxS("无法启动构建任务：") + wxString::FromUTF8(error.what())); }
    UpdateActions();
}
void VerilogFrame::Poll()
{
    if (!m_task.valid() || m_task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { m_lastResult = m_task.get(); }
    catch (const std::exception& error) { m_lastResult.output = wxString::FromUTF8(error.what()); }
    const bool stale = m_generation != m_buildGeneration;
    Report(m_lastResult.output + (stale ? wxString(wxS("\n代码已经更改，以上结果对应先前快照；请重新编译。")) : wxString()));
    if (!stale)
        for (std::size_t i = 0; i < m_lastResult.diagnostics.size(); ++i)
        {
            const auto& diagnostic = m_lastResult.diagnostics[i]; const auto* document = Find(diagnostic.sourceId); if (!document) continue;
            const long row = m_diagnostics->InsertItem(m_diagnostics->GetItemCount(), document->name);
            m_diagnostics->SetItem(row, 1, wxString::Format("%d", diagnostic.line)); m_diagnostics->SetItem(row, 2, diagnostic.message);
            m_diagnostics->SetItemData(row, i); document->editor->MarkerAdd(std::min(diagnostic.line - 1, document->editor->GetLineCount() - 1), 1);
        }
    UpdateActions();
}
void VerilogFrame::Stop() { if (m_cancel) m_cancel->store(true); }
void VerilogFrame::UpdateActions()
{
    const bool busy = m_task.valid();
    m_check->Enable(!busy); m_run->Enable(!busy); m_stop->Enable(busy); m_tools->Enable(!busy);
    m_wave->Enable(!busy && !m_lastResult.vcdPath.empty()); m_top->Enable(!busy); m_scope->Enable(!busy); m_timeout->Enable(!busy);
    for (int id : {IdCheck, IdRun, IdTools}) GetMenuBar()->Enable(id, !busy);
    GetMenuBar()->Enable(IdStop, busy); GetMenuBar()->Enable(IdSaveWave, !busy && !m_lastResult.vcdPath.empty());
}
void VerilogFrame::Report(const wxString& text)
{
    m_output->ChangeValue(text.length() <= 262144 ? text : text.Left(240000) + wxS("\n…中间输出已省略…\n") + text.Right(16000));
}
void VerilogFrame::JumpToDiagnostic(long row)
{
    if (row < 0) return;
    if (m_generation != m_buildGeneration) { SetStatusText(wxS("代码已经更改，请重新检查后定位。")); return; }
    const auto index = static_cast<std::size_t>(m_diagnostics->GetItemData(row));
    if (index >= m_lastResult.diagnostics.size()) return;
    const auto& diagnostic = m_lastResult.diagnostics[index];
    for (std::size_t i = 0; i < m_documents.size(); ++i) if (m_documents[i]->id == diagnostic.sourceId)
    { m_notebook->SetSelection(i); m_documents[i]->editor->GotoLine(diagnostic.line - 1); m_documents[i]->editor->SetFocus(); break; }
}
void VerilogFrame::SaveWave()
{
    if (m_lastResult.vcdPath.empty() || m_task.valid()) return;
    wxFileDialog dialog(this, wxS("另存 VCD 波形"), wxEmptyString, "wave.vcd", "VCD (*.vcd)|*.vcd", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    wxString target = dialog.GetPath(); if (!wxFileName(target).HasExt()) target += ".vcd";
    if (!wxCopyFile(m_lastResult.vcdPath, target, true)) Report(wxS("无法保存 VCD，请检查目标目录权限。"));
    else Report(wxS("VCD 已保存：") + target);
}
void VerilogFrame::ShowFind(bool replace)
{
    if (m_find.get()) { m_find->Raise(); return; }
    m_find = new wxFindReplaceDialog(this, &m_findData, replace ? wxS("查找与替换") : wxS("查找"),
        (replace ? wxFR_REPLACEDIALOG : 0) | wxFR_NOUPDOWN);
    m_find->Show();
}
void VerilogFrame::FindText(wxFindDialogEvent& event)
{
    auto* document = Current(); if (!document || event.GetFindString().empty()) return;
    auto& editor = *document->editor;
    const int flags = (event.GetFlags() & wxFR_MATCHCASE ? wxSTC_FIND_MATCHCASE : 0) |
        (event.GetFlags() & wxFR_WHOLEWORD ? wxSTC_FIND_WHOLEWORD : 0); editor.SetSearchFlags(flags);
    if (event.GetEventType() == wxEVT_FIND_REPLACE_ALL)
    {
        int position = 0, count = 0; editor.BeginUndoAction();
        while (position <= editor.GetTextLength() && count < 100000)
        {
            editor.SetTargetStart(position); editor.SetTargetEnd(editor.GetTextLength());
            if (editor.SearchInTarget(event.GetFindString()) < 0) break;
            editor.ReplaceTarget(event.GetReplaceString()); position = editor.GetTargetEnd(); ++count;
        }
        editor.EndUndoAction(); SetStatusText(wxString::Format(wxS("已替换 %d 处。"), count)); return;
    }
    if (event.GetEventType() == wxEVT_FIND_REPLACE && editor.GetSelectedText() == event.GetFindString()) editor.ReplaceSelection(event.GetReplaceString());
    const int start = editor.GetSelectionEnd(); editor.SetTargetStart(start); editor.SetTargetEnd(editor.GetTextLength());
    int found = editor.SearchInTarget(event.GetFindString());
    if (found < 0) { editor.SetTargetStart(0); editor.SetTargetEnd(start); found = editor.SearchInTarget(event.GetFindString()); }
    if (found >= 0) { editor.SetSelection(editor.GetTargetStart(), editor.GetTargetEnd()); editor.EnsureCaretVisible(); }
    else SetStatusText(wxS("未找到指定文本。"));
}
