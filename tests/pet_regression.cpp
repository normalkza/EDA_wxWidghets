#include <wx/wx.h>
#include <wx/regex.h>
#include <wx/evtloop.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include "MainFrame.h"
#include "PetWebView.h"
#include "AiAssistantDialog.h"
#include "Canvas.h"
#include "SelectionState.h"

class PetRegressionApp final : public wxApp
{
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(PetRegressionApp);

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<typename Predicate>
bool Until(Predicate predicate, int milliseconds = 30000)
{
    wxEventLoop loop;
    wxEventLoopActivator activate(&loop);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do
    {
        wxTheApp->ProcessPendingEvents();
        // Dispatch pending native/COM messages without throttling startup to
        // one message per 10 ms. Keep the batch bounded for the deadline.
        for (int dispatched = 0; dispatched < 64 && loop.Pending(); ++dispatched)
            if (!loop.Dispatch()) return false;
        loop.DispatchTimeout(1);
        if (predicate()) return true;
        wxMilliSleep(1);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
template<typename Window>
Window* FindChild(wxWindow& parent)
{
    for (auto* child : parent.GetChildren())
    {
        if (auto* match = dynamic_cast<Window*>(child)) return match;
        if (auto* match = FindChild<Window>(*child)) return match;
    }
    return nullptr;
}
wxString Snapshot(PetWebView& pet)
{
    auto result = std::make_shared<std::optional<wxString>>();
    pet.ReadDiagnostics([result](const wxString& json) { *result = json; });
    Require(Until([&] { return result->has_value(); }, 5000), "native diagnostics callback timed out");
    return **result;
}
unsigned long Number(const wxString& json, const wxString& field)
{
    wxRegEx expression("\"" + field + "\"[ ]*:[ ]*([0-9]+)");
    unsigned long value = 0;
    Require(expression.Matches(json) && expression.GetMatch(json, 1).ToULong(&value), "missing numeric model diagnostic");
    return value;
}
}

struct AiPetRegressionAccess
{
    static void Run(wxWindow& parent, Canvas& canvas)
    {
        auto dialog = std::make_unique<AiAssistantDialog>(&parent, canvas);
        std::vector<AiAssistantState> states;
        dialog->SetStateCallback([&](AiAssistantState state) { states.push_back(state); });
        Require(states.back() == AiAssistantState::Idle, "new observer receives idle state");
        dialog->ReportError(wxS("测试错误，无网络调用"));
        Require(states.back() == AiAssistantState::Failed, "real error reporting signals failure");
        AiAssistantDialog::Pending pending;
        pending.serial = 42;
        pending.mode = AiAssistantMode::Guide;
        dialog->m_pending = pending;
        dialog->NotifyState(AiAssistantState::Working);
        AiAssistantReply reply;
        reply.kind = AiAssistantReply::Kind::Text;
        reply.text = wxS("测试步骤");
        dialog->OnReply(42, std::move(reply));
        Require(states.back() == AiAssistantState::Answered, "completed guide signals answer");
        pending.serial = 43;
        dialog->m_pending = pending;
        reply.kind = AiAssistantReply::Kind::Cancelled;
        dialog->OnReply(43, std::move(reply));
        Require(states.back() == AiAssistantState::Cancelled, "cancelled reply is distinct from success");
        const auto events = states.size();
        reply.kind = AiAssistantReply::Kind::Text;
        dialog->OnReply(43, std::move(reply));
        Require(states.size() == events, "stale reply does not signal false completion");
        dialog->SetStateCallback({});
    }
};

int main(int argc, char** argv)
{
    if (!wxEntryStart(argc, argv) || !wxTheApp->CallOnInit()) return 2;
    wxTheApp->SetExitOnFrameDelete(false);
    delete wxLog::SetActiveTarget(new wxLogStderr());
    wxInitAllImageHandlers();
    int result = 0;
    try
    {
        for (const auto* command : {"open-ai", "start-tutorial", "show-help", "show-shortcuts", "hide-pet", "reload-pet"})
            Require(PetWebView::IsAllowedAction(command), "expected native action is reachable");
        for (const auto* command : {"", "OPEN-AI", " open-ai", "open-ai\n", "run", "https://example.com", "{\"id\":1}"})
            Require(!PetWebView::IsAllowedAction(command), "unrecognized action must be rejected");
        auto frame = std::make_unique<MainFrame>();
        frame->Show();
        frame->ShowCompanion();
        auto* pet = FindChild<PetWebView>(*frame);
        Require(pet && pet->IsShown() && !PetWebView::FindResourceDirectory().empty(),
            "workspace companion opens directly from the host with local resources");
        auto* workspace = frame->GetCanvas();
        const auto requireWorkspaceBounds = [&] {
            pet->SyncBounds();
            const wxRect expected(workspace->ClientToScreen(wxPoint()), workspace->GetClientSize());
            Require(pet->GetScreenRect() == expected,
                "transparent companion tracks the exact canvas client rectangle");
        };
        requireWorkspaceBounds();
#ifdef __WXMSW__
        const auto style = pet->GetWindowStyleFlag();
        Require((style & wxCAPTION) == 0 && (style & wxSTAY_ON_TOP) == 0,
            "workspace companion has no title bar or global always-on-top style");
        Require((style & (wxBORDER_SIMPLE | wxBORDER_SUNKEN | wxBORDER_RAISED |
            wxBORDER_STATIC | wxBORDER_THEME)) == 0, "workspace companion has no visible frame border");
#endif
        pet->SetStatusCallback([](const wxString& message) { std::cerr << message.ToStdString(wxConvUTF8) << '\n'; });
        const bool ready = Until([&] { return pet->IsReady(); }, 45000);
        if (!ready) std::cerr << Snapshot(*pet).ToStdString(wxConvUTF8) << '\n';
        Require(ready, "Live2D model did not become ready");
        const auto initial = Snapshot(*pet);
        Require(initial.Contains("\"ready\":true") && Number(initial, "parameterCount") > 0 &&
            Number(initial, "drawableCount") > 0, "ready means a real loaded model with geometry and parameters");
        const auto before = Number(initial, "frameCount");
        Require(Until([&] { return Number(Snapshot(*pet), "frameCount") > before + 2; }),
            "real Cubism update frames continue after initial rendering");
        pet->SetPetState("thinking");
        Require(Until([&] { return Snapshot(*pet).Contains("\"phase\":\"thinking\""); }),
            "AI working state reaches the original model client");
        requireWorkspaceBounds();
        Require(pet->IsReady(), "canvas bound synchronization preserves the running renderer");
        pet->SetPetState("done");
        Require(Until([&] { return Snapshot(*pet).Contains("\"phase\":\"done\""); }),
            "completed state reaches the original model client");
        pet->SetPageVisible(false);
        pet->Hide();
        Require(Until([&] { return Snapshot(*pet).Contains("\"paused\":true"); }), "hidden companion pauses animation");
        const auto stopped = Number(Snapshot(*pet), "frameCount");
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        Until([&] { return std::chrono::steady_clock::now() >= until; }, 1000);
        Require(Number(Snapshot(*pet), "frameCount") <= stopped + 1, "hidden renderer does not consume animation frames");
        pet->SyncBounds();
        pet->Show();
        pet->SetPageVisible(true);
        Require(Until([&] {
            const auto state = Snapshot(*pet);
            return state.Contains("\"paused\":false") && Number(state, "frameCount") > stopped + 1;
        }), "showing the same workspace companion resumes animation");
        AiPetRegressionAccess::Run(*frame, *frame->GetCanvas());
        // Destroy a second renderer immediately, while its asynchronous
        // environment/controller creation can still be pending.
        auto pending = std::make_unique<PetWebView>(frame.get(), frame->GetCanvas());
        pending.reset();
        wxTheApp->ProcessPendingEvents();
        frame.reset();
        wxTheApp->ProcessPendingEvents();
        g_selectedType.Clear();
        std::cout << "PASS: native WebView2, real Live2D geometry and frames, transparent workspace bounds, original-client state bridge, hidden pause/resume, AI outcome observation and pending shutdown\n";
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << '\n';
        result = 1;
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
