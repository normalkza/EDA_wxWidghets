"""Loopback XML adapter for the wx_demo circuit assistant."""

import argparse
import http.client
import http.server
import json
import os
import re
import socket
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET


HOST = "127.0.0.1"
PORT = 8765
TURN_PATH = "/assistant/turn"
DEEPSEEK_URL = "https://api.deepseek.com/responses"
DEEPSEEK_PROBE_URL = "https://api.deepseek.com/models"
DEFAULT_MODEL = "deepseek-flash"
UPSTREAM_TIMEOUT = 25
NETWORK_PROBE_TIMEOUT = 4
MAX_REQUEST_BYTES = 16 * 1024
MAX_QUESTION_CHARS = 4000
MAX_UPSTREAM_BYTES = 256 * 1024
MAX_REPLY_CHARS = 6000
MAX_COMPONENTS = 32
MAX_WIRES = 64
REF_PATTERN = r"[A-Za-z_][A-Za-z0-9_-]{0,31}"
TYPES = ("INPUT", "OUTPUT", "AND", "OR", "NOT", "NAND", "NOR", "XOR")
FIXED_INPUTS = {"INPUT": 0, "OUTPUT": 1, "NOT": 1}
INVALID_XML_CHARS = re.compile(r"[^\x09\x0a\x0d\x20-\ud7ff\ue000-\ufffd\U00010000-\U0010ffff]")
INVALID_RESPONSE = "DeepSeek 返回的响应无效，请重试。"
INVALID_ACTION = "助手返回的电路无效，没有应用。请简化要求后重试。"
_auto_proxy_mode = "system"

PIN_RULES = (
    "仅支持这些元件：INPUT（inputCount=0，输出 OUT），OUTPUT（inputCount=1，输入 IN），"
    "NOT（inputCount=1，输入 IN，输出 OUT）；AND、OR、NAND、NOR、XOR 的 inputCount "
    "是 2 至 16，输入从 A 按顺序命名到 P，输出是 OUT。引脚名称必须严格匹配，区分大小写。"
)
BASE_INSTRUCTIONS = (
    "你是 wx_demo 的数字电路助手。用提问者使用的语言简明作答。"
    "你没有当前画布、文件或已有元件的数据，不要声称读过或更改过它们。" + PIN_RULES
)
GUIDE_INSTRUCTIONS = "当前是指导模式。只提供解释或接线步骤，不生成可执行操作。"
APPLY_INSTRUCTIONS = (
    "当前是应用模式。只有用户明确要求添加或绘制电路，并且能用上述元件实现时，"
    "才调用一次 append_circuit；否则给出简短的文字答复。只能追加此次新建的元件，"
    "不能引用原有画布。为每个元件分配唯一、简短的本地 ref。"
    "最多 32 个元件和 64 条导线。导线从输出接至输入，允许扇出，"
    "同一输入不得有多个驱动，不得有组合逻辑环路。客户端决定布局。"
)

APPEND_TOOL = {
    "type": "function",
    "name": "append_circuit",
    "description": "Append one circuit made only of newly declared components and their wires.",
    "parameters": {
        "type": "object",
        "additionalProperties": False,
        "required": ["components", "wires"],
        "properties": {
            "components": {
                "type": "array",
                "minItems": 1,
                "maxItems": MAX_COMPONENTS,
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "required": ["ref", "type", "inputCount"],
                    "properties": {
                        "ref": {"type": "string", "pattern": "^" + REF_PATTERN + "$"},
                        "type": {"type": "string", "enum": list(TYPES)},
                        "inputCount": {"type": "integer", "minimum": 0, "maximum": 16},
                    },
                },
            },
            "wires": {
                "type": "array",
                "maxItems": MAX_WIRES,
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "required": ["from", "outPin", "to", "inPin"],
                    "properties": {
                        "from": {"type": "string", "pattern": "^" + REF_PATTERN + "$"},
                        "outPin": {"type": "string", "enum": ["OUT"]},
                        "to": {"type": "string", "pattern": "^" + REF_PATTERN + "$"},
                        "inPin": {"type": "string", "enum": ["IN", *"ABCDEFGHIJKLMNOP"]},
                    },
                },
            },
        },
    },
}


class BrokerError(Exception):
    """An error whose message is safe to display locally."""


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # A bearer token is sent only to the fixed, HTTPS endpoint.
        return None


