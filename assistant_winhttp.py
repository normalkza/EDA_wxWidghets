"""Small urllib-compatible WinHTTP transport using Windows TLS and trust stores."""

import ctypes
from ctypes import wintypes
import urllib.error
import urllib.parse
import urllib.request

API_HOST = "api.deepseek.com"
API_PATHS = {"/models", "/responses"}
_dll = None


class WinHttpError(OSError):
    def __init__(self, stage, code):
        super().__init__(code, "Windows HTTPS request failed")
        self.stage = stage
        self.winerror = code


def bindings():
    global _dll
    if _dll is not None:
        return _dll
    dll = ctypes.WinDLL("winhttp", use_last_error=True)
    handle, dword = wintypes.HANDLE, wintypes.DWORD
    pointer, text = ctypes.c_void_p, wintypes.LPCWSTR
    signatures = {
        "WinHttpOpen": ([text, dword, text, text, dword], handle),
        "WinHttpConnect": ([handle, text, wintypes.WORD, dword], handle),
        "WinHttpOpenRequest": ([handle, text, text, text, text, ctypes.POINTER(text), dword], handle),
        "WinHttpSetTimeouts": ([handle, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int], wintypes.BOOL),
        "WinHttpSetOption": ([handle, dword, pointer, dword], wintypes.BOOL),
        "WinHttpSendRequest": ([handle, text, dword, pointer, dword, dword, ctypes.c_size_t], wintypes.BOOL),
        "WinHttpReceiveResponse": ([handle, pointer], wintypes.BOOL),
        "WinHttpQueryHeaders": ([handle, dword, text, pointer, ctypes.POINTER(dword), ctypes.POINTER(dword)], wintypes.BOOL),
        "WinHttpReadData": ([handle, pointer, dword, ctypes.POINTER(dword)], wintypes.BOOL),
        "WinHttpCloseHandle": ([handle], wintypes.BOOL),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(dll, name)
        function.argtypes = arguments
        function.restype = result
    _dll = dll
    return dll


def checked(value, stage):
    if not value:
        raise WinHttpError(stage, ctypes.get_last_error())
    return value


def proxy_options(mode):
    if mode == "direct":
        return 1, None
    if mode == "system":
        if urllib.request.proxy_bypass(API_HOST):
            return 1, None
        proxy = urllib.request.getproxies().get("https")
        if not proxy:
            # Use per-user/system settings, including automatic proxy config.
            return 4, None
    else:
        proxy = mode
    parsed = urllib.parse.urlsplit(proxy if "://" in proxy else "http://" + proxy)
    if (parsed.scheme != "http" or not parsed.hostname or parsed.username is not None or parsed.password is not None
            or parsed.path not in ("", "/") or parsed.query or parsed.fragment):
        raise ValueError("unsupported HTTP proxy")
    host = parsed.hostname
    if ":" in host:
        host = "[" + host + "]"
    return 3, f"{host}:{parsed.port or 80}"


class WindowsResponse:
    def __init__(self, api, handles, status):
        self.api = api
        self.handles = handles
        self.status = status

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        while self.handles:
            self.api.WinHttpCloseHandle(self.handles.pop())

    def read(self, count):
        if not self.handles or count <= 0:
            return b""
        chunks = []
        while count:
            size = min(count, 65536)
            buffer = ctypes.create_string_buffer(size)
            received = wintypes.DWORD()
            checked(self.api.WinHttpReadData(self.handles[-1], buffer, size, ctypes.byref(received)), "read")
            if not received.value:
                break
            if received.value > size:
                raise WinHttpError("read-size", 13)
            chunks.append(buffer.raw[:received.value])
            count -= received.value
        return b"".join(chunks)


class WindowsOpener:
    def __init__(self, mode, api=None):
        self.mode = mode
        self.api = api if api is not None else bindings()

    def open(self, request, timeout):
        url = urllib.parse.urlsplit(request.full_url)
        if (url.scheme != "https" or url.hostname != API_HOST or url.port not in (None, 443)
                or url.username is not None or url.password is not None or url.query or url.fragment
                or url.path not in API_PATHS or request.get_method() not in ("GET", "POST")):
            raise ValueError("unsupported DeepSeek endpoint")
        body = request.data or b""
        if not isinstance(body, bytes) or len(body) > 4 * 1024 * 1024:
            raise ValueError("invalid request body")
        headers = request.header_items()
        if any("\r" in name or "\n" in name or "\r" in value or "\n" in value for name, value in headers):
            raise ValueError("invalid request headers")
        api, handles = self.api, []
        try:
            access, proxy = proxy_options(self.mode)
            session = checked(api.WinHttpOpen("wx_demo DeepSeek/2", access, proxy, None, 0), "open")
            handles.append(session)
            milliseconds = max(1, int(timeout * 1000))
            checked(api.WinHttpSetTimeouts(session, milliseconds, milliseconds, milliseconds, milliseconds), "timeouts")
            protocols = wintypes.DWORD(0x0800 | 0x2000)  # TLS 1.2 and TLS 1.3 only.
            if not api.WinHttpSetOption(session, 84, ctypes.byref(protocols), ctypes.sizeof(protocols)):
                protocols.value = 0x0800
                checked(api.WinHttpSetOption(session, 84, ctypes.byref(protocols), ctypes.sizeof(protocols)), "TLS-options")
            connection = checked(api.WinHttpConnect(session, API_HOST, 443, 0), "connect")
            handles.append(connection)
            message = checked(api.WinHttpOpenRequest(connection, request.get_method(), url.path,
                              None, None, None, 0x00800000), "request")
            handles.append(message)
            never_redirect = wintypes.DWORD(0)
            checked(api.WinHttpSetOption(message, 88, ctypes.byref(never_redirect), ctypes.sizeof(never_redirect)), "redirect-policy")
            no_auto_logon = wintypes.DWORD(2)
            checked(api.WinHttpSetOption(message, 77, ctypes.byref(no_auto_logon), ctypes.sizeof(no_auto_logon)), "logon-policy")
            header_text = "".join(name + ": " + value + "\r\n" for name, value in headers)
            buffer = ctypes.create_string_buffer(body) if body else None
            checked(api.WinHttpSendRequest(message, header_text, len(header_text), buffer, len(body), len(body), 0), "send")
            checked(api.WinHttpReceiveResponse(message, None), "receive")
            status, size = wintypes.DWORD(), wintypes.DWORD(ctypes.sizeof(wintypes.DWORD))
            checked(api.WinHttpQueryHeaders(message, 19 | 0x20000000, None, ctypes.byref(status), ctypes.byref(size), None), "status")
            response = WindowsResponse(api, handles, status.value)
            if status.value >= 300:
                # Do not read or expose an error body, and never follow a redirect.
                response.close()
                raise urllib.error.HTTPError(request.full_url, status.value, "DeepSeek HTTP error", {}, None)
            return response
        except BaseException:
            while handles:
                api.WinHttpCloseHandle(handles.pop())
            raise
