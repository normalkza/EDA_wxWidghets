#pragma once

#include "PcbFrame.h"
#include <wx/checklst.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/log.h>
#include <memory>
#include <stdexcept>

struct PcbWorkspaceRegressionAccess
{
    static void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(std::string("PCB workspace: ") + message);
    }
    static std::string Read(const wxString& path)
    {
        wxFile file(path);
        Require(file.IsOpened(), "open test file");
        std::string bytes(static_cast<std::size_t>(file.Length()), '\0');
        Require(file.Read(bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()), "read exact bytes");
        return bytes;
    }
    static void Write(const wxString& path, const std::string& bytes)
    {
        wxFile file(path, wxFile::write);
        Require(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size() && file.Close(), "write fixture");
    }
    static void Finish(PcbFrame& frame)
    {
        Require(frame.m_pending.valid(), "background load was scheduled");
        frame.m_pending.wait(); frame.Poll();
        Require(!frame.m_pending.valid(), "UI consumes finished worker result");
    }
    static void ChangeDetected(PcbFrame& frame)
    {
        // Two stable one-second observations debounce KiCad's save/rename cycle.
        for (int i = 0; i < 12 && !frame.m_pending.valid(); ++i) frame.Poll();
        Require(frame.m_pending.valid(), "a saved source change schedules refresh");
        Finish(frame);
    }
    static void Run()
    {
        wxLogNull quiet;
        wxFileName sample(wxFileName(wxString::FromUTF8(__FILE__)).GetPath() +
            wxT("/../examples/kicad_import_demo.kicad_pcb"));
        sample.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
        const auto original = Read(sample.GetFullPath());
        const wxString source = wxFileName::CreateTempFileName("wx_demo_pcb_source_");
        const wxString copied = wxFileName::CreateTempFileName("wx_demo_pcb_copy_");
        const wxString invalid = wxFileName::CreateTempFileName("wx_demo_pcb_invalid_");
        struct Cleanup
        {
            std::vector<wxString> paths;
            ~Cleanup() { for (const auto& path : paths) if (wxFileExists(path)) wxRemoveFile(path); }
        } cleanup{{source, copied, invalid}};
        Write(source, original); Write(invalid, "(kicad_pcb");
        auto frame = std::make_unique<PcbFrame>(nullptr);
        Require(!frame->m_document && !frame->m_reloadButton->IsEnabled(), "empty workspace controls");
        Require(frame->LoadBoard(source) && !frame->LoadBoard(source), "only one asynchronous import runs at a time");
        Require(!frame->m_importButton->IsEnabled(), "loading disables conflicting commands");
        Finish(*frame);
        Require(frame->m_document && frame->m_canvas->GetDocument() == frame->m_document &&
            frame->m_document->sourcePath == source, "successful load commits the owned board and canvas together");
        Require(frame->m_document->footprints.size() == 2 && frame->m_layers->GetCount() == frame->m_layerNames.size() &&
            frame->m_nets->GetItemCount() > 0 && frame->m_footprints->GetItemCount() == 2,
            "layers, networks and footprints populate the workspace");
        Require(frame->m_reloadButton->IsEnabled() && frame->m_copyButton->IsEnabled(), "ready commands enabled");
        Require(Read(source) == original, "import never writes source bytes");
        wxString error;
        Require(!frame->WriteCopy(source, error) && Read(source) == original, "copy cannot overwrite the managed source");
        Require(frame->WriteCopy(copied, error) && Read(copied) == original, "snapshot preserves all source bytes");
        frame->SelectNet(0);
        Require(frame->m_inspector->GetValue().Contains(wxT("未连接")), "unconnected net zero has explicit semantics");
        PcbSelection pad; pad.kind = PcbSelection::Kind::Pad; pad.index = 0; pad.subIndex = 0;
        frame->ShowSelection(pad);
        Require(frame->m_inspector->GetValue().Contains(wxT("焊盘")) && frame->m_inspector->GetValue().Contains("mm"),
            "pad inspector reports real millimetre geometry");
        frame->m_netSearch->ChangeValue("__network_that_does_not_exist__"); frame->PopulateNets();
        Require(frame->m_nets->GetItemCount() == 0, "network filter narrows results");
        frame->m_netSearch->ChangeValue(""); frame->PopulateNets();
        frame->m_footprintSearch->ChangeValue("__missing_footprint__"); frame->PopulateFootprints();
        Require(frame->m_footprints->GetItemCount() == 0, "footprint search filters reference, value and library");
        frame->m_footprintSearch->ChangeValue(""); frame->PopulateFootprints();
        frame->m_canvas->SetLayerVisible("F.Cu", false);
        frame->m_canvas->Zoom(1.5);
        const auto viewBefore = frame->m_canvas->ScreenToWorld(wxPoint(150, 130));
        const auto retained = frame->m_document;
        Require(frame->LoadBoard(invalid, true), "invalid replacement can be tested without a modal dialog");
        Finish(*frame);
        Require(frame->m_document == retained && frame->m_canvas->GetDocument() == retained &&
            frame->m_copyButton->IsEnabled() && frame->m_failedStamp.empty(),
            "failed import preserves the last usable board and cannot poison another source's refresh stamp");
        Write(source, original + "\n\n");
        ChangeDetected(*frame);
        Require(frame->m_document != retained && frame->m_document->originalSource == original + "\n\n",
            "saved KiCad changes refresh the board after debounce");
        const auto viewAfter = frame->m_canvas->ScreenToWorld(wxPoint(150, 130));
        Require(!frame->m_canvas->IsLayerVisible("F.Cu") && std::abs(viewBefore.m_x - viewAfter.m_x) < 0.000001 &&
            std::abs(viewBefore.m_y - viewAfter.m_y) < 0.000001, "same-source refresh preserves layer filtering and the viewport");
        const auto valid = frame->m_document;
        Write(source, "(kicad_pcb");
        ChangeDetected(*frame);
        Require(frame->m_document == valid, "an incomplete external save cannot destroy the current view");
        Write(source, original + "\n\n\n");
        ChangeDetected(*frame);
        Require(frame->m_document->originalSource == original + "\n\n\n", "refresh recovers after a later valid save");
        frame->m_autoRefresh->SetValue(false);
        Write(source, original + "\n\n\n\n");
        for (int i = 0; i < 20; ++i) frame->Poll();
        Require(!frame->m_pending.valid() && frame->m_document->originalSource == original + "\n\n\n",
            "disabling auto refresh leaves the imported snapshot stable");
        Require(frame->WriteCopy(copied, error) && Read(copied) == original + "\n\n\n" &&
            Read(source) == original + "\n\n\n\n", "copy exports the loaded snapshot and preserves newer source changes");
    }
};