def configured_proxy_mode():
    value = os.environ.get("DEEPSEEK_PROXY", "").strip() or "auto"
    if value.lower() in ("auto", "system", "direct"):
        return value.lower()
    try:
        proxy = urllib.parse.urlsplit(value)
        if (proxy.scheme == "http" and proxy.hostname and proxy.port and proxy.username is None
                and proxy.password is None and proxy.path in ("", "/") and not proxy.query and not proxy.fragment
                and not any(ch.isspace() for ch in value)):
            return value
    except ValueError:
        pass
    raise BrokerError("DEEPSEEK_PROXY 仅支持 auto、system、direct 或 http://主机:端口（不含账号）。")


def network_opener(mode):
    if mode == "system":
        return urllib.request.build_opener(NoRedirect())
    proxies = {} if mode == "direct" else {"https": mode}
    return urllib.request.build_opener(urllib.request.ProxyHandler(proxies), NoRedirect())


def describe_network_error(error):
    reason = error.reason if isinstance(error, urllib.error.URLError) else error
    if isinstance(reason, TimeoutError) or getattr(reason, "winerror", None) == 10060:
        return "连接 DeepSeek 超时，请重试。"
    if isinstance(reason, socket.gaierror):
        return "DNS 无法解析 DeepSeek 或代理地址。请检查网络，或使用 --check-network 比较连接路径。"
    if isinstance(reason, ssl.SSLCertVerificationError):
        return "连接 DeepSeek 时证书校验失败。请检查系统时间、证书和代理设置。"
    if isinstance(reason, ssl.SSLError):
        return "连接 DeepSeek 时 TLS 握手失败。请检查 HTTPS 代理是否使用正确的 HTTP 代理端口。"
    if isinstance(reason, OSError):
        # urllib wraps a rejected CONNECT in OSError, not necessarily HTTPError.
        tunnel = re.match(r"^Tunnel connection failed:\s*(\d{3})\b", str(reason))
        if tunnel:
            if tunnel.group(1) == "407":
                return "连接 DeepSeek 时代理要求身份验证（HTTP 407）。"
            return "代理未能建立到 DeepSeek 的 HTTPS 连接。请检查代理或使用 --check-network。"
    if isinstance(reason, ConnectionRefusedError):
        return "连接 DeepSeek 或代理时被拒绝。请确认代理正在运行，或使用 --check-network。"
    if isinstance(reason, (ConnectionResetError, http.client.RemoteDisconnected, http.client.IncompleteRead)):
        return "与 DeepSeek 的连接中断，未收到完整回复。请重试，或使用 --check-network。"
    return "无法连接 DeepSeek。请运行 py assistant_server.py --check-network 检查代理和直连。"


def probe_network(mode):
    # A deliberately unauthenticated request only checks DNS/TLS/HTTP reachability.
    request = urllib.request.Request(DEEPSEEK_PROBE_URL, method="GET")
    try:
        with network_opener(mode).open(request, timeout=NETWORK_PROBE_TIMEOUT) as response:
            return True, f"已连接（HTTP {response.status}）"
    except urllib.error.HTTPError as error:
        code = error.code
        error.close()
        if code == 407:
            return False, "代理要求身份验证（HTTP 407）。"
        if code == 401:
            return True, "已连接（HTTP 401；无密钥探测的预期结果）"
        return True, f"已收到响应（HTTP {code}）"
    except (OSError, ValueError, http.client.HTTPException) as error:
        return False, describe_network_error(error)


def choose_auto_proxy():
    if probe_network("system")[0]:
        return "system"
    return "direct" if probe_network("direct")[0] else "system"


def network_label(mode):
    return {"system": "系统/环境设置", "direct": "直连"}.get(mode, "指定 HTTP 代理")


