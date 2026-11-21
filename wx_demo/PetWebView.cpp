#include "PetWebView.h"

#include <wx/app.h>
#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/weakref.h>
#include <wx/msw/wrapwin.h>
#include <windowsx.h>
#include <dcomp.h>
#include <wrl.h>
#include "../桌宠源文件/third_party/webview2/include/WebView2.h"

#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

#pragma comment(lib, "dcomp.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t PageUrl[] = L"https://wx-demo-pet.local/index.html?v=floating-2";
constexpr wchar_t LocalRuntimeScript[] =
    L"Object.defineProperty(window,'__wxDemoPetRuntime',{value:Object.freeze({"
    L"coreUrl:'https://wx-demo-runtime.local/live2dcubismcore.min.js',"
    L"vendorUrl:'https://wx-demo-pet.local/vendor.js'}),writable:false,configurable:false});";
constexpr wchar_t CdnRuntimeScript[] =
    L"Object.defineProperty(window,'__wxDemoPetRuntime',{value:Object.freeze({"
    L"coreUrl:'https://cubism.live2d.com/sdk-web/cubismcore/live2dcubismcore.min.js',"
    L"vendorUrl:'https://wx-demo-pet.local/vendor.js'}),writable:false,configurable:false});";
using Clock = std::chrono::steady_clock;

bool ValidState(const wxString& state)
{
    return state == "idle" || state == "thinking" || state == "done" ||
        state == "error" || state == "cancelled";
}

wxString NormalDirectory(const wxString& value)
{
    wxFileName directory = wxFileName::DirName(value);
    directory.Normalize(wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS);
    return directory.GetPath();
}

wxString ApplicationDataDirectory()
{
    wxString local;
    if (!wxGetEnv("LOCALAPPDATA", &local) || local.empty()) return wxEmptyString;
    return NormalDirectory(local + "\\wx_demo");
}

void LogStage(const char* stage, HRESULT result = S_OK)
{
    // 诊断日志不可影响程序。旧日志不可写时改用当前进程独立的临时日志。
    static bool primaryUnavailable = false, fallbackUnavailable = false;
    if (primaryUnavailable && fallbackUnavailable) return;
    wxLogNull quiet;
    const wxString line = wxDateTime::Now().FormatISOCombined(' ') + " " + wxString::FromUTF8(stage) +
        wxString::Format(" hr=0x%08lX\n", static_cast<unsigned long>(result));
    const auto append = [&line](const wxString& filename) {
        wxFFile file(filename, "ab");
        return file.IsOpened() && file.Write(line, wxConvUTF8) && file.Close();
    };
    if (!primaryUnavailable)
    {
        const wxString directory = ApplicationDataDirectory();
        if (!directory.empty() && wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
            append(wxFileName(directory, "pet-plugin.log").GetFullPath())) return;
        primaryUnavailable = true;
    }
    if (fallbackUnavailable) return;
    const wxString filename = wxFileName(wxStandardPaths::Get().GetTempDir(),
        wxString::Format("wx-demo-pet-%lu.log", static_cast<unsigned long>(GetCurrentProcessId()))).GetFullPath();
    fallbackUnavailable = !append(filename);
}

bool TrustedPage(LPCWSTR source)
{
    return source && wxString(source) == PageUrl;
}

COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS MouseKeys(unsigned buttons = 0)
{
    unsigned keys = buttons;
    if (GetKeyState(VK_CONTROL) < 0) keys |= MK_CONTROL;
    if (GetKeyState(VK_SHIFT) < 0) keys |= MK_SHIFT;
    return static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(keys);
}
}

struct PetWebView::State
{
    wxWeakRef<PetWebView> owner;
    wxWeakRef<wxWindow> mainWindow, workspace;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2CompositionController> composition;
    ComPtr<ICoreWebView2> web;
    ComPtr<IDCompositionDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    EventRegistrationToken messageToken{}, navigationToken{}, frameNavigationToken{}, completedToken{};
    EventRegistrationToken windowToken{}, cursorToken{}, processToken{}, permissionToken{};
    bool messageBound = false, navigationBound = false, frameNavigationBound = false;
    bool completedBound = false, windowBound = false;
    bool cursorBound = false, processBound = false, permissionBound = false;
    std::function<void(const wxString&)> action, status;
    wxString phase = "idle", statusText;
    wxString failureStage, failureCode;
    unsigned generation = 0, buttons = 0, probeSerial = 0;
    bool ready = false, visible = false, pageLoaded = false, creating = false;
    bool startQueued = false, hit = false, captured = false, probePending = false, pointerInside = false;
    bool failed = false, modelReported = false;
    Clock::time_point probeDeadline{}, startupDeadline{};

