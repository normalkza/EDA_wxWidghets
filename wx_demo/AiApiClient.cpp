#include "AiApiClient.h"
#include "AiSession.h"
#include "CircuitValidation.h"
#include <wx/app.h>
#include <wx/mstream.h>
#include <wx/xml/xml.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <winhttp.h>
#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif
#endif
#include <charconv>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace
{
constexpr std::size_t MaxQuestion = 4000;
constexpr std::size_t MaxRequestBytes = 2 * 1024 * 1024;
constexpr std::size_t MaxResponseBytes = 256 * 1024;
constexpr std::size_t MaxComponents = 32;
constexpr std::size_t MaxWires = 64;
constexpr int DeadlineMs = 65000;

AiAssistantReply Failure(const wxString& message)
{
    return {AiAssistantReply::Kind::Error, message, {}};
}

bool IsXmlSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void SkipSpace(std::string_view& input)
{
    while (!input.empty() && IsXmlSpace(input.front())) input.remove_prefix(1);
}

bool ReadDeclarationField(std::string_view& input, std::string_view name, std::string_view& value)
{
    if (input.empty() || !IsXmlSpace(input.front())) return false;
    SkipSpace(input);
    if (!input.starts_with(name)) return false;
    input.remove_prefix(name.size());
    SkipSpace(input);
    if (input.empty() || input.front() != '=') return false;
    input.remove_prefix(1);
    SkipSpace(input);
    if (input.empty() || (input.front() != '\'' && input.front() != '"')) return false;
    const char quote = input.front();
    input.remove_prefix(1);
    const auto end = input.find(quote);
    if (end == std::string_view::npos) return false;
    value = input.substr(0, end);
    input.remove_prefix(end + 1);
    return true;
}

bool IsUtf8Name(std::string_view name)
{
    return name.size() == 5 && (name[0] == 'u' || name[0] == 'U') &&
        (name[1] == 't' || name[1] == 'T') && (name[2] == 'f' || name[2] == 'F') &&
        name.substr(3) == "-8";
}

bool IsSafeXml(std::string_view xml)
{
    if (xml.empty() || xml.size() > MaxResponseBytes || xml.find('\0') != std::string_view::npos)
        return false;
    if (xml.starts_with("\xEF\xBB\xBF")) xml.remove_prefix(3);
    if (xml.starts_with("<?xml"))
    {
        const auto close = xml.find("?>");
        if (close == std::string_view::npos || close > 96) return false;
        auto declaration = xml.substr(5, close - 5);
        std::string_view value;
        if (!ReadDeclarationField(declaration, "version", value) || value != "1.0") return false;
        auto rest = declaration;
        SkipSpace(rest);
        if (rest.starts_with("encoding"))
        {
            if (!ReadDeclarationField(declaration, "encoding", value) || !IsUtf8Name(value)) return false;
            rest = declaration;
            SkipSpace(rest);
        }
        if (rest.starts_with("standalone"))
        {
            if (!ReadDeclarationField(declaration, "standalone", value) ||
                (value != "yes" && value != "no")) return false;
        }
        SkipSpace(declaration);
        if (!declaration.empty()) return false;
        xml.remove_prefix(close + 2);
    }

    // Only the protocol's elements and the five predefined XML escapes are
    // needed. Reject declarations, entities and deep trees before wxXml loads.
    if (xml.find("<!") != std::string_view::npos || xml.find("<?") != std::string_view::npos)
        return false;
    for (auto pos = xml.find('&'); pos != std::string_view::npos; pos = xml.find('&', pos + 1))
    {
        const auto escape = xml.substr(pos);
        if (!escape.starts_with("&amp;") && !escape.starts_with("&lt;") &&
            !escape.starts_with("&gt;") && !escape.starts_with("&quot;") &&
            !escape.starts_with("&apos;")) return false;
    }

    std::size_t depth = 0, nodes = 0;
    for (auto start = xml.find('<'); start != std::string_view::npos; )
    {
        char quote = '\0';
        auto end = start + 1;
        for (; end < xml.size(); ++end)
        {
            const char c = xml[end];
            if (quote) { if (c == quote) quote = '\0'; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '>') break;
            else if (c == '<') return false;
        }
        if (end == xml.size() || end == start + 1) return false;
        if (xml[start + 1] == '/')
        {
            if (depth == 0) return false;
            --depth;
        }
        else
        {
            if (++nodes > 10000 || depth >= 5) return false;
            if (xml[end - 1] != '/') ++depth;
        }
        start = xml.find('<', end + 1);
    }
    return depth == 0;
}

bool IsElement(const wxXmlNode* node, const wxString& name)
{
    return node && node->GetType() == wxXML_ELEMENT_NODE && node->GetName() == name;
}

bool HasOnlyAttributes(const wxXmlNode& node, std::initializer_list<const char*> names)
{
    std::size_t count = 0;
    for (auto* attribute = node.GetAttributes(); attribute; attribute = attribute->GetNext()) ++count;
    if (count != names.size()) return false;
    for (auto name : names)
        if (!node.HasAttribute(wxString::FromAscii(name))) return false;
    return true;
}

bool IsIdentifier(std::string_view value, std::size_t maxLength)
{
    const auto first = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    if (value.empty() || value.size() > maxLength || !first(value.front())) return false;
    for (char c : value)
        if (!first(c) && !(c >= '0' && c <= '9') && c != '-') return false;
    return true;
}

bool ReadIdentifier(const wxXmlNode& node, const wxString& key, std::size_t maxLength, std::string& value)
{
    value = node.GetAttribute(key).ToStdString(wxConvUTF8);
    return IsIdentifier(value, maxLength);
}

bool ReadAction(const wxXmlNode& root, AiCircuitAction& action)
{
    auto* components = root.GetChildren();
    auto* wires = components ? components->GetNext() : nullptr;
    if (!IsElement(components, "components") || !IsElement(wires, "wires") || wires->GetNext() ||
        !HasOnlyAttributes(*components, {}) || !HasOnlyAttributes(*wires, {})) return false;

    for (auto* node = components->GetChildren(); node; node = node->GetNext())
    {
        if (action.components.size() >= MaxComponents || !IsElement(node, "component") || node->GetChildren() ||
            !HasOnlyAttributes(*node, {"ref", "type", "inputCount"})) return false;
        AiComponentSpec item;
        if (!ReadIdentifier(*node, "ref", 32, item.ref) || !ReadIdentifier(*node, "type", 16, item.type))
            return false;
        const std::string count = node->GetAttribute("inputCount").ToStdString(wxConvUTF8);
        const auto parsed = std::from_chars(count.data(), count.data() + count.size(), item.inputCount);
        if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() ||
            item.inputCount < 0 || item.inputCount > 16) return false;
        action.components.push_back(std::move(item));
    }
    if (action.components.empty()) return false;

    for (auto* node = wires->GetChildren(); node; node = node->GetNext())
    {
        if (action.wires.size() >= MaxWires || !IsElement(node, "wire") || node->GetChildren() ||
            !HasOnlyAttributes(*node, {"from", "outPin", "to", "inPin"})) return false;
        AiWireSpec wire;
        if (!ReadIdentifier(*node, "from", 32, wire.from) || !ReadIdentifier(*node, "to", 32, wire.to) ||
            !ReadIdentifier(*node, "outPin", 16, wire.outPin) || !ReadIdentifier(*node, "inPin", 16, wire.inPin))
            return false;
        action.wires.push_back(std::move(wire));
    }
    return true;
}