def parse_turn(data):
    if not 0 < len(data) <= MAX_REQUEST_BYTES:
        raise ValueError("invalid request size")
    xml = data.decode("utf-8-sig")
    declaration = re.match(r"<\?xml(?=\s).*?\?>", xml, re.DOTALL)
    remaining = xml[declaration.end():] if declaration else xml
    # The protocol needs neither a DTD, entities, comments nor processing instructions.
    if "<!" in xml or "<?" in remaining:
        raise ValueError("unsupported XML markup")
    if declaration:
        encoding = re.search(r"\bencoding\s*=\s*(['\"])([^'\"]+)\1", declaration.group())
        if encoding and encoding.group(2).lower() != "utf-8":
            raise ValueError("expected UTF-8")
    root = ET.fromstring(xml)
    mode = root.get("mode")
    if (root.tag != "assistant_request" or set(root.attrib) != {"mode"}
            or mode not in ("guide", "apply") or len(root) != 1 or (root.text or "").strip()):
        raise ValueError("invalid request root")
    node = root[0]
    if node.tag != "question" or node.attrib or len(node) or (node.tail or "").strip():
        raise ValueError("invalid question element")
    question = node.text or ""
    if not question.strip() or len(question) > MAX_QUESTION_CHARS:
        raise ValueError("invalid question length")
    return mode, question.strip()


def make_payload(mode, question, model):
    apply = mode == "apply"
    payload = {
        "model": model,
        "input": question,
        "instructions": BASE_INSTRUCTIONS + (APPLY_INSTRUCTIONS if apply else GUIDE_INSTRUCTIONS),
        "reasoning": {"effort": "none"},
        "stream": False,
        "max_output_tokens": 4096 if apply else 1536,
    }
    if apply:
        payload["tools"] = [APPEND_TOOL]
    return payload