    void Notify(const wxString& text)
    {
        statusText = text;
        if (status) status(text);
    }

    void Disconnect()
    {
        ready = pageLoaded = creating = probePending = hit = pointerInside = modelReported = false;
        ++probeSerial;
        if (owner.get() && ::GetCapture() == static_cast<HWND>(owner->GetHandle())) ::ReleaseCapture();
        captured = false;
        buttons = 0;
        if (composition && cursorBound) composition->remove_CursorChanged(cursorToken);
        if (web)
        {
            if (messageBound) web->remove_WebMessageReceived(messageToken);
            if (navigationBound) web->remove_NavigationStarting(navigationToken);
            if (frameNavigationBound) web->remove_FrameNavigationStarting(frameNavigationToken);
            if (completedBound) web->remove_NavigationCompleted(completedToken);
            if (windowBound) web->remove_NewWindowRequested(windowToken);
            if (processBound) web->remove_ProcessFailed(processToken);
            if (permissionBound) web->remove_PermissionRequested(permissionToken);
        }
        messageBound = navigationBound = frameNavigationBound = completedBound = false;
        windowBound = cursorBound = processBound = permissionBound = false;
        if (composition) composition->put_RootVisualTarget(nullptr);
        if (target) target->SetRoot(nullptr);
        if (device) device->Commit();
        if (controller) controller->Close();
        web.Reset();
        controller.Reset();
        composition.Reset();
        visual.Reset();
        target.Reset();
        device.Reset();
        environment.Reset();
    }

    void SendState()
    {
        if (!web || !pageLoaded) return;
        const wxString json = "{\"type\":\"state\",\"state\":\"" + phase + "\"}";
        web->PostWebMessageAsJson(json.wc_str());
        const bool shown = visible && owner.get() && owner->IsShown();
        web->PostWebMessageAsJson(shown ? L"{\"type\":\"visibility\",\"visible\":true}" :
            L"{\"type\":\"visibility\",\"visible\":false}");
    }
};

wxString PetWebView::FindResourceDirectory()
{
    wxString directory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    for (int level = 0; level <= 4; ++level)
    {
        for (const auto& relative : {wxString("res\\pet"), wxString("wx_demo\\res\\pet")})
        {
            wxFileName candidate(directory + "\\" + relative, "index.html");
            if (candidate.FileExists()) return NormalDirectory(candidate.GetPath());
        }
        wxFileName parent = wxFileName::DirName(directory);
        if (parent.GetDirCount() == 0) break;
        parent.RemoveLastDir();
        const wxString next = parent.GetPath();
        if (next == directory) break;
        directory = next;
    }
    return wxEmptyString;
}

wxString PetWebView::RuntimeDirectory()
{
    const wxString directory = ApplicationDataDirectory();
    return directory.empty() ? wxString() : NormalDirectory(directory + "\\live2d");
}

bool PetWebView::IsAllowedAction(const wxString& command)
{
    return command == "open-ai" || command == "start-tutorial" || command == "show-help" ||
        command == "show-shortcuts" || command == "hide-pet" || command == "reload-pet";
}

