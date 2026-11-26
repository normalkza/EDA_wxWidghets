#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/evtloop.h>
#include <wx/filename.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include "VerilogFrame.h"
#include "VerilogWorkspace.h"
#include "VerilogRunner.h"
#include "MainFrame.h"
#include "MenuBar.h"

namespace fs = std::filesystem;
class VerilogTestApp final : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(VerilogTestApp);
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string Read(const fs::path& path)
{ std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); }

// The test executable doubles as deterministic external-tool fixtures. These
// check launch, stream, timeout and diagnostic handling, not HDL compilation.
int FakeTool(int argc, wchar_t** argv)
{
    if (std::wstring(argv[1]) == L"-n")
    {
        std::ofstream("wave.vcd") << "$timescale 1ns $end\n$enddefinitions $end\n#0\n";
        std::cout << "FAKE_SIMULATION_OK\n" << std::flush; return 0;
    }
    fs::path artifact; std::string code;
    for (int i = 1; i < argc; ++i)
    {
        if (std::wstring(argv[i]) == L"-o" && i + 1 < argc) { artifact = argv[++i]; continue; }
        const fs::path file(argv[i]);
        if (file.extension() != L".v" && file.extension() != L".sv") continue;
        const auto data = Read(file); code += data;
        if (data.find("EXPECT_RELATIVE_HEADER") != std::string::npos)
            if (Read(file.parent_path() / L"../include/shared.vh").find("UNSAVED_HEADER") == std::string::npos) return 9;
        if (data.find("SYNTAX_ERROR") != std::string::npos)
        { std::cerr << wxString(file.wstring()).ToStdString(wxConvUTF8) << ":3: syntax error\n" << std::flush; return 2; }
    }
    if (code.find("WAIT_FOREVER") != std::string::npos)
        for (int i = 0; i < 200; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!artifact.empty()) std::ofstream(artifact, std::ios::binary) << code;
    std::cout << "FAKE_COMPILE_OK\n" << std::flush; return 0;
}
template<typename Predicate> bool Until(Predicate predicate)
{
    wxEventLoop loop; wxEventLoopActivator activate(&loop);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do
    {
        wxTheApp->ProcessPendingEvents();
        for (int count = 0; count < 64 && loop.Pending(); ++count) if (!loop.Dispatch()) return false;
        loop.DispatchTimeout(1); if (predicate()) return true; wxMilliSleep(1);
    } while (std::chrono::steady_clock::now() < limit);
    return false;
}
struct VerilogRegressionAccess
{
    static void Editor(wxWindow& owner, const wxString& executable, const wxString& folder)
    {
        auto frame = std::make_unique<VerilogFrame>(&owner); frame->Show();
        auto* document = frame->AddDocument(wxS("中文设计.v"), "module old;\nendmodule\n");
        Require(document && document->editor->GetModify(), "new template/generated code is unsaved");
        document->editor->Colourise(0, -1);
        Require(document->editor->GetLexer() == wxSTC_LEX_VERILOG && document->editor->GetStyleAt(0) == wxSTC_V_WORD,
            "actual native Verilog lexer highlights module keyword");
        wxFindDialogEvent replace(wxEVT_FIND_REPLACE_ALL);
        replace.SetFindString("old"); replace.SetReplaceString("new_name"); replace.SetFlags(wxFR_DOWN);
        frame->FindText(replace);
        Require(document->editor->GetText().Contains("module new_name;"), "replace all edits current source");
        document->editor->Undo(); Require(document->editor->GetText().Contains("module old;"), "replacement is one undo group");
        document->path = wxFileName(folder, wxS("中文设计.v")).GetFullPath();
        Require(frame->Save(*document) && !document->editor->GetModify(), "save marks source clean");
        const auto count = frame->m_documents.size(); wxString error;
        Require(frame->LoadSource(document->path, error) && frame->m_documents.size() == count, "opening same path activates existing tab");
        frame->m_compiler = executable; frame->m_runtime = executable; frame->Build(true);
        Require(Until([&] { return !frame->m_task.valid(); }) && frame->m_lastResult.success && !frame->m_lastResult.vcdPath.empty(),
            "editor completes asynchronous external compile/sim pipeline and retains VCD");
        document->editor->AppendText("\nSYNTAX_ERROR\n"); frame->Build(false);
        Require(Until([&] { return !frame->m_task.valid(); }) && !frame->m_lastResult.success && !frame->m_lastResult.diagnostics.empty(),
            "compiler diagnostic arrives at editor");
        const auto before = frame->m_notebook->GetSelection(); frame->JumpToDiagnostic(0);
        if (frame->m_notebook->GetSelection() != before || document->editor->GetCurrentLine() != 2)
            std::cerr << "diagnostic state: selection=" << frame->m_notebook->GetSelection() << " before=" << before
                << " line=" << document->editor->GetCurrentLine() << " generation=" << frame->m_generation
                << " snapshot=" << frame->m_buildGeneration << " rows=" << frame->m_diagnostics->GetItemCount() << std::endl;
        Require(frame->m_notebook->GetSelection() == before && document->editor->GetCurrentLine() == 2, "diagnostic selects the correct buffer and line");
        document->editor->AppendText("// changed"); frame->JumpToDiagnostic(0);
        Require(frame->m_generation != frame->m_buildGeneration, "edited buffer invalidates diagnostic snapshot");
        for (const auto& item : frame->m_documents) item->editor->SetSavePoint();
        Require(frame->PrepareClose(), "clean workspace closes without discarding prompts"); frame.reset();
    }
};