bool ReadText(const wxXmlNode& node, wxString& text)
{
    for (auto* child = node.GetChildren(); child; child = child->GetNext())
    {
        if (child->GetType() != wxXML_TEXT_NODE) return false;
        text += child->GetContent();
    }
    return text.length() <= 6000 && CircuitValidation::IsValidXmlText(text);
}

bool ReadPatch(const wxXmlNode& root, AiCircuitPatch& patch)
{
    auto* summary = root.GetChildren(); auto* operations = summary ? summary->GetNext() : nullptr;
    if (!IsElement(summary, "summary") || !IsElement(operations, "operations") || operations->GetNext() ||
        !HasOnlyAttributes(*summary, {}) || !HasOnlyAttributes(*operations, {}) || !ReadText(*summary, patch.summary)) return false;
    for (auto* node = operations->GetChildren(); node; node = node->GetNext())
    {
        if (patch.operations.size() >= 128 || !IsElement(node, "op") || !HasOnlyAttributes(*node, {"kind", "ref"})) return false;
        AiEditOperation operation;
        if (!ReadIdentifier(*node, "kind", 32, operation.kind) || !ReadIdentifier(*node, "ref", 32, operation.ref)) return false;
        for (auto* child = node->GetChildren(); child; child = child->GetNext())
        {
            if (IsElement(child, "field") && HasOnlyAttributes(*child, {"name"}))
            {
                std::string name; wxString text;
                if (operation.fields.size() >= 14 || !ReadIdentifier(*child, "name", 32, name) ||
                    !ReadText(*child, text) || !operation.fields.emplace(name, text).second) return false;
            }
            else if (IsElement(child, "point") && HasOnlyAttributes(*child, {"x", "y"}) && !child->GetChildren())
            {
                wxPoint point;
                if (operation.points.size() >= 64) return false;
                for (const auto& field : {std::pair("x", &point.x), std::pair("y", &point.y)})
                {
                    const auto value = child->GetAttribute(field.first).ToStdString(wxConvUTF8);
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), *field.second);
                    if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()) return false;
                }
                operation.points.push_back(point);
            }
            else return false;
        }
        patch.operations.push_back(std::move(operation));
    }
    return !patch.operations.empty();
}