PetWebView::PetWebView(wxWindow* mainWindow, wxWindow* workspace)
    : wxFrame(mainWindow, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
        wxFRAME_NO_TASKBAR | wxBORDER_NONE),
      m_state(std::make_shared<State>()), m_startupTimer(this), m_pointerTimer(this)
{
    m_state->owner = this;
    m_state->mainWindow = mainWindow;
    m_state->workspace = workspace;
    SetBackgroundStyle(wxBG_STYLE_CUSTOM);
    const HWND window = static_cast<HWND>(GetHandle());
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(window, GWL_STYLE, style | WS_POPUP);
    LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    extended &= ~(WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST);
    SetWindowLongPtrW(window, GWL_EXSTYLE,
        extended | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    if (mainWindow) SetWindowLongPtrW(window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(mainWindow->GetHandle()));
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(initialized);
    LogStage("constructed", initialized);

    const std::weak_ptr<State> weak = m_state;
    Bind(wxEVT_TIMER, [weak](wxTimerEvent&) {
        auto state = weak.lock();
        if (state && state->owner.get()) state->owner->ProbePointer();
    }, m_pointerTimer.GetId());
    Bind(wxEVT_TIMER, [weak](wxTimerEvent&) {
        auto state = weak.lock();
        if (!state || !state->owner.get()) return;
        if (Clock::now() < state->startupDeadline)
        {
            state->owner->m_startupTimer.StartOnce(250);
            return;
        }
        state->owner->ShowFailure(wxS("桌宠加载超时，可从帮助菜单重新加载。"),
            state->pageLoaded ? "model-timeout" : "browser-timeout", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }, m_startupTimer.GetId());
    Bind(wxEVT_SIZE, [weak](wxSizeEvent& event) {
        if (auto state = weak.lock(); state && state->owner.get()) state->owner->UpdateBounds();
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, [weak](wxCloseEvent& event) {
        if (auto state = weak.lock(); state && state->owner.get())
        {
            state->owner->SetPageVisible(false);
            if (event.CanVeto()) { event.Veto(); return; }
        }
        event.Skip();
    });
    const auto sync = [weak](auto& event) {
        if (auto state = weak.lock(); state && state->owner.get()) state->owner->SyncBounds();
        event.Skip();
    };
    if (mainWindow)
    {
        mainWindow->Bind(wxEVT_MOVE, sync);
        mainWindow->Bind(wxEVT_SIZE, sync);
        mainWindow->Bind(wxEVT_ICONIZE, sync);
    }
    if (workspace)
    {
        workspace->Bind(wxEVT_SIZE, sync);
        workspace->Bind(wxEVT_MOVE, sync);
        workspace->Bind(wxEVT_SHOW, sync);
    }
    Hide();
}

PetWebView::~PetWebView()
{
    m_startupTimer.Stop();
    m_pointerTimer.Stop();
    ++m_state->generation;
    m_state->action = {};
    m_state->status = {};
    m_state->Disconnect();
    m_state->owner = nullptr;
    m_state.reset();
    if (m_comInitialized) CoUninitialize();
}

void PetWebView::ShowFailure(const wxString& message, const char* stage, long result)
{
    m_startupTimer.Stop();
    LogStage(stage, static_cast<HRESULT>(result));
    ++m_state->generation;
    m_state->ready = m_state->hit = m_state->creating = m_state->probePending = m_state->pageLoaded = false;
    m_state->failed = true;
    m_state->failureStage = wxString::FromUTF8(stage);
    m_state->failureCode = wxString::Format("0x%08lX", static_cast<unsigned long>(result));
    if (m_state->controller) m_state->controller->put_IsVisible(FALSE);
    Hide();
    m_state->Notify(message + " [" + m_state->failureStage + ", " + m_state->failureCode + "]");
}

void PetWebView::SyncBounds()
{
    const auto state = m_state;
    auto* workspace = state->workspace.get();
    auto* mainWindow = state->mainWindow.get();
    auto* topLevel = dynamic_cast<wxTopLevelWindow*>(mainWindow);
    const bool show = state->visible && !state->failed && workspace && mainWindow && workspace->IsShownOnScreen() &&
        mainWindow->IsShown() && (!topLevel || !topLevel->IsIconized());
    if (!show)
    {
        const bool wasShown = IsShown();
        if (wasShown) Hide();
        if (state->controller) state->controller->put_IsVisible(FALSE);
        if (wasShown) state->SendState();
        return;
    }
    const wxPoint origin = workspace->ClientToScreen(wxPoint());
    const wxSize size = workspace->GetClientSize();
    if (size.x <= 0 || size.y <= 0) return;
    RECT current{};
    GetWindowRect(static_cast<HWND>(GetHandle()), &current);
    if (current.left != origin.x || current.top != origin.y ||
        current.right - current.left != size.x || current.bottom - current.top != size.y)
    {
        SetWindowPos(static_cast<HWND>(GetHandle()), nullptr, origin.x, origin.y, size.x, size.y,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        UpdateBounds();
    }
    if (!IsShown())
    {
        ShowWithoutActivating();
        state->SendState();
    }
    if (state->controller) state->controller->put_IsVisible(state->pageLoaded);
}

void PetWebView::UpdateBounds()
{
    if (!m_state->controller) return;
    const wxSize size = GetClientSize();
    const RECT bounds{0, 0, size.x, size.y};
    m_state->controller->put_Bounds(bounds);
    m_state->controller->NotifyParentWindowPositionChanged();
}

void PetWebView::StartBrowser()
{
    m_startupTimer.Stop();
    auto state = m_state;
    state->startQueued = false;
    const unsigned generation = ++state->generation;
    state->Disconnect();
    state->failed = false;
    state->failureStage.clear();
    state->failureCode.clear();
    if (!m_comInitialized)
    {
        ShowFailure(wxS("无法初始化桌宠 COM 环境。"), "com-initialize", RPC_E_CHANGED_MODE);
        return;
    }
    const wxString resources = FindResourceDirectory(), runtime = RuntimeDirectory();
    if (resources.empty())
    {
        ShowFailure(wxS("找不到 res/pet 桌宠资源，请保留完整的 res 目录。"), "resources-missing");
        return;
    }
    if (runtime.empty() || !wxFileName::Mkdir(runtime, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
    {
        ShowFailure(wxS("无法创建桌宠本地缓存目录。"), "runtime-directory");
        return;
    }
#ifdef _WIN64
    const wxString loaderPath = wxFileName(resources + "\\webview2\\x64", "WebView2Loader.dll").GetFullPath();
#else
    const wxString loaderPath = wxFileName(resources + "\\webview2\\x86", "WebView2Loader.dll").GetFullPath();
#endif
    // 不卸载模块，异步环境和控制器回调可能仍在加载器代码中。
    static HMODULE loader = nullptr;
    if (!loader) loader = LoadLibraryExW(loaderPath.wc_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR,
        ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    const auto create = loader ? reinterpret_cast<CreateEnvironment>(
        GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")) : nullptr;
    if (!create)
    {
        ShowFailure(wxS("缺少桌宠 WebView2 加载组件，可从帮助菜单重试。"),
            "loader-unavailable", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    const wxString userData = ApplicationDataDirectory() + "\\pet-webview2";
    if (!wxFileName::Mkdir(userData, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
    {
        ShowFailure(wxS("无法创建浏览器缓存目录。"), "browser-directory");
        return;
    }
    state->creating = true;
    state->startupDeadline = Clock::now() + std::chrono::seconds(25);
    m_startupTimer.StartOnce(25000);
    LogStage("environment-start");
    state->Notify(wxS("正在启动透明 Live2D 桌宠…"));
    const std::weak_ptr<State> weak = state;
    auto environmentCallback = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [weak, generation, resources, runtime](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
        const auto state = weak.lock();
        if (!state || !state->owner.get() || state->generation != generation) return S_OK;
        LogStage("environment-complete", result);
        if (FAILED(result) || !environment)
        {
            state->owner->ShowFailure(wxS("WebView2 启动失败，请检查运行时后从帮助菜单重试。"),
                "environment-failed", result);
            return S_OK;
        }
        state->environment = environment;
        ComPtr<ICoreWebView2Environment3> environment3;
        HRESULT hr = environment->QueryInterface(IID_PPV_ARGS(&environment3));
        if (FAILED(hr))
        {
            state->owner->ShowFailure(wxS("WebView2 运行时不支持透明组合显示。"), "composition-unavailable", hr);
            return S_OK;
        }
        auto controllerCallback = Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
            [weak, generation, resources, runtime](HRESULT result,
                ICoreWebView2CompositionController* composition) -> HRESULT {
            const auto state = weak.lock();
            if (!state || !state->owner.get() || state->generation != generation)
            {
                if (composition)
                {
                    ComPtr<ICoreWebView2Controller> stale;
                    if (SUCCEEDED(composition->QueryInterface(IID_PPV_ARGS(&stale)))) stale->Close();
                }
                return S_OK;
            }
            LogStage("composition-complete", result);
            const auto fail = [state](const char* stage, HRESULT hr) {
                state->owner->ShowFailure(wxS("无法建立透明桌宠显示层，可从帮助菜单重试。"), stage, hr);
            };
            if (FAILED(result) || !composition) { fail("composition-create", result); return S_OK; }
            state->composition = composition;
            HRESULT hr = composition->QueryInterface(IID_PPV_ARGS(&state->controller));
            if (FAILED(hr)) { fail("controller-query", hr); return S_OK; }
            hr = state->controller->get_CoreWebView2(&state->web);
            if (FAILED(hr) || !state->web) { fail("webview-query", hr); return S_OK; }
            ComPtr<ICoreWebView2Controller2> colors;
            hr = state->controller.As(&colors);
            if (FAILED(hr)) { fail("controller-colors", hr); return S_OK; }
            hr = colors->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
            if (FAILED(hr)) { fail("transparent-background", hr); return S_OK; }
            hr = DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&state->device));
            if (FAILED(hr)) { fail("dcomp-device", hr); return S_OK; }
            hr = state->device->CreateTargetForHwnd(static_cast<HWND>(state->owner->GetHandle()),
                TRUE, &state->target);
            if (FAILED(hr)) { fail("dcomp-target", hr); return S_OK; }
            hr = state->device->CreateVisual(&state->visual);
            if (FAILED(hr)) { fail("dcomp-visual", hr); return S_OK; }
            hr = state->target->SetRoot(state->visual.Get());
            if (FAILED(hr)) { fail("dcomp-root", hr); return S_OK; }
            hr = composition->put_RootVisualTarget(state->visual.Get());
            if (FAILED(hr)) { fail("composition-target", hr); return S_OK; }
            hr = state->device->Commit();
            if (FAILED(hr)) { fail("dcomp-commit", hr); return S_OK; }
            ComPtr<ICoreWebView2_3> local;
            hr = state->web.As(&local);
            if (FAILED(hr)) { fail("virtual-host-interface", hr); return S_OK; }
            hr = local->SetVirtualHostNameToFolderMapping(L"wx-demo-pet.local", resources.wc_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
            if (FAILED(hr)) { fail("resource-mapping", hr); return S_OK; }
            hr = local->SetVirtualHostNameToFolderMapping(L"wx-demo-runtime.local", runtime.wc_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
            if (FAILED(hr)) { fail("runtime-mapping", hr); return S_OK; }
            ComPtr<ICoreWebView2Settings> settings;
            if (SUCCEEDED(state->web->get_Settings(&settings)))
            {
                settings->put_IsWebMessageEnabled(TRUE);
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_AreDefaultContextMenusEnabled(FALSE);
                settings->put_AreHostObjectsAllowed(FALSE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
            }

            state->messageBound = SUCCEEDED(state->web->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [weak, generation](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* event) -> HRESULT {
                const auto state = weak.lock();
                if (!state || !state->owner.get() || state->generation != generation) return S_OK;
                LPWSTR source = nullptr;
                const HRESULT gotSource = event->get_Source(&source);
                const bool trusted = SUCCEEDED(gotSource) && TrustedPage(source);
                CoTaskMemFree(source);
                if (!trusted) return S_OK;
                LPWSTR raw = nullptr;
                const HRESULT gotMessage = event->TryGetWebMessageAsString(&raw);
                if (FAILED(gotMessage)) { CoTaskMemFree(raw); return S_OK; }
                const wxString command(raw ? raw : L"");
                CoTaskMemFree(raw);
                if (command.length() > 64) return S_OK;
                if (command == "model-ready")
                {
                    state->ready = state->modelReported = true;
                    state->creating = false;
                    state->owner->m_startupTimer.Stop();
                    state->SendState();
                    LogStage("model-ready");
                    state->web->ExecuteScript(
                        L"window.wxDemoPet ? window.wxDemoPet.snapshot() : null",
                        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                            [weak, generation](HRESULT result, LPCWSTR json) -> HRESULT {
                            const auto current = weak.lock();
                            if (current && current->owner.get() && current->generation == generation)
                            {
                                const wxString detail = ("ready-diagnostics " + wxString(json ? json : L"null")).Left(4096);
                                LogStage(detail.ToUTF8().data(), result);
                            }
                            return S_OK;
                        }).Get());
                    state->Notify(wxS("Live2D 桌宠已就绪"));
                }
                else if (command == "model-unavailable")
                {
                    state->ready = state->creating = false;
                    state->modelReported = true;
                    state->owner->m_startupTimer.Stop();
                    LogStage("model-unavailable");
                    state->web->ExecuteScript(
                        L"window.wxDemoPet ? window.wxDemoPet.snapshot() : ({url:location.href,state:document.readyState})",
                        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                            [weak, generation](HRESULT result, LPCWSTR json) -> HRESULT {
                            const auto current = weak.lock();
                            if (current && current->owner.get() && current->generation == generation)
                            {
                                const wxString detail = ("page-diagnostics " + wxString(json ? json : L"null")).Left(4096);
                                LogStage(detail.ToUTF8().data(), result);
                            }
                            return S_OK;
                        }).Get());
                    state->Notify(wxS("Live2D 模型暂不可用，可从视图菜单重试。"));
                }
                else if (IsAllowedAction(command) && wxTheApp)
                {
                    // COM 事件返回后才执行可能打开对话框的原生动作。
                    wxTheApp->CallAfter([weak, generation, command] {
                        const auto current = weak.lock();
                        if (current && current->owner.get() && current->generation == generation && current->action)
                            current->action(command);
                    });
                }
                return S_OK;
            }).Get(), &state->messageToken));
            const auto navigation = Callback<ICoreWebView2NavigationStartingEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* event) -> HRESULT {
                LPWSTR uri = nullptr;
                const HRESULT result = event->get_Uri(&uri);
                if (FAILED(result) || !TrustedPage(uri)) event->put_Cancel(TRUE);
                CoTaskMemFree(uri);
                return S_OK;
            });
            state->navigationBound = SUCCEEDED(state->web->add_NavigationStarting(
                navigation.Get(), &state->navigationToken));
            state->frameNavigationBound = SUCCEEDED(state->web->add_FrameNavigationStarting(
                navigation.Get(), &state->frameNavigationToken));
            state->windowBound = SUCCEEDED(state->web->add_NewWindowRequested(
                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* event) -> HRESULT {
                    event->put_Handled(TRUE); return S_OK;
                }).Get(), &state->windowToken));
            state->permissionBound = SUCCEEDED(state->web->add_PermissionRequested(
                Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* event) -> HRESULT {
                    event->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
                }).Get(), &state->permissionToken));
            state->processBound = SUCCEEDED(state->web->add_ProcessFailed(
                Callback<ICoreWebView2ProcessFailedEventHandler>(
                [weak, generation](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*) -> HRESULT {
                    if (const auto state = weak.lock(); state && state->owner.get() && state->generation == generation)
                        state->owner->ShowFailure(wxS("桌宠渲染进程已停止，可从帮助菜单重试。"),
                            "browser-process-failed", E_FAIL);
                    return S_OK;
                }).Get(), &state->processToken));
            state->cursorBound = SUCCEEDED(composition->add_CursorChanged(
                Callback<ICoreWebView2CursorChangedEventHandler>(
                [weak, generation](ICoreWebView2CompositionController* sender, IUnknown*) -> HRESULT {
                    const auto state = weak.lock();
                    if (state && state->owner.get() && state->generation == generation &&
                        (state->hit || state->captured))
                    {
                        HCURSOR cursor = nullptr;
                        if (SUCCEEDED(sender->get_Cursor(&cursor)) && cursor) ::SetCursor(cursor);
                    }
                    return S_OK;
                }).Get(), &state->cursorToken));

            state->completedBound = SUCCEEDED(state->web->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [weak, generation](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* event) -> HRESULT {
                    const auto state = weak.lock();
                    if (!state || !state->owner.get() || state->generation != generation) return S_OK;
                    BOOL success = FALSE;
                    event->get_IsSuccess(&success);
                    if (!success)
                        state->owner->ShowFailure(wxS("桌宠页面加载失败，可从帮助菜单重试。"),
                            "page-navigation", E_FAIL);
                    else
                    {
                        state->pageLoaded = true;
                        state->startupDeadline = Clock::now() + std::chrono::seconds(45);
                        if (!state->modelReported) state->owner->m_startupTimer.StartOnce(45000);
                        state->owner->UpdateBounds();
                        state->owner->SyncBounds();
                        state->SendState();
                        LogStage("page-loaded");
                    }
                    return S_OK;
                }).Get(), &state->completedToken));
            if (!state->messageBound || !state->navigationBound || !state->windowBound ||
                !state->completedBound)
            {
                fail("required-events", E_FAIL);
                return S_OK;
            }
            state->owner->UpdateBounds();
            state->controller->put_IsVisible(FALSE);
            const bool localCore = wxFileName(runtime, "live2dcubismcore.min.js").FileExists();
            LogStage(localCore ? "runtime-core-local" : "runtime-core-cdn");
            // 虚拟主机映射不会进入 WebResourceRequested；文档先拿到固定地址再启动客户端。
            hr = state->web->AddScriptToExecuteOnDocumentCreated(
                localCore ? LocalRuntimeScript : CdnRuntimeScript,
                Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [weak, generation](HRESULT result, LPCWSTR scriptId) -> HRESULT {
                    const auto current = weak.lock();
                    if (!current || !current->owner.get() || current->generation != generation) return S_OK;
                    if (FAILED(result) || !scriptId || !*scriptId)
                    {
                        current->owner->ShowFailure(wxS("无法配置桌宠资源地址，可从帮助菜单重试。"),
                            "runtime-script-register", FAILED(result) ? result : E_FAIL);
                        return S_OK;
                    }
                    LogStage("runtime-script-ready");
                    LogStage("page-navigate");
                    const HRESULT navigated = current->web->Navigate(PageUrl);
                    if (FAILED(navigated)) current->owner->ShowFailure(
                        wxS("桌宠页面导航失败，可从帮助菜单重试。"), "navigate-call", navigated);
                    return S_OK;
                }).Get());
            if (FAILED(hr)) fail("runtime-script-request", hr);
            return S_OK;
        });
        const HRESULT created = environment3->CreateCoreWebView2CompositionController(
            static_cast<HWND>(state->owner->GetHandle()), controllerCallback.Get());
        if (FAILED(created))
            state->owner->ShowFailure(wxS("无法创建透明桌宠窗口。"), "composition-request", created);
        return S_OK;
    });
    const HRESULT result = create(nullptr, userData.wc_str(), nullptr, environmentCallback.Get());
    if (FAILED(result)) ShowFailure(wxS("WebView2 环境创建失败，可从帮助菜单重试。"),
        "environment-request", result);
}

