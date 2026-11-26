#include "VerilogWorkspace.h"
#include "CircuitValidation.h"
#include "ComponentRegistry.h"
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/log.h>
#include <map>
#include <set>
#include <sstream>

namespace VerilogWorkspace
{
bool ReadUtf8(const wxString& path, wxString& code, wxString& error)
{
    wxLogNull quiet;
    wxFile file(path, wxFile::read);
    if (!file.IsOpened()) { error = wxS("无法读取文件。"); return false; }
    const auto length = file.Length();
    if (length < 0 || length > static_cast<wxFileOffset>(MaxSourceBytes))
    { error = wxS("代码文件不能超过 8 MB。"); return false; }
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (file.Read(bytes.data(), bytes.size()) != length) { error = wxS("文件未能完整读取。"); return false; }
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
    if (bytes.find('\0') != std::string::npos) { error = wxS("文件含有 NUL 字节，请使用 UTF-8 文本文件。"); return false; }
    const auto decoded = wxString::FromUTF8(bytes.data(), bytes.size());
    if (!bytes.empty() && decoded.empty()) { error = wxS("文件不是有效 UTF-8，请先转换编码。"); return false; }
    code = decoded; error.clear(); return true;
}

bool SaveUtf8(const wxString& path, const wxString& code, wxString& error)
{
    const auto bytes = code.ToUTF8();
    if (bytes.length() > MaxSourceBytes || code.find(wxUniChar(0)) != wxString::npos)
    { error = wxS("代码过大或含有 NUL 字符。"); return false; }
    wxLogNull quiet;
    wxTempFile file;
    if (!file.Open(path) || !file.Write(bytes.data(), bytes.length()) || !file.Commit())
    { error = wxS("保存失败，请检查目标目录权限；原文件不会被部分代码覆盖。"); return false; }
    error.clear(); return true;
}

bool FromCircuit(const CircuitDocument& circuit, wxString& code, wxString& error)
{
    if (!CircuitValidation::Validate(circuit, error)) return false;
    if (circuit.components.empty()) { error = wxS("当前画布没有元件。"); return false; }
    const std::set<std::string> supported{"INPUT", "OUTPUT", "BUTTON", "CONSTANT", "LED", "PROBE",
        "AND", "OR", "NOT", "NAND", "NOR", "XOR", "MUX", "COMPARATOR"};
    std::map<std::pair<int, int>, std::string> inputs;
    const auto signal = [](int id, int pin) { return "n_c" + std::to_string(id) + "_o" + std::to_string(pin); };
    for (const auto& wire : circuit.wires) inputs[{wire.targetId, wire.targetPin}] = signal(wire.sourceId, wire.sourcePin);
    const auto input = [&](int id, int pin) {
        const auto found = inputs.find({id, pin}); return found == inputs.end() ? std::string("1'b0") : found->second;
    };
    std::vector<std::string> ports;
    std::ostringstream declarations, assignments;
    for (const auto& part : circuit.components)
    {
        if (!supported.contains(part.type))
        { error = wxS("当前画布包含尚不支持自动转换的元件：") + wxString::FromUTF8(part.type) +
            wxS("。可在代码编辑器中手动编写时钟、复位和时序逻辑。"); return false; }
        const auto model = ComponentRegistry::Create(part.type, part.inputCount);
        for (std::size_t pin = 0; pin < model->outputs.size(); ++pin)
            declarations << "    wire " << signal(part.id, static_cast<int>(pin)) << ";\n";
        const auto id = std::to_string(part.id);
        const auto out = signal(part.id, 0);
        if (part.type == "INPUT" || part.type == "BUTTON")
        { ports.push_back("input wire in_c" + id); assignments << "    assign " << out << " = in_c" << id << ";\n"; }
        else if (part.type == "OUTPUT" || part.type == "LED" || part.type == "PROBE")
        { ports.push_back("output wire out_c" + id); assignments << "    assign out_c" << id << " = " << input(part.id, 0) << ";\n"; }
        else if (part.type == "CONSTANT") assignments << "    assign " << out << " = 1'b" << (part.high ? '1' : '0') << ";\n";
        else if (part.type == "NOT") assignments << "    assign " << out << " = ~(" << input(part.id, 0) << ");\n";
        else if (part.type == "MUX") assignments << "    assign " << out << " = " << input(part.id, 2) << " ? " << input(part.id, 1) << " : " << input(part.id, 0) << ";\n";
        else if (part.type == "COMPARATOR")
        {
            // The registry names define the comparator's output order.
            for (std::size_t pin = 0; pin < model->outputs.size(); ++pin)
            {
                const auto& name = model->outputs[pin].name;
                const char* op = name == "A>B" ? ">" : name == "A<B" ? "<" : "==";
                assignments << "    assign " << signal(part.id, static_cast<int>(pin)) << " = (" << input(part.id, 0) << " " << op << " " << input(part.id, 1) << ");\n";
            }
        }
        else
        {
            const char* op = part.type == "AND" || part.type == "NAND" ? " & " : part.type == "XOR" ? " ^ " : " | ";
            assignments << "    assign " << out << " = " << (part.type == "NAND" || part.type == "NOR" ? "~(" : "(");
            for (int pin = 0; pin < part.inputCount; ++pin) { if (pin) assignments << op; assignments << input(part.id, pin); }
            assignments << ");\n";
        }
    }
    std::ostringstream result;
    result << "`timescale 1ns/1ps\n\n// Generated from the current combinational circuit.\n";
    result << "// Unconnected inputs are tied to 1'b0; input initial levels become external stimuli.\n";
    if (ports.empty()) result << "module circuit_top;\n";
    else
    {
        result << "module circuit_top (\n";
        for (std::size_t i = 0; i < ports.size(); ++i) result << "    " << ports[i] << (i + 1 == ports.size() ? "\n" : ",\n");
        result << ");\n";
    }
    result << declarations.str() << "\n" << assignments.str() << "endmodule\n";
    code = wxString::FromUTF8(result.str()); error.clear(); return true;
}

wxString ModuleTemplate()
{
    return "`timescale 1ns/1ps\n\nmodule top (\n    input wire a,\n    input wire b,\n    output wire y\n);\n    assign y = a & b;\nendmodule\n";
}
wxString FullAdder()
{
    return "`timescale 1ns/1ps\n\nmodule full_adder (\n    input wire a, b, cin,\n    output wire sum, cout\n);\n    assign sum = a ^ b ^ cin;\n    assign cout = (a & b) | ((a ^ b) & cin);\nendmodule\n";
}
wxString FullAdderTestbench()
{
    return "`timescale 1ns/1ps\n\nmodule full_adder_tb;\n    reg a, b, cin;\n    wire sum, cout;\n    integer i;\n    full_adder dut (.a(a), .b(b), .cin(cin), .sum(sum), .cout(cout));\n\n    initial begin\n        $dumpfile(\"wave.vcd\");\n        $dumpvars(0, full_adder_tb);\n        $display(\"A B CIN | S COUT\");\n        for (i = 0; i < 8; i = i + 1) begin\n            {a, b, cin} = i;\n            #10;\n            $display(\"%b %b %b | %b %b\", a, b, cin, sum, cout);\n            if ({cout, sum} !== (a + {1'b0, b} + cin))\n                $fatal(1, \"Full adder mismatch\");\n        end\n        $display(\"PASS: all 8 input combinations\");\n        $finish;\n    end\nendmodule\n";
}
wxString DffTemplate()
{
    return "`timescale 1ns/1ps\n\nmodule dff (\n    input wire clk,\n    input wire rst,\n    input wire d,\n    output reg q\n);\n    always @(posedge clk or posedge rst) begin\n        if (rst)\n            q <= 1'b0;\n        else\n            q <= d;\n    end\nendmodule\n";
}
wxString TestbenchTemplate()
{
    return "`timescale 1ns/1ps\n\nmodule testbench;\n    reg clk = 1'b0;\n    always #5 clk = ~clk;\n\n    // Instantiate the design and add stimulus here.\n    initial begin\n        $dumpfile(\"wave.vcd\");\n        $dumpvars(0, testbench);\n        #100;\n        $finish;\n    end\nendmodule\n";
}
}