AiAssistantReply DecodeReply(const std::string& bytes, AiAssistantMode mode)
{
    if (bytes.empty()) return Failure(wxT("本地助手未返回内容。"));
    if (!IsSafeXml(bytes)) return Failure(wxT("本地助手返回了不受支持的 XML。"));
    wxMemoryInputStream stream(bytes.data(), bytes.size());
    wxXmlDocument document;
    wxXmlParseError parseError;
    if (!document.Load(stream, wxXMLDOC_NONE, &parseError))
        return Failure(wxT("无法读取本地助手的回复。"));
    auto* root = document.GetRoot();
    if (!IsElement(root, "assistant_reply") || !HasOnlyAttributes(*root, {"kind"}))
        return Failure(wxT("本地助手的回复格式不正确。"));
    const wxString kind = root->GetAttribute("kind");
    if (kind == "patch")
    {
        if (mode != AiAssistantMode::Apply) return Failure(wxS("指导模式不能执行画布修改。"));
        AiAssistantReply reply; reply.kind = AiAssistantReply::Kind::Patch;
        if (!ReadPatch(*root, reply.patch)) return Failure(wxS("画布修改操作的格式无效。"));
        return reply;
    }
    if (kind == "action")
    {
        if (mode != AiAssistantMode::Apply) return Failure(wxT("给步骤模式收到了非预期的电路操作。"));
        AiAssistantReply reply{AiAssistantReply::Kind::Action, {}, {}};
        if (!ReadAction(*root, reply.action)) return Failure(wxT("本地助手返回的电路操作格式不正确。"));
        return reply;
    }
    if (kind != "text" && kind != "error") return Failure(wxT("本地助手返回了未知的回复类型。"));
    auto* node = root->GetChildren();
    if (!IsElement(node, "text") || node->GetNext() || !HasOnlyAttributes(*node, {}))
        return Failure(wxT("本地助手的文字回复格式不正确。"));
    wxString text;
    for (auto* child = node->GetChildren(); child; child = child->GetNext())
    {
        if (child->GetType() != wxXML_TEXT_NODE) return Failure(wxT("本地助手的文字回复格式不正确。"));
        text += child->GetContent();
    }
    text.Trim().Trim(false);
    if (text.empty()) return Failure(wxT("本地助手未返回内容。"));
    return {kind == "text" ? AiAssistantReply::Kind::Text : AiAssistantReply::Kind::Error, std::move(text), {}};
}

