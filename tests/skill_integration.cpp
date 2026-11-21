#include <wx/wx.h>
#include <wx/evtloop.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "AiAssistantDialog.h"
#include "AiSession.h"
#include "Canvas.h"

class SkillIntegrationApp final : public wxApp { public: bool OnInit() override { return true; } };
wxIMPLEMENT_APP_NO_MAIN(SkillIntegrationApp);
void Require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<typename Predicate> bool Until(Predicate predicate, int milliseconds = 8000)
{
    wxEventLoop loop; wxEventLoopActivator activate(&loop);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do
    {
        wxTheApp->ProcessPendingEvents();
        for (int count = 0; count < 64 && loop.Pending(); ++count) if (!loop.Dispatch()) return false;
        loop.DispatchTimeout(1);
        if (predicate()) return true;
        wxMilliSleep(1);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
struct AiSkillRegressionAccess
{
    static void Configure(int port)
    {
        AiSession::Get().m_endpoint = wxString::Format("http://127.0.0.1:%d/assistant/turn", port);
        AiSession::Get().m_token = "native-skill-test-token";
    }
    static void Submit(AiAssistantDialog& dialog, const wxString& text)
    { dialog.m_question->ChangeValue(text); dialog.Submit(AiAssistantMode::Apply); }
    static void Cancel(AiAssistantDialog& dialog)
    {
        wxCommandEvent event(wxEVT_BUTTON, dialog.m_cancel->GetId());
        event.SetEventObject(dialog.m_cancel); dialog.m_cancel->GetEventHandler()->ProcessEvent(event);
    }
    static AiAssistantState State(const AiAssistantDialog& dialog) { return dialog.m_state; }
    static std::string History(const AiAssistantDialog& dialog) { return dialog.m_transcript.ToStdString(wxConvUTF8); }
};

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const int port = std::stoi(argv[1]); if (port <= 0 || port > 65535) return 2;
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    wxTheApp->SetExitOnFrameDelete(false); wxInitAllImageHandlers();
    delete wxLog::SetActiveTarget(new wxLogStderr());
    int result = 0;
    try
    {
        AiSkillRegressionAccess::Configure(port);
        auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Canvas skill integration", wxDefaultPosition, wxSize(1100, 800));
        auto* canvas = new Canvas(frame.get()); auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(canvas, 1, wxEXPAND); frame->SetSizer(layout); frame->Show(); frame->Layout();
        CircuitDocument base; base.formatVersion = 3;
        base.components = {{2, "INPUT", 80, 100, 0}, {7, "OUTPUT", 660, 100, 1}};
        base.wires = {{2, 0, 7, 0, {}}}; wxString error;
        Require(canvas->ImportDocument(base, error), "fixture import failed");
        auto dialog = std::make_unique<AiAssistantDialog>(frame.get(), *canvas);
        const auto complete = [&] {
            Require(Until([&] { return !dialog->IsBusy(); }), "assistant loop did not finish");
            if (AiSkillRegressionAccess::State(*dialog) != AiAssistantState::Applied)
                throw std::runtime_error(AiSkillRegressionAccess::History(*dialog));
        };
        AiSkillRegressionAccess::Submit(*dialog, "skill-layout"); complete();
        auto edited = canvas->ExportDocument();
        Require(edited.components.size() == 2 && edited.components[0].x == 180 && edited.wires.size() == 1 &&
            edited.texts.size() == 1 && edited.texts[0].text == "A", "edit did not change original object and add label");
        AiSkillRegressionAccess::Submit(*dialog, "skill-repair"); complete();
        Require(canvas->ExportDocument().components[0].x == 240 && canvas->ExportDocument().texts.size() == 1,
            "validation feedback did not repair the layout");
        Require(canvas->Undo() && canvas->ExportDocument().components[0].x == 180, "repair undo lost previous request");
        Require(canvas->Undo() && canvas->ExportDocument().components[0].x == 80 && canvas->ExportDocument().texts.empty() &&
            !canvas->CanUndo(), "one undo per request did not restore original state");
        AiSkillRegressionAccess::Submit(*dialog, "skill-stale");
        bool changed = false; auto began = std::chrono::steady_clock::now();
        Require(Until([&] {
            if (!changed && std::chrono::steady_clock::now() - began > std::chrono::milliseconds(80))
            {
                auto settings = canvas->ExportDocument().simulation; ++settings.stopTimeNs;
                Require(canvas->SetSimulationConfig(settings, error), "manual edit failed"); changed = true;
            }
            return !dialog->IsBusy();
        }), "stale test timed out");
        Require(changed && canvas->ExportDocument().components[0].x == 80 &&
            AiSkillRegressionAccess::State(*dialog) == AiAssistantState::Failed, "stale result overwrote manual edit");
        AiSkillRegressionAccess::Submit(*dialog, "skill-cancel"); began = std::chrono::steady_clock::now(); bool cancelled = false;
        Require(Until([&] {
            if (!cancelled && std::chrono::steady_clock::now() - began > std::chrono::milliseconds(80))
            { cancelled = true; AiSkillRegressionAccess::Cancel(*dialog); }
            return !dialog->IsBusy();
        }), "cancellation did not complete");
        Require(cancelled && canvas->ExportDocument().components[0].x == 80 &&
            AiSkillRegressionAccess::State(*dialog) == AiAssistantState::Cancelled, "cancelled result changed canvas");
        dialog.reset(); frame.reset(); wxTheApp->ProcessPendingEvents();
        std::cout << "PASS: canvas + PNG -> native HTTP -> skill -> edit existing object -> fresh visual review; invalid layout repaired; undo grouped; stale and cancelled edits rejected\n";
    }
    catch (const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; result = 1; }
    AiSession::Get().Stop(); wxTheApp->OnExit(); wxEntryCleanup(); return result;
}
