"""Run with python -B -m unittest discover -s tests -p test_assistant_server.py."""

import copy
import http.client
import io
import json
import socket
import ssl
import threading
import unittest
import urllib.error
import xml.etree.ElementTree as ET
from unittest import mock

import assistant_server as broker


FAKE_KEY = "test-key-only-never-sent-to-a-real-service"
GUIDE_XML = '<?xml version="1.0" encoding="UTF-8"?><assistant_request mode="guide"><question>与门 &amp; 或门</question></assistant_request>'
APPLY_XML = '<assistant_request mode="apply"><question>画一个与门电路</question></assistant_request>'
ACTION = {
    "components": [
        {"ref": "a", "type": "INPUT", "inputCount": 0},
        {"ref": "b", "type": "INPUT", "inputCount": 0},
        {"ref": "g", "type": "AND", "inputCount": 2},
        {"ref": "y", "type": "OUTPUT", "inputCount": 1},
    ],
    "wires": [
        {"from": "a", "outPin": "OUT", "to": "g", "inPin": "A"},
        {"from": "b", "outPin": "OUT", "to": "g", "inPin": "B"},
        {"from": "g", "outPin": "OUT", "to": "y", "inPin": "IN"},
    ],
}


def text_output(value):
    return {"type": "message", "role": "assistant", "status": "completed",
            "content": [{"type": "output_text", "text": value}]}


def function_output(action=ACTION, **overrides):
    item = {"type": "function_call", "name": "append_circuit", "status": "completed",
            "call_id": "mock-call", "arguments": json.dumps(action)}
    item.update(overrides)
    return item


def completed(*items):
    return {"status": "completed", "error": None, "incomplete_details": None, "output": list(items)}


class AssistantServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = broker.make_server(0)
        cls.addClassCleanup(cls.server.server_close)
        cls.worker = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.worker.start()
        cls.addClassCleanup(cls.worker.join, 5)
        cls.addClassCleanup(cls.server.shutdown)

    def setUp(self):
        environment = mock.patch.object(broker.os, "environ", {"DEEPSEEK_API_KEY": FAKE_KEY})
        self.addCleanup(environment.stop)
        environment.start()
        # Every test intercepts the actual urllib boundary. No public endpoint is contacted.
        upstream = mock.patch.object(broker.urllib.request, "build_opener")
        self.addCleanup(upstream.stop)
        self.build_opener = upstream.start()
        self.open_upstream = self.build_opener.return_value.open

    def provide(self, value):
        self.open_upstream.return_value = io.BytesIO(json.dumps(value).encode("utf-8"))

    def post(self, xml, headers=None, path=broker.TURN_PATH, method="POST"):
        if isinstance(xml, str):
            xml = xml.encode("utf-8")
        supplied = {"Content-Type": "application/xml; charset=UTF-8"}
        supplied.update(headers or {})
        connection = http.client.HTTPConnection(broker.HOST, self.server.server_port, timeout=4)
        try:
            connection.request(method, path, body=xml, headers=supplied)
            response = connection.getresponse()
            data = response.read()
            self.assertEqual(response.getheader("Content-Type"), "application/xml; charset=utf-8")
            self.assertEqual(response.getheader("Cache-Control"), "no-store")
            self.assertLessEqual(len(data), 64 * 1024)
            return response.status, ET.fromstring(data), data
        finally:
            connection.close()

    def assert_kind(self, status, reply, kind):
        self.assertEqual(status, 200)
        self.assertEqual(reply.tag, "assistant_reply")
        self.assertEqual(reply.attrib, {"kind": kind})
        if kind in ("error", "text"):
            self.assertEqual([node.tag for node in reply], ["text"])
            self.assertTrue(reply.findtext("text"))

    def test_guide_uses_responses_http_contract_and_has_no_tools(self):
        value = "<步骤> 输入 → 与门 & 输出"
        self.provide(completed(text_output(value)))
        status, reply, data = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "text")
        self.assertEqual(reply.findtext("text"), value)
        self.assertIn(b"&lt;", data)
        self.open_upstream.assert_called_once()
        (request,), options = self.open_upstream.call_args
        self.assertEqual(request.full_url, "https://api.deepseek.com/responses")
        self.assertEqual(request.get_method(), "POST")
        self.assertEqual(request.get_header("Authorization"), "Bearer " + FAKE_KEY)
        self.assertEqual(request.get_header("Content-type"), "application/json")
        self.assertEqual(options, {"timeout": broker.UPSTREAM_TIMEOUT})
        body = json.loads(request.data)
        self.assertEqual(body["input"], "与门 & 或门")
        self.assertEqual(body["model"], "deepseek-flash")
        self.assertEqual(body["reasoning"], {"effort": "none"})
        self.assertIs(body["stream"], False)
        self.assertNotIn("tools", body)
        self.assertNotIn(FAKE_KEY.encode(), request.data)

    def test_apply_uses_flat_tool_and_returns_atomic_xml(self):
        broker.os.environ["DEEPSEEK_MODEL"] = "deepseek-v4-pro"
        self.provide(completed(text_output("正在构建"), function_output()))
        status, reply, _ = self.post(APPLY_XML)
        self.assert_kind(status, reply, "action")
        self.assertEqual([node.tag for node in reply], ["components", "wires"])
        self.assertEqual([node.attrib for node in reply.findall("components/component")],
                         [{**part, "inputCount": str(part["inputCount"])} for part in ACTION["components"]])
        self.assertEqual([node.attrib for node in reply.findall("wires/wire")], ACTION["wires"])
        body = json.loads(self.open_upstream.call_args.args[0].data)
        self.assertEqual(body["model"], "deepseek-v4-pro")
        self.assertNotIn("parallel_tool_calls", body)
        self.assertEqual(len(body["tools"]), 1)
        tool = body["tools"][0]
        self.assertEqual(tool["type"], "function")
        self.assertEqual(tool["name"], "append_circuit")
        self.assertIn("parameters", tool)
        self.assertNotIn("function", tool)
        self.assertNotIn("strict", tool)

    def test_plain_output_is_never_promoted_to_action(self):
        self.provide(completed(text_output(json.dumps(ACTION))))
        status, reply, _ = self.post(APPLY_XML)
        self.assert_kind(status, reply, "text")

    def test_component_only_action_keeps_empty_wires_container(self):
        self.provide(completed(function_output({"components": ACTION["components"][:1], "wires": []})))
        status, reply, _ = self.post(APPLY_XML)
        self.assert_kind(status, reply, "action")
        self.assertIsNotNone(reply.find("wires"))
        self.assertEqual(len(reply.find("wires")), 0)

    def test_incomplete_unknown_and_multiple_calls_fail_closed(self):
        cases = [
            (GUIDE_XML, completed(function_output())),
            (APPLY_XML, completed(function_output(), function_output(call_id="second"))),
            (APPLY_XML, completed(function_output(name="delete_circuit"))),
            (APPLY_XML, completed(function_output(status="incomplete"))),
            (APPLY_XML, completed(function_output(arguments="{"))),
            (APPLY_XML, completed(function_output(call_id=""))),
            (APPLY_XML, {**completed(function_output()), "status": "incomplete"}),
            (APPLY_XML, completed(function_output(), {"type": "custom_tool_call", "name": "unexpected"})),
        ]
        for xml, upstream in cases:
            with self.subTest(upstream=upstream):
                self.provide(upstream)
                status, reply, _ = self.post(xml)
                self.assert_kind(status, reply, "error")

    def test_invalid_connections_and_schema_do_not_return_action(self):
        bad_count = copy.deepcopy(ACTION)
        bad_count["components"][0]["inputCount"] = False
        bad_pin = copy.deepcopy(ACTION)
        bad_pin["wires"][0]["inPin"] = "C"
        bad_ref = copy.deepcopy(ACTION)
        bad_ref["wires"][0]["from"] = "existing"
        bad_token = {"components": [{"ref": "1bad", "type": "INPUT", "inputCount": 0}], "wires": []}
        multi_driver = copy.deepcopy(ACTION)
        multi_driver["wires"][1]["inPin"] = "A"
        cycle = {"components": [{"ref": ref, "type": "NOT", "inputCount": 1} for ref in ("a", "b")],
                 "wires": [{"from": a, "outPin": "OUT", "to": b, "inPin": "IN"}
                           for a, b in (("a", "b"), ("b", "a"))]}
        for action in [bad_count, bad_pin, bad_ref, bad_token, multi_driver, cycle,
                       {**ACTION, "filename": "unexpected"}]:
            with self.subTest(action=action):
                self.provide(completed(function_output(action)))
                status, reply, _ = self.post(APPLY_XML)
                self.assert_kind(status, reply, "error")

    def test_duplicate_json_keys_and_oversize_upstream_body_are_rejected(self):
        self.open_upstream.return_value = io.BytesIO(b'{"status":"failed","status":"completed","output":[]}')
        status, reply, _ = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "error")
        response = mock.Mock(wraps=io.BytesIO(b"x" * (broker.MAX_UPSTREAM_BYTES + 20)))
        self.open_upstream.return_value = mock.MagicMock()
        self.open_upstream.return_value.__enter__.return_value = response
        status, reply, _ = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "error")
        response.read.assert_called_once_with(broker.MAX_UPSTREAM_BYTES + 1)

    def test_missing_key_makes_no_upstream_request(self):
        del broker.os.environ["DEEPSEEK_API_KEY"]
        status, reply, _ = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "error")
        self.assertIn("DEEPSEEK_API_KEY", reply.findtext("text"))
        self.build_opener.assert_not_called()

    def test_upstream_http_error_and_timeout_are_safe(self):
        upstream_body = io.BytesIO(("raw response contains " + FAKE_KEY).encode())
        self.open_upstream.side_effect = urllib.error.HTTPError(
            broker.DEEPSEEK_URL, 401, FAKE_KEY, {}, upstream_body)
        status, reply, data = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "error")
        self.assertNotIn(FAKE_KEY.encode(), data)
        self.assertNotIn(b"raw response", data)
        self.assertTrue(upstream_body.closed)
        self.open_upstream.side_effect = urllib.error.URLError(TimeoutError(FAKE_KEY))
        status, reply, data = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "error")
        self.assertIn("超时", reply.findtext("text"))
        self.assertNotIn(FAKE_KEY.encode(), data)

    def test_output_text_is_xml_safe_and_secret_is_redacted(self):
        self.provide(completed(text_output("value\x00\ud800<& " + FAKE_KEY)))
        status, reply, data = self.post(GUIDE_XML)
        self.assert_kind(status, reply, "text")
        self.assertEqual(reply.findtext("text"), "value\ufffd\ufffd<& [已隐藏]")
        self.assertNotIn(FAKE_KEY.encode(), data)

    def test_local_transport_rejects_unexpected_host_origin_and_content_type(self):
        cases = [({"Host": "foreign.example"}, 403), ({"Origin": "https://foreign.example"}, 403),
                 ({"Authorization": "Bearer inbound"}, 403), ({"Content-Type": "text/plain"}, 415),
                 ({"Transfer-Encoding": "chunked"}, 400),
                 ({"Content-Length": str(broker.MAX_REQUEST_BYTES + 1)}, 413)]
        for headers, code in cases:
            with self.subTest(headers=headers):
                status, reply, _ = self.post(GUIDE_XML, headers=headers)
                self.assertEqual(status, code)
                self.assertEqual(reply.get("kind"), "error")
        self.build_opener.assert_not_called()

    def test_local_transport_rejects_malformed_and_extended_xml(self):
        cases = [b"\xff", "<assistant_request>",
                 '<!DOCTYPE assistant_request []>' + APPLY_XML,
                 APPLY_XML.replace('mode="apply"', 'mode="apply" other="unexpected"'),
                 APPLY_XML.replace("</question>", "<nested/></question>"),
                 APPLY_XML.replace("画一个与门电路", " "),
                 APPLY_XML.replace("画一个与门电路", "x" * (broker.MAX_QUESTION_CHARS + 1))]
        for xml in cases:
            with self.subTest(xml=xml if len(xml) < 200 else "long input"):
                status, reply, _ = self.post(xml)
                self.assertEqual(status, 400)
                self.assertEqual(reply.get("kind"), "error")
        self.build_opener.assert_not_called()

    def test_only_post_to_the_turn_path_is_supported(self):
        status, reply, _ = self.post(GUIDE_XML, path="/other")
        self.assertEqual(status, 404)
        self.assertEqual(reply.get("kind"), "error")
        status, reply, _ = self.post(b"", method="GET")
        self.assertEqual(status, 405)
        self.assertEqual(reply.get("kind"), "error")
        self.build_opener.assert_not_called()