void RealTools(const wxString& compiler, const wxString& folder)
{
    std::atomic_bool cancelled{false};
    VerilogRunner::Request request;
    request.compiler = compiler; request.runtime = VerilogRunner::FindTool("vvp.exe", compiler);
    Require(wxFileExists(request.compiler) && wxFileExists(request.runtime), "real Icarus/VVP are available");
    request.top = "full_adder_tb"; request.simulate = true;
    request.sources = {{1, "full_adder.v", wxString(), VerilogWorkspace::FullAdder()},
                       {2, "full_adder_tb.v", wxString(), VerilogWorkspace::FullAdderTestbench()}};
    auto result = VerilogRunner::Run(request, cancelled);
    std::cout << result.output.ToStdString(wxConvUTF8) << std::endl;
    Require(result.success && result.output.Contains("PASS: all 8 input combinations") && wxFileExists(result.vcdPath),
        "real Icarus compiles and simulates the shipped full-adder template");
    Require(Read(fs::path(result.vcdPath.ToStdWstring())).find("$enddefinitions") != std::string::npos, "real VCD contains signal definitions");
    CircuitDocument circuit;
    circuit.components = {{1, "INPUT", 20, 40, 0}, {2, "INPUT", 20, 140, 0}, {3, "XOR", 220, 60, 2}, {4, "OUTPUT", 420, 80, 1}};
    circuit.wires = {{1, 0, 3, 0, {}}, {2, 0, 3, 1, {}}, {3, 0, 4, 0, {}}}; wxString generated, error;
    Require(VerilogWorkspace::FromCircuit(circuit, generated, error), "canvas RTL export for real compiler");
    request.top = "canvas_tb";
    request.sources = {{1, "circuit.v", wxString(), generated}, {2, "canvas_tb.v", wxString(),
        "module canvas_tb; reg a,b; wire y; integer i; circuit_top dut(.in_c1(a), .in_c2(b), .out_c4(y));\n"
        "initial begin for(i=0;i<4;i=i+1) begin {a,b}=i; #1; if(y !== (a^b)) $fatal(1, \"canvas mismatch\"); end\n"
        "$display(\"PASS: canvas XOR\"); $finish; end endmodule\n"}};
    result = VerilogRunner::Run(request, cancelled);
    if (!result.success) std::cerr << result.output.ToStdString(wxConvUTF8) << std::endl;
    Require(result.success && result.output.Contains("PASS: canvas XOR"), "real compiler verifies generated canvas connectivity");
    request.top = "dff_tb";
    request.sources = {{1, "dff.v", wxString(), VerilogWorkspace::DffTemplate()}, {2, "dff_tb.v", wxString(),
        "module dff_tb; reg clk=0,rst=0,d=0; wire q; dff dut(clk,rst,d,q);\n"
        "initial begin #1; rst=1; #1; if(q!==0) $fatal(1,\"reset\"); rst=0; d=1; #1; clk=1; #1;\n"
        "if(q!==1) $fatal(1,\"sample\"); $display(\"PASS: DFF reset and sample\"); $finish; end endmodule\n"}};
    result = VerilogRunner::Run(request, cancelled);
    Require(result.success && result.output.Contains("PASS: DFF reset and sample"), "real DFF template reset and sampling");
    request.top = "header_tb";
    const fs::path root(folder.ToStdWstring());
    request.sources = {{1, "header_tb.v", wxString((root / L"project/rtl/header_tb.v").wstring()),
        "`include \"../include/shared.vh\"\nmodule header_tb; initial begin if(`VALUE != 7) $fatal(1,\"header\"); $display(\"PASS: snapshot header\"); $finish; end endmodule\n"},
        {2, "shared.vh", wxString((root / L"project/include/shared.vh").wstring()), "`define VALUE 7\n"}};
    result = VerilogRunner::Run(request, cancelled);
    if (!result.success) std::cerr << result.output.ToStdString(wxConvUTF8) << std::endl;
    Require(result.success && result.output.Contains("PASS: snapshot header"), "real compiler resolves unsaved relative header snapshots");
    request.top.clear(); request.simulate = false;
    request.sources = {{42, "broken.v", wxString(), "module broken;\n assign y = ;\nendmodule\n"}};
    result = VerilogRunner::Run(request, cancelled);
    Require(!result.success && !result.diagnostics.empty() && result.diagnostics[0].sourceId == 42 && result.diagnostics[0].line == 2,
        "real Icarus syntax error maps to source buffer and line");
    std::cout << "PASS: real Icarus/VVP full adder, VCD, canvas RTL, DFF, relative headers and diagnostics." << std::endl;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1 && (std::wstring(argv[1]) == L"-g2012" || std::wstring(argv[1]) == L"-n")) return FakeTool(argc, argv);
    const wxString realCompiler = argc == 3 && std::wstring(argv[1]) == L"--real-tools" ? wxString(argv[2]) : wxString();
    const wxString executable = wxFileName(wxString(argv[0])).GetFullPath();
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    wxTheApp->SetExitOnFrameDelete(false); wxInitAllImageHandlers();
    delete wxLog::SetActiveTarget(new wxLogStderr()); int status = 0;
    try
    {
        auto files = std::make_shared<VerilogRunner::TemporaryWorkspace>();
        const auto source = wxFileName(files->Path(), wxS("设计 & 测试.v")).GetFullPath();
        wxString error, read;
        Require(VerilogWorkspace::SaveUtf8(source, wxS("// 中文注释\nmodule test; endmodule\n"), error) &&
            VerilogWorkspace::ReadUtf8(source, read, error) && read.Contains(wxS("中文注释")), "Unicode path and UTF-8 source roundtrip");
        const auto original = read;
        Require(!VerilogWorkspace::SaveUtf8(source, wxString(std::wstring(L"a\0b", 3)), error) &&
            VerilogWorkspace::ReadUtf8(source, read, error) && read == original, "rejected save leaves original file intact");
        CircuitDocument circuit;
        circuit.components = {{1, "INPUT", 20, 40, 0}, {2, "INPUT", 20, 140, 0}, {3, "XOR", 220, 60, 2}, {4, "OUTPUT", 420, 80, 1}};
        circuit.wires = {{1, 0, 3, 0, {}}, {2, 0, 3, 1, {}}, {3, 0, 4, 0, {}}}; wxString generated;
        Require(VerilogWorkspace::FromCircuit(circuit, generated, error) && generated.Contains("input wire in_c1") &&
            generated.Contains("assign n_c3_o0 = (n_c1_o0 ^ n_c2_o0);") && generated.Contains("assign out_c4 = n_c3_o0;"),
            "canvas export retains ports, named internal nets and connectivity");
        circuit.formatVersion = 3; circuit.components.push_back({5, "CLOCK", 20, 250, 0});
        const auto preserved = generated;
        Require(!VerilogWorkspace::FromCircuit(circuit, generated, error) && generated == preserved, "unsupported RTL export is non-mutating");
        std::atomic_bool cancelled{false}; VerilogRunner::Request request;
        request.compiler = executable; request.runtime = executable; request.sources = {{42, "test.v", wxString(), "module test; endmodule\n"}};
        request.simulate = true; request.timeoutSeconds = 3;
        auto result = VerilogRunner::Run(request, cancelled);
        if (!result.success || result.vcdPath.empty())
            std::cerr << result.output.ToStdString(wxConvUTF8) << std::endl;
        Require(result.success && result.output.Contains("FAKE_SIMULATION_OK") && wxFileExists(result.vcdPath), "native process pipeline and VCD detection");
        const auto work = result.workspace->Path(); result = {};
        Require(!wxDirExists(work), "owned build workspace is cleaned after artifacts are released");
        request.simulate = false; request.sources[0].code = "SYNTAX_ERROR";
        result = VerilogRunner::Run(request, cancelled);
        Require(!result.success && result.exitCode == 2 && result.diagnostics.size() == 1 && result.diagnostics[0].sourceId == 42 &&
            result.diagnostics[0].line == 3, "tool error maps only to this snapshot's buffer");
        const fs::path project(files->Path().ToStdWstring());
        request.sources = {{1, "main.v", wxString((project / L"project/rtl/main.v").wstring()), "// EXPECT_RELATIVE_HEADER\nmodule main; endmodule"},
                           {2, "shared.vh", wxString((project / L"project/include/shared.vh").wstring()), "// UNSAVED_HEADER\n"}};
        Require(VerilogRunner::Run(request, cancelled).success, "relative includes use unsaved sibling header snapshots");
        request.sources = {{1, "wait.v", wxString(), "// WAIT_FOREVER"}}; request.timeoutSeconds = 1;
        result = VerilogRunner::Run(request, cancelled); Require(result.timedOut && !result.success, "runaway tool is stopped by timeout");
        request.timeoutSeconds = 10;
        std::thread stop([&] { std::this_thread::sleep_for(std::chrono::milliseconds(120)); cancelled = true; });
        result = VerilogRunner::Run(request, cancelled); stop.join(); Require(result.cancelled && !result.success, "cancellation terminates the owned process job");
        if (!realCompiler.empty()) RealTools(realCompiler, files->Path());
        auto owner = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Verilog checks", wxDefaultPosition, wxSize(1200, 900));
        owner->Show(); VerilogRegressionAccess::Editor(*owner, executable, files->Path()); owner.reset();
        auto main = std::make_unique<MainFrame>();
        Require(main->GetMenuBar()->FindItem(ID_VERILOG_EDITOR) && main->GetMenuBar()->FindItem(ID_VERILOG_FROM_CANVAS), "main menu exposes code editor and canvas export");
        main.reset(); wxTheApp->ProcessPendingEvents();
        std::cout << "PASS: native Verilog lexer/editor, UTF-8 files, undo/replace, canvas RTL generation, snapshot includes, process pipeline, diagnostics, VCD, timeout, cancellation and menu integration. External tools mocked; real Icarus is optional." << std::endl;
    }
    catch (const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << std::endl; status = 1; }
    wxTheApp->OnExit(); wxEntryCleanup(); return status;
}