void PetWebView::SetActionCallback(std::function<void(const wxString&)> callback)
{
    m_state->action = std::move(callback);
}

void PetWebView::SetStatusCallback(std::function<void(const wxString&)> callback)
{
    m_state->status = std::move(callback);
    if (m_state->status && !m_state->statusText.empty()) m_state->status(m_state->statusText);
}

void PetWebView::SetPetState(const wxString& state)
{
    if (!ValidState(state)) return;
    m_state->phase = state;
    m_state->SendState();
}

void PetWebView::SetPageVisible(bool visible)
{
    auto state = m_state;
    state->visible = visible;
    SyncBounds();
    state->SendState();
    if (visible)
    {
        if (!m_pointerTimer.IsRunning()) m_pointerTimer.Start(33);
        if ((!state->controller || state->failed) && !state->creating && !state->startQueued && wxTheApp)
        {
            state->startQueued = true;
            const std::weak_ptr<State> weak = state;
            const unsigned generation = state->generation;
            wxTheApp->CallAfter([weak, generation] {
                const auto current = weak.lock();
                if (!current || !current->owner.get()) return;
                current->startQueued = false;
                if (current->generation == generation && current->visible)
                    current->owner->StartBrowser();
            });
        }
    }
    else
    {
        m_pointerTimer.Stop();
        state->hit = false;
        if (state->captured && ::GetCapture() == static_cast<HWND>(GetHandle())) ::ReleaseCapture();
    }
}