class AssistantNetworkTests(unittest.TestCase):
    def test_proxy_configuration_and_invalid_values(self):
        with mock.patch.object(broker.os, "environ", {}):
            self.assertEqual(broker.configured_proxy_mode(), "auto")
            for mode in ("auto", "system", "direct", "http://127.0.0.1:8080"):
                with self.subTest(mode=mode):
                    broker.os.environ["DEEPSEEK_PROXY"] = mode
                    self.assertEqual(broker.configured_proxy_mode(), mode)
            for invalid in ("socks5://" + FAKE_KEY + "@127.0.0.1:1080", "http://127.0.0.1:not-a-port"):
                broker.os.environ["DEEPSEEK_PROXY"] = invalid
                with self.assertRaises(broker.BrokerError) as caught:
                    broker.configured_proxy_mode()
                self.assertNotIn(FAKE_KEY, str(caught.exception))
                self.assertNotIn(invalid, str(caught.exception))

    def test_system_direct_and_explicit_modes_choose_the_expected_handlers(self):
        proxy = "http://127.0.0.1:8080"
        with mock.patch.object(broker.urllib.request, "build_opener") as build:
            for mode in ("system", "direct", proxy):
                with self.subTest(mode=mode):
                    self.assertIs(broker.network_opener(mode), build.return_value)
                    handlers = [handler for handler in build.call_args.args
                                if isinstance(handler, broker.urllib.request.ProxyHandler)]
                    if mode == "system":
                        self.assertEqual(handlers, [])
                    elif mode == "direct":
                        self.assertEqual([handler.proxies for handler in handlers], [{}])
                    else:
                        self.assertEqual([handler.proxies.get("https") for handler in handlers], [proxy])

    def test_probe_uses_unauthenticated_models_get_and_401_means_reachable(self):
        with mock.patch.object(broker.os, "environ", {"DEEPSEEK_API_KEY": FAKE_KEY}), \
                mock.patch.object(broker, "network_opener") as opener:
            opener.return_value.open.side_effect = urllib.error.HTTPError(
                "https://api.deepseek.com/models", 401, FAKE_KEY, {}, io.BytesIO(FAKE_KEY.encode()))
            reachable, message = broker.probe_network("direct")
            self.assertTrue(reachable)
            self.assertNotIn(FAKE_KEY, message)
            opener.assert_called_once_with("direct")
            opener.return_value.open.assert_called_once()
            request = opener.return_value.open.call_args.args[0]
            self.assertEqual(request.full_url, "https://api.deepseek.com/models")
            self.assertEqual(request.get_method(), "GET")
            self.assertIsNone(request.data)
            self.assertNotIn("authorization", {name.lower() for name, _ in request.header_items()})

    def test_probe_reports_transport_failure_without_upstream_details(self):
        with mock.patch.object(broker, "network_opener") as opener:
            opener.return_value.open.side_effect = urllib.error.URLError(TimeoutError(FAKE_KEY))
            reachable, message = broker.probe_network("system")
            self.assertFalse(reachable)
            self.assertIn("超时", message)
            self.assertNotIn(FAKE_KEY, message)

    def test_auto_tries_direct_only_when_system_probe_cannot_connect(self):
        with mock.patch.object(broker, "probe_network", return_value=(True, "reachable")) as probe:
            self.assertEqual(broker.choose_auto_proxy(), "system")
            probe.assert_called_once_with("system")
        with mock.patch.object(broker, "probe_network", side_effect=[(False, "offline"), (True, "reachable")]) as probe:
            self.assertEqual(broker.choose_auto_proxy(), "direct")
            self.assertEqual(probe.call_args_list, [mock.call("system"), mock.call("direct")])

    def test_network_errors_have_specific_safe_messages(self):
        cases = [
            (socket.gaierror(11001, FAKE_KEY), ("DNS", "解析")),
            (ssl.SSLCertVerificationError(1, FAKE_KEY), ("证书", "CERT")),
            (OSError("Tunnel connection failed: 407 " + FAKE_KEY), ("代理", "407")),
            (ConnectionRefusedError(10061, FAKE_KEY), ("拒绝", "代理")),
            (TimeoutError(FAKE_KEY), ("超时",)),
            (http.client.RemoteDisconnected(FAKE_KEY), ("断开", "断线", "中断", "关闭", "重置")),
        ]
        for reason, expected in cases:
            with self.subTest(exception=type(reason).__name__):
                message = broker.describe_network_error(urllib.error.URLError(reason))
                self.assertTrue(any(fragment in message for fragment in expected), message)
                self.assertNotIn(FAKE_KEY, message)


if __name__ == "__main__":
    unittest.main()