def decode_json(text):
    def unique_object(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result

    def invalid_constant(_):
        raise ValueError("invalid JSON constant")

    return json.loads(text, object_pairs_hook=unique_object, parse_constant=invalid_constant)


def fetch_deepseek(payload, api_key):
    mode = configured_proxy_mode()
    if mode == "auto":
        mode = _auto_proxy_mode
    request = urllib.request.Request(
        DEEPSEEK_URL,
        data=json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8"),
        headers={"Authorization": "Bearer " + api_key, "Content-Type": "application/json",
                 "Accept": "application/json"},
        method="POST",
    )
    try:
        opener = network_opener(mode)
        with opener.open(request, timeout=UPSTREAM_TIMEOUT) as response:
            data = response.read(MAX_UPSTREAM_BYTES + 1)
    except urllib.error.HTTPError as error:
        error.close()  # Never relay the upstream error body or its headers.
        if error.code in (401, 403):
            message = "DeepSeek 拒绝了访问，请检查 API 密钥。"
        elif error.code == 402:
            message = "DeepSeek 账户余额不足。"
        elif error.code == 429:
            message = "DeepSeek 请求过于频繁，请稍后再试。"
        elif error.code == 407:
            message = "连接 DeepSeek 时代理要求身份验证（HTTP 407）。"
        elif error.code in (408, 504):
            message = "DeepSeek 响应超时，请重试。"
        elif error.code in (400, 404, 422):
            message = "DeepSeek 未接受请求，请检查模型配置。"
        else:
            message = "DeepSeek 服务暂时不可用，请稍后再试。"
        raise BrokerError(message) from None
    except (OSError, ValueError, http.client.HTTPException) as error:
        raise BrokerError(describe_network_error(error)) from None
    if len(data) > MAX_UPSTREAM_BYTES:
        raise BrokerError("DeepSeek 返回的响应过大，请缩小问题范围。")
    try:
        return decode_json(data.decode("utf-8"))
    except (ValueError, RecursionError):
        raise BrokerError(INVALID_RESPONSE) from None


def parse_action(arguments):
    if not isinstance(arguments, str) or len(arguments) > MAX_UPSTREAM_BYTES:
        raise BrokerError(INVALID_ACTION)
    try:
        action = decode_json(arguments)
    except (ValueError, RecursionError):
        raise BrokerError(INVALID_ACTION) from None
    if not isinstance(action, dict) or set(action) != {"components", "wires"}:
        raise BrokerError(INVALID_ACTION)
    components, wires = action["components"], action["wires"]
    if (not isinstance(components, list) or not 1 <= len(components) <= MAX_COMPONENTS
            or not isinstance(wires, list) or len(wires) > MAX_WIRES):
        raise BrokerError(INVALID_ACTION)

    local = {}
    for part in components:
        if not isinstance(part, dict) or set(part) != {"ref", "type", "inputCount"}:
            raise BrokerError(INVALID_ACTION)
        ref, kind, count = part["ref"], part["type"], part["inputCount"]
        if (not isinstance(ref, str) or not re.fullmatch(REF_PATTERN, ref) or ref in local
                or kind not in TYPES or type(count) is not int):
            raise BrokerError(INVALID_ACTION)
        if kind in FIXED_INPUTS:
            if count != FIXED_INPUTS[kind]:
                raise BrokerError(INVALID_ACTION)
        elif not 2 <= count <= 16:
            raise BrokerError(INVALID_ACTION)
        local[ref] = (kind, count)

    inputs_used = set()
    next_refs = {ref: [] for ref in local}
    incoming = dict.fromkeys(local, 0)
    for wire in wires:
        if (not isinstance(wire, dict) or set(wire) != {"from", "outPin", "to", "inPin"}
                or any(not isinstance(value, str) for value in wire.values())):
            raise BrokerError(INVALID_ACTION)
        source, target = wire["from"], wire["to"]
        if (source not in local or target not in local or source == target
                or local[source][0] == "OUTPUT" or wire["outPin"] != "OUT"):
            raise BrokerError(INVALID_ACTION)
        kind, count = local[target]
        pin = wire["inPin"]
        pins = ("IN",) if kind in ("OUTPUT", "NOT") else tuple("ABCDEFGHIJKLMNOP"[:count])
        if pin not in pins or (target, pin) in inputs_used:
            raise BrokerError(INVALID_ACTION)
        inputs_used.add((target, pin))
        next_refs[source].append(target)
        incoming[target] += 1

    ready = [ref for ref, count in incoming.items() if count == 0]
    visited = 0
    while ready:
        ref = ready.pop()
        visited += 1
        for target in next_refs[ref]:
            incoming[target] -= 1
            if incoming[target] == 0:
                ready.append(target)
    if visited != len(local):
        raise BrokerError(INVALID_ACTION)

    reply = ET.Element("assistant_reply", {"kind": "action"})
    parts_node = ET.SubElement(reply, "components")
    for part in components:
        ET.SubElement(parts_node, "component", {"ref": part["ref"], "type": part["type"],
                                               "inputCount": str(part["inputCount"])})
    wires_node = ET.SubElement(reply, "wires")
    for wire in wires:
        ET.SubElement(wires_node, "wire", wire)
    return reply


def text_reply(kind, text):
    text = INVALID_XML_CHARS.sub("\ufffd", text)
    if len(text) > MAX_REPLY_CHARS:
        text = text[:MAX_REPLY_CHARS - 1] + "…"
    reply = ET.Element("assistant_reply", {"kind": kind})
    ET.SubElement(reply, "text").text = text
    return reply


def interpret_response(document, mode, api_key):
    if (not isinstance(document, dict) or document.get("status") != "completed"
            or document.get("error") is not None or document.get("incomplete_details") is not None
            or not isinstance(document.get("output"), list)):
        raise BrokerError("DeepSeek 没有完整返回结果，请重试。")
    messages, calls = [], []
    for item in document["output"]:
        if not isinstance(item, dict) or item.get("status") not in (None, "completed"):
            raise BrokerError(INVALID_RESPONSE)
        kind = item.get("type")
        if kind == "function_call":
            calls.append(item)
        elif kind == "message":
            if item.get("role", "assistant") != "assistant" or not isinstance(item.get("content"), list):
                raise BrokerError(INVALID_RESPONSE)
            parts = []
            for part in item["content"]:
                if (not isinstance(part, dict) or part.get("type") != "output_text"
                        or not isinstance(part.get("text"), str)):
                    raise BrokerError(INVALID_RESPONSE)
                parts.append(part["text"])
            messages.append("".join(parts))
        elif kind != "reasoning":
            raise BrokerError(INVALID_RESPONSE)

    if calls:
        if (mode != "apply" or len(calls) != 1 or calls[0].get("name") != "append_circuit"
                or not isinstance(calls[0].get("call_id"), str) or not calls[0]["call_id"].strip()):
            raise BrokerError(INVALID_ACTION)
        return parse_action(calls[0].get("arguments"))
    message = "\n".join(messages).strip()
    if not message:
        raise BrokerError(INVALID_RESPONSE)
    return text_reply("text", message.replace(api_key, "[已隐藏]"))


def run_turn(mode, question):
    api_key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if not api_key:
        return text_reply("error", "本地服务没有设置 DEEPSEEK_API_KEY。")
    if not re.fullmatch(r"[!-~]{1,1024}", api_key):
        return text_reply("error", "DEEPSEEK_API_KEY 的格式无效。")
    model = os.environ.get("DEEPSEEK_MODEL", "").strip() or DEFAULT_MODEL
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", model):
        return text_reply("error", "DEEPSEEK_MODEL 的格式无效。")
    question = question.replace(api_key, "[已隐藏]")
    try:
        return interpret_response(fetch_deepseek(make_payload(mode, question, model), api_key), mode, api_key)
    except BrokerError as error:
        return text_reply("error", str(error))


class AssistantHandler(http.server.BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, _format, *args):
        # Avoid logging requests, user questions and incoming header values.
        pass

    def send_error(self, code, message=None, explain=None):
        if code == 501:  # BaseHTTPRequestHandler uses 501 for an unsupported method.
            code = 405
        self.send_reply(code, text_reply("error", "无效的助手请求。"))

    def send_reply(self, code, reply):
        body = ET.tostring(reply, encoding="utf-8", xml_declaration=True)
        self.close_connection = True
        try:
            self.send_response(code)
            self.send_header("Content-Type", "application/xml; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Connection", "close")
            if code == 405:
                self.send_header("Allow", "POST")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
        except OSError:
            pass  # The client may have canceled or closed the window.

    def do_POST(self):
        port = self.server.server_address[1]
        hosts = {f"{HOST}:{port}", f"localhost:{port}"}
        host_values = self.headers.get_all("Host", [])
        if (self.client_address[0] != HOST or len(host_values) != 1
                or host_values[0].lower() not in hosts or "Origin" in self.headers
                or "Authorization" in self.headers or "X-Api-Key" in self.headers):
            self.send_error(403)
            return
        if self.path != TURN_PATH:
            self.send_error(404)
            return
        if "Transfer-Encoding" in self.headers or "Content-Encoding" in self.headers:
            self.send_error(400)
            return
        if (len(self.headers.get_all("Content-Type", [])) != 1
                or self.headers.get_content_type() not in ("application/xml", "text/xml")
                or self.headers.get_content_charset("utf-8") != "utf-8"):
            self.send_error(415)
            return
        length = self.headers.get_all("Content-Length", [])
        if len(length) != 1 or not re.fullmatch(r"[0-9]{1,10}", length[0]):
            self.send_error(400)
            return
        count = int(length[0])
        if count > MAX_REQUEST_BYTES:
            self.send_error(413)
            return
        try:
            data = self.rfile.read(count)
            if len(data) != count:
                raise ValueError("truncated request")
            mode, question = parse_turn(data)
        except (ValueError, ET.ParseError, OSError):
            self.send_error(400)
            return
        self.send_reply(200, run_turn(mode, question))


class AssistantServer(http.server.HTTPServer):
    def handle_error(self, request, client_address):
        # Do not print a traceback that might include an upstream exception message.
        print("A local assistant request failed.", file=sys.stderr)


def make_server(port=PORT):
    return AssistantServer((HOST, port), AssistantHandler)


def main(argv=None):
    global _auto_proxy_mode
    parser = argparse.ArgumentParser(description="启动 wx_demo 的 DeepSeek 本地服务。")
    parser.add_argument("--check-network", action="store_true", help="不使用密钥检查 DeepSeek 连接")
    args = parser.parse_args(argv)
    try:
        mode = configured_proxy_mode()
    except BrokerError as error:
        print(str(error), file=sys.stderr)
        return 2
    if args.check_network:
        modes = ("system", "direct") if mode == "auto" else (mode,)
        reachable = False
        for candidate in modes:
            ok, message = probe_network(candidate)
            print(f"{network_label(candidate)}：{message}", flush=True)
            reachable = reachable or ok
        return 0 if reachable else 1
    try:
        with make_server() as server:
            if mode == "auto":
                print("正在检查 DeepSeek 连接（不使用密钥）…", flush=True)
                _auto_proxy_mode = choose_auto_proxy()
                mode = _auto_proxy_mode
            print(f"电路助手本地服务：http://{HOST}:{PORT}；DeepSeek：{network_label(mode)}", flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        return 0
    except OSError:
        print("无法启动电路助手本地服务，请检查端口是否被占用。", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