void PetWebView::Reload()
{
    if (m_state->visible) StartBrowser();
    else
    {
        ++m_state->generation;
        m_state->Disconnect();
    }
}

bool PetWebView::IsReady() const
{
    return m_state->ready;
}

void PetWebView::ProbePointer()
{
    auto state = m_state;
    SyncBounds();
    if (!state->web || !state->composition || !state->pageLoaded || !IsShown()) return;
    POINT point{};
    if (!GetCursorPos(&point)) return;
    ::ScreenToClient(static_cast<HWND>(GetHandle()), &point);
    const wxSize size = GetClientSize();
    const bool inside = point.x >= 0 && point.y >= 0 && point.x < size.x && point.y < size.y;
    if (!inside && !state->captured) state->hit = false;
    if (inside)
    {
        state->composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
            MouseKeys(state->buttons), 0, point);
        state->pointerInside = true;
    }
    else if (state->pointerInside)
    {
        state->composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,
            MouseKeys(state->buttons), 0, POINT{});
        state->pointerInside = false;
    }
    if (state->probePending)
    {
        if (Clock::now() < state->probeDeadline) return;
        state->hit = false;
    }
    state->probePending = true;
    const unsigned serial = ++state->probeSerial;
    state->probeDeadline = Clock::now() + std::chrono::milliseconds(1000);
    const double scale = static_cast<double>(GetDpiForWindow(static_cast<HWND>(GetHandle()))) / 96.0;
    std::ostringstream script;
    script.imbue(std::locale::classic());
    script << std::fixed << std::setprecision(3)
        << "(()=>{const x=" << point.x / (scale > 0 ? scale : 1.0)
        << ",y=" << point.y / (scale > 0 ? scale : 1.0)
        << ";window.wxDemoPet?.pointer?.(x,y);return window.wxDemoPet?.probe(x,y)===true;})()";
    const unsigned generation = state->generation;
    const std::weak_ptr<State> weak = state;
    const wxString javascript = wxString::FromUTF8(script.str());
    const HRESULT started = state->web->ExecuteScript(javascript.wc_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [weak, generation, inside, serial](HRESULT result, LPCWSTR json) -> HRESULT {
            const auto current = weak.lock();
            if (current && current->owner.get() && current->generation == generation && current->probeSerial == serial)
            {
                current->probePending = false;
                current->hit = inside && SUCCEEDED(result) && json && wxString(json) == "true";
            }
            return S_OK;
        }).Get());
    if (FAILED(started)) state->probePending = state->hit = false;
}

