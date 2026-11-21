#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/evtloop.h>
#include <wx/gauge.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "AiAssistantDialog.h"
#include "AiSession.h"
#include "AiCircuitExecutor.h"
#include "Canvas.h"
#include "CircuitValidation.h"
#include "OfflineAssistant.h"
#include "SimulationSnapshot.h"
#include "StartupDialog.h"

class OfflineTestApp final : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(OfflineTestApp);
void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<typename Predicate> bool Until(Predicate predicate)
{
    wxEventLoop loop; wxEventLoopActivator activate(&loop);
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    do
    {
        wxTheApp->ProcessPendingEvents();
        for (int count = 0; count < 64 && loop.Pending(); ++count) if (!loop.Dispatch()) return false;
        loop.DispatchTimeout(1); if (predicate()) return true; wxMilliSleep(1);
    } while (std::chrono::steady_clock::now() < limit);
    return false;
}
struct OfflineRegressionAccess
{
    static void Startup()
    {
        wxString path; const bool hadPath = wxGetEnv("PATH", &path); wxSetEnv("PATH", "");
        std::string failure;
        {
            StartupDialog dialog;
            wxTimer input(&dialog, wxID_HIGHEST + 8100);
            dialog.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
                input.Stop();
                try
                {
                    Require(dialog.m_key->IsEmpty() && dialog.m_username->IsEmpty(), "offline login uses no credentials");
                    dialog.EnterOffline();
                    Require(dialog.m_ready && !dialog.m_worker.joinable(), "offline entry must not start Python worker");
                    Require(AiSession::Get().IsOffline() && AiSession::Get().Token().empty() &&
                        AiSession::Get().UserName() == wxS("访客"), "guest offline session state");
                    Require(dialog.m_progress->GetValue() == 100 && dialog.m_enter->IsEnabled(), "offline welcome ready");
                }
                catch (const std::exception& exception) { failure = exception.what(); dialog.Cancel(); }
            }, input.GetId());
            input.StartOnce(60);
            Require(dialog.ShowModal() == wxID_OK && failure.empty(), failure.empty() ? "offline welcome did not finish" : failure.c_str());
        }
        if (hadPath) wxSetEnv("PATH", path); else wxUnsetEnv("PATH");
    }
    static void Submit(AiAssistantDialog& dialog, const wxString& question, AiAssistantMode mode = AiAssistantMode::Apply)
    { dialog.m_question->ChangeValue(question); dialog.Submit(mode); }
    static void Cancel(AiAssistantDialog& dialog)
    {
        wxCommandEvent event(wxEVT_BUTTON, dialog.m_cancel->GetId());
        dialog.m_cancel->GetEventHandler()->ProcessEvent(event);
    }
    static AiAssistantState State(const AiAssistantDialog& dialog) { return dialog.m_state; }
    static bool NativeRequest(const AiAssistantDialog& dialog) { return dialog.m_client.IsBusy(); }
    static bool OnlyOffline(const AiAssistantDialog& dialog) { return dialog.m_mode->GetCount() == 1 && dialog.IsOfflineMode(); }
};

void CheckAdder(const wxString& name, int inputs)
{
    CircuitDocument empty;
    auto result = OfflineAssistant::Plan(empty, wxSize(1200, 800), wxS("新建") + name, true);
    Require(result.kind == OfflineAssistant::Result::Kind::Edit, "offline adder generated");
    wxString error; SimulationSnapshot snapshot;
    Require(snapshot.Compile(result.document, 1, error), "adder compiles for real logic evaluation");
    std::vector<int> inputIds, outputIds;
    for (const auto& part : result.document.components)
    {
        if (part.type == "INPUT") inputIds.push_back(part.id);
        if (part.type == "OUTPUT") outputIds.push_back(part.id);
    }
    Require(inputIds.size() == inputs && outputIds.size() == 2, "adder ports");
    for (int mask = 0; mask < (1 << inputs); ++mask)
    {
        int sum = 0;
        for (int index = 0; index < inputs; ++index)
        { const bool high = (mask & (1 << index)) != 0; sum += high; Require(snapshot.SetInput(inputIds[index], high ? LogicValue::High : LogicValue::Low), "set stimulus"); }
        Require(snapshot.Evaluate(), "adder evaluates");
        Require(snapshot.GetValue(outputIds[0], PinType::Input, 0) == ((sum & 1) ? LogicValue::High : LogicValue::Low), "all sum truth-table rows");
        Require(snapshot.GetValue(outputIds[1], PinType::Input, 0) == ((sum >= 2) ? LogicValue::High : LogicValue::Low), "all carry truth-table rows");
    }
    Require(result.document.texts.size() == inputs + 2, "persistent port labels included");
    auto scrambled = result.document;
    for (auto& part : scrambled.components) { part.x += 160; part.y += 80; }
    const auto arranged = OfflineAssistant::Plan(scrambled, wxSize(1200, 800), wxS("整理当前布局"), true);
    Require(arranged.kind == OfflineAssistant::Result::Kind::Edit && arranged.document.components.size() == scrambled.components.size() &&
        arranged.document.wires.size() == scrambled.wires.size(), "layout edits existing circuit without duplication");
    for (std::size_t i = 0; i < scrambled.components.size(); ++i)
        Require(scrambled.components[i].id == arranged.document.components[i].id, "layout retains persistent IDs");
    for (std::size_t a = 0; a < arranged.document.components.size(); ++a)
    {
        const auto bounds = AiCircuitExecutor::ComponentBounds(arranged.document.components[a]);
        Require(wxRect(0, 0, 1200, 800).Contains(bounds), "offline arranged component visible");
        for (std::size_t b = 0; b < a; ++b)
            Require(!bounds.Intersects(AiCircuitExecutor::ComponentBounds(arranged.document.components[b])), "offline arranged components do not overlap");
    }
    const auto labels = OfflineAssistant::Plan(result.document, wxSize(1200, 800), wxS("标注输入输出"), true);
    Require(labels.kind == OfflineAssistant::Result::Kind::Answer, "repeat labeling retains existing labels");
}