wxXmlNode* Element(wxXmlNode* parent, const wxString& name)
{
    auto* node = new wxXmlNode(wxXML_ELEMENT_NODE, name); parent->AddChild(node); return node;
}

bool BuildRequest(AiAssistantMode mode, wxString question, wxString& body, wxString& error,
    const AiWorkspaceContext* context)
{
    question.Trim().Trim(false);
    if (question.empty()) { error = wxT("请输入问题或电路需求。"); return false; }
    if (question.length() > MaxQuestion || !CircuitValidation::IsValidXmlText(question))
    {
        error = wxT("请输入不超过 4000 字的有效文本。");
        return false;
    }
    wxXmlDocument document;
    document.SetVersion("1.0");
    document.SetFileEncoding("UTF-8");
    auto* root = new wxXmlNode(wxXML_ELEMENT_NODE, "assistant_request");
    document.SetRoot(root);
    root->AddAttribute("mode", mode == AiAssistantMode::Guide ? "guide" : "apply");
    auto* child = Element(root, "question");
    child->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "text", question));
    if (context)
    {
        wxXmlDocument scene;
        const auto bytes = context->sceneXml.ToUTF8();
        wxMemoryInputStream input(bytes.data(), bytes.length());
        wxXmlParseError parseError;
        if (!scene.Load(input, wxXMLDOC_NONE, &parseError) || !IsElement(scene.GetRoot(), "workspace"))
        { error = wxS("无法读取当前画布快照。"); return false; }
        auto* state = Element(root, "context");
        state->AddAttribute("phase", context->review ? "review" : "edit");
        state->AddChild(new wxXmlNode(*scene.GetRoot()));
        auto* image = Element(state, "image");
        image->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "text", context->imageBase64));
        auto* history = Element(state, "history");
        for (const auto& message : context->history)
        {
            auto* item = Element(history, "message"); item->AddAttribute("role", message.role);
            item->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "text", message.text));
        }
        auto* feedback = Element(state, "feedback");
        feedback->AddChild(new wxXmlNode(wxXML_TEXT_NODE, "text", context->feedback));
    }
    wxMemoryOutputStream output;
    if (!document.Save(output, -1) || output.GetSize() > MaxRequestBytes)
    {
        error = wxT("问题和画布上下文超过 2 MB，请缩短文本或关闭画布图像后重试。");
        return false;
    }
    std::string bytes(output.GetSize(), '\0');
    output.CopyTo(bytes.data(), bytes.size());
    body = wxString::FromUTF8(bytes.data(), bytes.size());
    return !body.empty();
}

int NextRequestId()
{
    // The WebRequest events share the application handler; use distinct IDs
    // even when a previous dialog was closed before its completion arrived.
    static int next = wxID_HIGHEST + 3000;
    return ++next;
}
}

AiApiClient::AiApiClient() : m_deadline(this)
{
#ifdef __WXMSW__
    m_session = wxWebSession::New(wxString::FromAscii(wxWebSessionBackendWinHTTP));
    m_sessionReady = m_session.IsOpened() && m_session.SetProxy(wxWebProxy::Disable());
#endif
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Stop(StopReason::Deadline); }, m_deadline.GetId());
}

AiApiClient::~AiApiClient()
{
    Stop(StopReason::User);
    DetachEvents();
}

bool AiApiClient::Start(AiAssistantMode mode, const wxString& question, Completion completion, wxString& error,
    const AiWorkspaceContext* context)
{
    error.clear();
    if (IsBusy()) { error = wxT("已有请求正在处理。"); return false; }
    if (!m_sessionReady || !wxTheApp) { error = wxT("无法启动本地助手连接。"); return false; }
    wxString body;
    if (!BuildRequest(mode, question, body, error, context)) return false;

    const int id = NextRequestId();
    auto request = m_session.CreateRequest(wxTheApp, AiSession::Get().Endpoint(), id);
    if (!request.IsOk()) { error = wxT("无法创建本地助手请求。"); return false; }
#ifdef __WXMSW__
    // Do not follow even a loopback service's redirects or use its proxy.
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(reinterpret_cast<HINTERNET>(m_session.GetNativeHandle()),
        WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)))
    {
        error = wxT("无法限制本地助手连接范围。");
        return false;
    }