WXLRESULT PetWebView::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
{
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        BeginPaint(static_cast<HWND>(GetHandle()), &paint);
        EndPaint(static_cast<HWND>(GetHandle()), &paint);
        return 0;
    }
    if (!m_state) return wxFrame::MSWWindowProc(message, wParam, lParam);
    auto state = m_state;
    if (message == WM_NCHITTEST)
        return state->visible && (state->hit || state->captured) ? HTCLIENT : HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_SETCURSOR && state->composition && (state->hit || state->captured))
    {
        HCURSOR cursor = nullptr;
        if (SUCCEEDED(state->composition->get_Cursor(&cursor)) && cursor)
        {
            ::SetCursor(cursor);
            return TRUE;
        }
    }
    if (!state->composition || !state->pageLoaded)
        return wxFrame::MSWWindowProc(message, wParam, lParam);

    COREWEBVIEW2_MOUSE_EVENT_KIND kind{};
    unsigned flag = 0;
    bool down = false, up = false;
    switch (message)
    {
    case WM_MOUSEMOVE: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE; break;
    case WM_LBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN; flag = MK_LBUTTON; down = true; break;
    case WM_LBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP; flag = MK_LBUTTON; up = true; break;
    case WM_LBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK; flag = MK_LBUTTON; down = true; break;
    case WM_RBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN; flag = MK_RBUTTON; down = true; break;
    case WM_RBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP; flag = MK_RBUTTON; up = true; break;
    case WM_RBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK; flag = MK_RBUTTON; down = true; break;
    case WM_MBUTTONDOWN: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN; flag = MK_MBUTTON; down = true; break;
    case WM_MBUTTONUP: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP; flag = MK_MBUTTON; up = true; break;
    case WM_MBUTTONDBLCLK: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOUBLE_CLICK; flag = MK_MBUTTON; down = true; break;
    case WM_MOUSEWHEEL: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL; break;
    case WM_MOUSEHWHEEL: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL; break;
    case WM_MOUSELEAVE: kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE; break;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (state->buttons & MK_LBUTTON) state->composition->SendMouseInput(
            COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP, MouseKeys(0), 0, POINT{-1, -1});
        if (state->buttons & MK_RBUTTON) state->composition->SendMouseInput(
            COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP, MouseKeys(0), 0, POINT{-1, -1});
        if (state->buttons & MK_MBUTTON) state->composition->SendMouseInput(
            COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP, MouseKeys(0), 0, POINT{-1, -1});
        state->composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE, MouseKeys(0), 0, POINT{});
        state->captured = false;
        state->buttons = 0;
        return wxFrame::MSWWindowProc(message, wParam, lParam);
    default:
        return wxFrame::MSWWindowProc(message, wParam, lParam);
    }
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    UINT32 data = 0;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
    {
        ::ScreenToClient(static_cast<HWND>(GetHandle()), &point);
        data = static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(wParam));
    }
    if (down)
    {
        state->buttons |= flag;
        state->captured = true;
        ::SetCapture(static_cast<HWND>(GetHandle()));
    }
    if (up) state->buttons &= ~flag;
    if (message == WM_MOUSEMOVE)
    {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, static_cast<HWND>(GetHandle()), 0};
        TrackMouseEvent(&track);
    }
    if (message == WM_MOUSELEAVE) point = POINT{};
    state->composition->SendMouseInput(kind, MouseKeys(state->buttons), data, point);
    if (up && state->buttons == 0)
    {
        state->captured = false;
        if (::GetCapture() == static_cast<HWND>(GetHandle())) ::ReleaseCapture();
    }
    return 0;
}

void PetWebView::ReadDiagnostics(std::function<void(const wxString&)> callback)
{
    if (!callback) return;
    if (m_state->failed)
    {
        callback("{\"ready\":false,\"stage\":\"" + m_state->failureStage +
            "\",\"errorCode\":\"" + m_state->failureCode + "\"}");
        return;
    }
    if (!m_state->web) { callback("null"); return; }
    const std::weak_ptr<State> weak = m_state;
    const unsigned generation = m_state->generation;
    m_state->web->ExecuteScript(L"window.wxDemoPet ? window.wxDemoPet.snapshot() : null",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [weak, generation, callback = std::move(callback)](HRESULT result, LPCWSTR json) -> HRESULT {
            const auto state = weak.lock();
            if (state && state->owner.get() && state->generation == generation && callback)
                callback(SUCCEEDED(result) && json ? wxString(json) : wxString("null"));
            return S_OK;
        }).Get());
}