int main(int argc, char** argv)
{
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    wxTheApp->SetExitOnFrameDelete(false); wxInitAllImageHandlers();
    delete wxLog::SetActiveTarget(new wxLogStderr()); int result = 0;
    try
    {
        OfflineRegressionAccess::Startup();
        CheckAdder(wxS("半加器"), 2); CheckAdder(wxS("全加器"), 3);
        CircuitDocument empty;
        for (const auto* name : {L"与门", L"或门", L"非门", L"异或门", L"与非门", L"或非门"})
            Require(OfflineAssistant::Plan(empty, wxSize(1000, 700), wxString(wxS("新建")) + name, true).kind ==
                OfflineAssistant::Result::Kind::Edit, "all basic gate templates available");
        Require(OfflineAssistant::Plan(empty, wxSize(80, 80), wxS("新建全加器"), true).kind ==
            OfflineAssistant::Result::Kind::Error, "unusable viewport rejected before editing");
        auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Offline regression", wxDefaultPosition, wxSize(1300, 900));
        auto* canvas = new Canvas(frame.get()); auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(canvas, 1, wxEXPAND); frame->SetSizer(layout); frame->Show(); frame->Layout();
        auto dialog = std::make_unique<AiAssistantDialog>(frame.get(), *canvas);
        Require(OfflineRegressionAccess::OnlyOffline(*dialog), "offline login defaults to local tools");
        OfflineRegressionAccess::Submit(*dialog, wxS("全加器"), AiAssistantMode::Guide);
        Require(Until([&] { return !dialog->IsBusy(); }) && canvas->ExportDocument().components.empty(), "guide does not edit");
        OfflineRegressionAccess::Submit(*dialog, wxS("直接新建"));
        Require(Until([&] { return !dialog->IsBusy(); }) && OfflineRegressionAccess::State(*dialog) == AiAssistantState::Applied &&
            canvas->ExportDocument().components.size() == 10 && !OfflineRegressionAccess::NativeRequest(*dialog), "followup creates full adder without HTTP");
        Require(canvas->Undo() && canvas->ExportDocument().components.empty(), "one undo restores before offline operation");
        OfflineRegressionAccess::Submit(*dialog, wxS("新建半加器")); OfflineRegressionAccess::Cancel(*dialog);
        Require(Until([&] { return !dialog->IsBusy(); }) && canvas->ExportDocument().components.empty() &&
            OfflineRegressionAccess::State(*dialog) == AiAssistantState::Cancelled, "cancel queued local edit");
        OfflineRegressionAccess::Submit(*dialog, wxS("新建半加器"));
        auto manual = empty.simulation; ++manual.stopTimeNs; wxString error;
        Require(canvas->SetSimulationConfig(manual, error), "manual edit before local result");
        Require(Until([&] { return !dialog->IsBusy(); }) && canvas->ExportDocument().components.empty() &&
            OfflineRegressionAccess::State(*dialog) == AiAssistantState::Failed, "stale local edit rejected");
        OfflineRegressionAccess::Submit(*dialog, wxS("帮我写一首诗"));
        Require(Until([&] { return !dialog->IsBusy(); }) && OfflineRegressionAccess::State(*dialog) == AiAssistantState::Answered &&
            !OfflineRegressionAccess::NativeRequest(*dialog), "unsupported local request does not fall back to API");
        dialog.reset(); frame.reset(); wxTheApp->ProcessPendingEvents();
        std::cout << "PASS: keyless guest startup without Python, offline welcome, templates, adder truth tables, existing layout and labels, guide/followup, grouped undo, cancellation, stale edits and no API fallback" << std::endl;
    }
    catch (const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << std::endl; result = 1; }
    AiSession::Get().Stop(); wxTheApp->OnExit(); wxEntryCleanup(); return result;
}
