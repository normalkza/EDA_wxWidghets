import ctypes
from ctypes import wintypes
import io
import json
import unittest
import urllib.error
import urllib.request
from unittest import mock

import assistant_server as broker
import assistant_winhttp as native


def number(pointer, value):
    ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD)).contents.value = value


class FakeWindowsApi:
    def __init__(self, status=200, data=b'{"status":"completed"}'):
        self.data = io.BytesIO(data)
        self.options = []
        self.WinHttpOpen = mock.Mock(return_value=1)
        self.WinHttpConnect = mock.Mock(return_value=2)
        self.WinHttpOpenRequest = mock.Mock(return_value=3)
        self.WinHttpCloseHandle = mock.Mock(return_value=1)
        self.WinHttpSetTimeouts = mock.Mock(return_value=1)
        self.WinHttpSetOption = mock.Mock(side_effect=self.set_option)
        self.WinHttpSendRequest = mock.Mock(return_value=1)
        self.WinHttpReceiveResponse = mock.Mock(return_value=1)
        self.WinHttpQueryHeaders = mock.Mock(side_effect=lambda _handle, _flags, _name, buffer, _size, _index:
                                             (number(buffer, status), 1)[1])
        self.WinHttpReadData = mock.Mock(side_effect=self.read)

    def set_option(self, handle, option, pointer, _length):
        self.options.append((handle, option, ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD)).contents.value))
        return 1

    def read(self, _handle, buffer, size, count):
        data = self.data.read(size)
        ctypes.memmove(buffer, data, len(data)); number(count, len(data))
        return 1


class WindowsTransportTests(unittest.TestCase):
    def request(self):
        return urllib.request.Request("https://api.deepseek.com/responses", data='{"input":"电路"}'.encode(),
                                      headers={"Authorization": "Bearer sk-placeholder-test", "Content-Type": "application/json"})

    def test_bytes_headers_status_and_bounded_read_use_native_api(self):
        api = FakeWindowsApi(data=b"0123456789")
        with native.WindowsOpener("direct", api).open(self.request(), timeout=3) as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(response.read(6), b"012345")
            self.assertEqual(response.read(8), b"6789")
        sent = api.WinHttpSendRequest.call_args.args
        self.assertIn("Authorization: Bearer sk-placeholder-test\r\n", sent[1])
        self.assertEqual(ctypes.string_at(sent[3], sent[4]), '{"input":"电路"}'.encode())
        self.assertNotIn(b"sk-placeholder", ctypes.string_at(sent[3], sent[4]))
        self.assertEqual(api.WinHttpOpen.call_args.args[1], 1)
        self.assertEqual(api.WinHttpConnect.call_args.args[1:3], ("api.deepseek.com", 443))
        self.assertEqual(api.WinHttpOpenRequest.call_args.args[-1], 0x00800000)
        self.assertIn((1, 84, 0x2800), api.options)
        self.assertIn((3, 88, 0), api.options)
        self.assertIn((3, 77, 2), api.options)
        self.assertFalse(any(option == 31 for _, option, _ in api.options))  # No certificate bypass.
        self.assertEqual(api.WinHttpCloseHandle.call_args_list, [mock.call(3), mock.call(2), mock.call(1)])

    def test_http_error_and_redirect_are_not_followed_or_read(self):
        for status in (302, 401, 403, 429, 502):
            with self.subTest(status=status):
                api = FakeWindowsApi(status, b"private error body")
                with self.assertRaises(urllib.error.HTTPError) as caught:
                    native.WindowsOpener("direct", api).open(self.request(), timeout=3)
                self.assertEqual(caught.exception.code, status)
                self.assertNotIn("private", str(caught.exception))
                caught.exception.close()
                api.WinHttpSendRequest.assert_called_once()
                api.WinHttpReadData.assert_not_called()
                self.assertEqual(api.WinHttpCloseHandle.call_count, 3)

    def test_failed_send_closes_handles_without_replaying_payload(self):
        api = FakeWindowsApi()
        api.WinHttpSendRequest.return_value = 0
        with mock.patch.object(native.ctypes, "get_last_error", return_value=12175):
            with self.assertRaises(native.WinHttpError) as caught:
                native.WindowsOpener("direct", api).open(self.request(), timeout=3)
        self.assertEqual(caught.exception.winerror, 12175)
        self.assertEqual(caught.exception.stage, "send")
        self.assertIn("12175", broker.describe_network_error(caught.exception))
        self.assertNotIn("sk-placeholder", broker.describe_network_error(caught.exception))
        api.WinHttpSendRequest.assert_called_once()
        self.assertEqual(api.WinHttpCloseHandle.call_count, 3)

    def test_response_read_failure_also_closes_every_handle(self):
        api = FakeWindowsApi()
        api.WinHttpReadData.return_value = 0
        api.WinHttpReadData.side_effect = None
        with mock.patch.object(native.ctypes, "get_last_error", return_value=12030):
            with self.assertRaises(native.WinHttpError):
                with native.WindowsOpener("direct", api).open(self.request(), timeout=3) as response:
                    response.read(200)
        self.assertEqual(api.WinHttpCloseHandle.call_count, 3)

    def test_credentials_cannot_leave_pinned_https_endpoints(self):
        api = FakeWindowsApi()
        for url in ("http://api.deepseek.com/responses", "https://foreign.example/responses",
                    "https://api.deepseek.com:8443/responses", "https://api.deepseek.com/responses?key=abc",
                    "https://api.deepseek.com/files", "https://user:pass@api.deepseek.com/responses"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                native.WindowsOpener("direct", api).open(urllib.request.Request(url), timeout=3)
        api.WinHttpOpen.assert_not_called()

    def test_system_explicit_and_direct_proxy_settings_are_preserved(self):
        with mock.patch.object(native.urllib.request, "getproxies", return_value={"https": "http://127.0.0.1:7890"}), \
                mock.patch.object(native.urllib.request, "proxy_bypass", return_value=False):
            self.assertEqual(native.proxy_options("system"), (3, "127.0.0.1:7890"))
            self.assertEqual(native.proxy_options("http://localhost:8080"), (3, "localhost:8080"))
            self.assertEqual(native.proxy_options("direct"), (1, None))
        with mock.patch.object(native.urllib.request, "getproxies", return_value={}), \
                mock.patch.object(native.urllib.request, "proxy_bypass", return_value=False):
            self.assertEqual(native.proxy_options("system"), (4, None))

    def test_broker_selects_native_transport_and_preserves_api_error_mapping(self):
        with mock.patch.object(broker, "USE_WINHTTP", True), mock.patch.object(native, "WindowsOpener") as opener, \
                mock.patch.object(broker, "request_proxy_mode", return_value="direct"):
            opener.return_value.open.side_effect = urllib.error.HTTPError(broker.DEEPSEEK_URL, 401, "ignored", {}, None)
            with self.assertRaises(broker.BrokerError) as caught:
                broker.fetch_deepseek({"input": "test"}, "sk-placeholder-test")
            self.assertIn("密钥", str(caught.exception))
            self.assertNotIn("sk-placeholder", str(caught.exception))
            opener.assert_called_once_with("direct")
            opener.return_value.open.assert_called_once()


if __name__ == "__main__": unittest.main()