#endif
    request.SetMethod("POST");
    if (!AiSession::Get().Token().empty())
        request.SetHeader("Authorization", "Bearer " + AiSession::Get().Token());
    request.SetHeader("Accept", "application/xml");
    request.SetData(body, "application/xml; charset=utf-8");
    request.SetStorage(wxWebRequest::Storage_None);
    request.SetTimeouts(5000, DeadlineMs);

    m_mode = mode;
    m_request = std::move(request);
    m_eventId = id;
    m_completion = std::move(completion);
    m_stopReason = StopReason::None;
    m_body.clear();
    wxTheApp->Bind(wxEVT_WEBREQUEST_DATA, &AiApiClient::OnData, this, id);
    wxTheApp->Bind(wxEVT_WEBREQUEST_STATE, &AiApiClient::OnState, this, id);
    m_deadline.StartOnce(DeadlineMs);
    m_request.Start();
    return true;
}

void AiApiClient::Cancel() { Stop(StopReason::User); }

void AiApiClient::Stop(StopReason reason)
{
    if (!IsBusy() || m_stopReason != StopReason::None) return;
    m_stopReason = reason;
    m_deadline.Stop();
    if (m_request.GetState() == wxWebRequest::State_Active) m_request.Cancel();
}

void AiApiClient::DetachEvents()
{
    if (m_eventId == wxID_NONE || !wxTheApp) return;
    wxTheApp->Unbind(wxEVT_WEBREQUEST_DATA, &AiApiClient::OnData, this, m_eventId);
    wxTheApp->Unbind(wxEVT_WEBREQUEST_STATE, &AiApiClient::OnState, this, m_eventId);
    m_eventId = wxID_NONE;
}

void AiApiClient::Complete(AiAssistantReply reply)
{
    m_deadline.Stop();
    DetachEvents();
    m_request = wxWebRequest();
    m_body.clear();
    auto completion = std::move(m_completion);
    if (completion) completion(std::move(reply));
}

void AiApiClient::OnData(wxWebRequestEvent& event)
{
    if (event.GetId() != m_eventId || m_stopReason != StopReason::None) return;
    const std::size_t size = event.GetDataSize();
    if (size > MaxResponseBytes - m_body.size()) { Stop(StopReason::BodyLimit); return; }
    if (size) m_body.append(static_cast<const char*>(event.GetDataBuffer()), size);
}

void AiApiClient::OnState(wxWebRequestEvent& event)
{
    if (event.GetId() != m_eventId || event.GetState() == wxWebRequest::State_Active ||
        event.GetState() == wxWebRequest::State_Idle) return;
    if (m_stopReason == StopReason::Deadline) { Complete(Failure(wxT("本地助手响应超时，请重试。"))); return; }
    if (m_stopReason == StopReason::BodyLimit) { Complete(Failure(wxT("本地助手回复超过 256 KB。"))); return; }
    if (m_stopReason == StopReason::User || event.GetState() == wxWebRequest::State_Cancelled)
    {
        Complete({AiAssistantReply::Kind::Cancelled, wxT("已取消。"), {}});
        return;
    }
    const auto& response = event.GetResponse();
    if (!response.IsOk())
    {
        Complete(Failure(wxT("无法连接本地助手，请重新启动应用并登录后重试。")));
        return;
    }
    const int status = response.GetStatus();
    if (status >= 200 && status < 300 && event.GetState() == wxWebRequest::State_Completed)
    {
        Complete(DecodeReply(m_body, m_mode));
        return;
    }
    if (status >= 400 && !m_body.empty())
    {
        auto reply = DecodeReply(m_body, AiAssistantMode::Guide);
        // Only a well-formed broker error may add a message on HTTP failure.
        if (reply.kind == AiAssistantReply::Kind::Error)
        {
            Complete(Failure(wxString::Format(wxT("本地助手请求失败（HTTP %d）："), status) + reply.text));
            return;
        }
    }
    Complete(Failure(wxString::Format(wxT("本地助手请求失败（HTTP %d）。"), status)));
}
