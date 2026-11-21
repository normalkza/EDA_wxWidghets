import base64
import io
import json
import ssl
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock
import xml.etree.ElementTree as ET

import assistant_server as broker
import assistant_skills as skills

KEY = "sk-skill-test-placeholder"
PNG = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aKbkAAAAASUVORK5CYII="


def request(mode="apply", phase="edit"):
    root = ET.Element("assistant_request", {"mode": mode})
    ET.SubElement(root, "question").text = "直接新建"
    context = ET.SubElement(root, "context", {"phase": phase})
    scene = ET.SubElement(context, "workspace", {"width": "1000", "height": "700", "revision": "12",
        "selectedComponent": "c2", "selectedWire": "", "selectedText": "", "grid": "20"})
    caps = ET.SubElement(scene, "capabilities")
    ET.SubElement(caps, "type", {"name": "INPUT", "minInputs": "0", "maxInputs": "0", "inputs": "", "outputs": "OUT"})
    components = ET.SubElement(scene, "components")
    part = ET.SubElement(components, "component", {"ref": "c2", "type": "INPUT", "x": "40", "y": "80", "rotation": "0", "inputCount": "0"})
    ET.SubElement(part, "bounds", {"x": "35", "y": "75", "width": "51", "height": "51"})
    ET.SubElement(part, "pin", {"name": "OUT", "direction": "output", "x": "80", "y": "100", "value": "1"})
    ET.SubElement(scene, "wires")
    texts = ET.SubElement(scene, "texts")
    ET.SubElement(texts, "text", {"ref": "t0", "x": "30", "y": "30", "width": "80", "height": "30", "pointSize": "14"}).text = "A 输入"
    ET.SubElement(context, "image").text = PNG
    history = ET.SubElement(context, "history")
    ET.SubElement(history, "message", {"role": "user"}).text = "需要一个全加器"
    ET.SubElement(history, "message", {"role": "assistant"}).text = "可以用两组异或门计算 S，并用与或门计算 COUT。"
    ET.SubElement(context, "feedback").text = "执行成功，检查最新画布。" if phase == "review" else ""
    return root


def response(*output):
    return {"status": "completed", "error": None, "incomplete_details": None, "output": list(output)}


def call(operations, **changes):
    value = {"type": "function_call", "name": "edit_circuit", "status": "completed", "call_id": "edit1",
             "arguments": json.dumps({"summary": "调整已有元件并添加标注", "operations": operations})}
    value.update(changes)
    return value


class SkillProtocolTests(unittest.TestCase):
    def setUp(self):
        transport = mock.patch.object(broker, "USE_WINHTTP", False)
        transport.start(); self.addCleanup(transport.stop)
        environment = mock.patch.object(broker.os, "environ", {"DEEPSEEK_PROXY": "direct"})
        environment.start(); self.addCleanup(environment.stop)
        patch = mock.patch.object(broker.urllib.request, "build_opener")
        self.opener = patch.start(); self.addCleanup(patch.stop)

    def run_request(self, root, upstream):
        self.opener.return_value.open.return_value = io.BytesIO(json.dumps(upstream).encode("utf-8"))
        mode, question, context = broker.parse_turn(ET.tostring(root, encoding="utf-8"))
        return broker.run_turn(mode, question, KEY, context)

    def test_context_history_image_and_edit_tool_reach_deepseek(self):
        reply = self.run_request(request(), response(call([
            {"op": "update_component", "ref": "c2", "x": 160, "rotation": 90},
            {"op": "add_text", "ref": "n_label", "x": 200, "y": 40, "width": 100, "height": 30, "text": "A & B\n输入"},
        ])))
        self.assertEqual(reply.get("kind"), "patch")
        self.assertEqual(reply.find("operations/op").attrib, {"kind": "update_component", "ref": "c2"})
        self.assertEqual(reply.findtext("operations/op[2]/field[@name='text']"), "A & B\n输入")
        sent = self.opener.return_value.open.call_args.args[0]
        self.assertEqual(sent.get_header("Authorization"), "Bearer " + KEY)
        body = json.loads(sent.data)
        self.assertEqual(body["tools"][0]["name"], "edit_circuit")
        self.assertIn("需要一个全加器", body["input"][0]["content"])
        parts = body["input"][-1]["content"]
        context = json.loads(parts[0]["text"])
        self.assertEqual(context["goal"], "直接新建")
        self.assertEqual(context["workspace"]["components"][0]["ref"], "c2")
        self.assertEqual(context["workspace"]["components"][0]["pins"][0]["value"], "1")
        self.assertEqual(parts[1]["type"], "input_image")
        self.assertEqual(parts[1]["image_url"], "data:image/png;base64," + PNG)
        self.assertNotIn(KEY.encode(), sent.data)

    def test_review_uses_fresh_coordinates_and_execution_feedback(self):
        root = request(phase="review")
        root.find("context/workspace/components/component").set("x", "200")
        reply = self.run_request(root, response({"type": "message", "role": "assistant", "content": [
            {"type": "output_text", "text": "布局检查完成"}]}))
        self.assertEqual(reply.get("kind"), "text")
        body = json.loads(self.opener.return_value.open.call_args.args[0].data)
        sent = json.loads(body["input"][-1]["content"][0]["text"])
        self.assertEqual(sent["phase"], "review")
        self.assertEqual(sent["workspace"]["components"][0]["x"], 200)
        self.assertIn("执行成功", sent["feedback"])

    def test_guide_never_exposes_or_executes_edit_tools(self):
        reply = self.run_request(request(mode="guide"), response(call([{"op": "delete_component", "ref": "c2"}])))
        self.assertEqual(reply.get("kind"), "error")
        self.assertNotIn("tools", json.loads(self.opener.return_value.open.call_args.args[0].data))

    def test_invalid_edit_operations_do_not_become_actions(self):
        cases = [
            [{"op": "run_python", "ref": "n1", "code": "print(1)"}],
            [{"op": "update_component", "ref": "c2", "x": True}],
            [{"op": "update_component", "ref": "c2", "x": 20.5}],
            [{"op": "update_component", "ref": "c2", "type": "NOT"}],
            [{"op": "update_component", "ref": "c2"}],
            [{"op": "route_wire", "ref": "w0", "points": [{"x": 1, "y": 1}, {"x": 2, "y": 2}]}],
            [{"op": "add_wire", "ref": "n1", "from": "c2", "to": "c7", "outPin": "OUT"}],
        ]
        for operations in cases:
            with self.subTest(operations=operations):
                reply = self.run_request(request(), response(call(operations)))
                self.assertEqual(reply.get("kind"), "error")

    def test_unknown_multiple_and_malformed_calls_fail_closed(self):
        valid = call([{"op": "delete_text", "ref": "t0"}])
        for upstream in [response(valid, valid), response({**valid, "name": "append_circuit"}),
                         response({**valid, "arguments": '{"summary":"a","summary":"b","operations":[]}'}),
                         {**response(valid), "status": "incomplete"}]:
            self.assertEqual(self.run_request(request(), upstream).get("kind"), "error")

    def test_context_rejects_remote_images_and_untrusted_history_roles(self):
        for field, value in [("image", "https://foreign.example/secret.png"), ("history/message", "bad")]:
            root = request()
            if field == "image": root.find("context/image").text = value
            else: root.find("context/history/message").set("role", "system")
            with self.assertRaises(ValueError): broker.parse_turn(ET.tostring(root, encoding="utf-8"))
        self.opener.assert_not_called()

    def test_skill_reloads_from_project_files_without_restart(self):
        _, _, context = broker.parse_turn(ET.tostring(request(), encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(skills, "SKILL_DIR", Path(directory)):
            folder = Path(directory)
            with self.assertRaises(ValueError): skills.make_payload("apply", "layout", "deepseek-flash", context)
            (folder / "tools.json").write_text('{"type":"function","name":"edit_circuit","parameters":{}}', encoding="utf-8")
            (folder / "SKILL.md").write_text("---\nname: circuit-editor\n---\nUse layout rule A", encoding="utf-8")
            self.assertIn("rule A", skills.make_payload("apply", "layout", "deepseek-flash", context)["instructions"])
            (folder / "SKILL.md").write_text("---\nname: circuit-editor\n---\nUse layout rule B", encoding="utf-8")
            self.assertIn("rule B", skills.make_payload("apply", "layout", "deepseek-flash", context)["instructions"])

    def test_unsupported_vision_model_keeps_structured_canvas_context(self):
        _, _, context = broker.parse_turn(ET.tostring(request(), encoding="utf-8"))
        body = skills.make_payload("apply", "layout", "deepseek-v4-pro", context)
        parts = body["input"][-1]["content"]
        self.assertEqual(len(parts), 1)
        self.assertFalse(json.loads(parts[0]["text"])["image_attached"])


class RuntimeNetworkTests(unittest.TestCase):
    def setUp(self):
        transport = mock.patch.object(broker, "USE_WINHTTP", False)
        transport.start(); self.addCleanup(transport.stop)
        patch = mock.patch.object(broker.os, "environ", {"DEEPSEEK_PROXY": "auto"})
        patch.start(); self.addCleanup(patch.stop)
        patch = mock.patch.object(broker, "_auto_proxy_mode", "system")
        patch.start(); self.addCleanup(patch.stop)

    def test_tls_probe_failure_switches_to_direct_before_one_post(self):
        with mock.patch.object(broker, "probe_network", side_effect=[(False, "TLS 握手失败"), (True, "HTTP 401")]) as probe, \
                mock.patch.object(broker.urllib.request, "build_opener") as opener:
            opener.return_value.open.return_value = io.BytesIO(b'{"status":"completed"}')
            self.assertEqual(broker.fetch_deepseek({"input": "test"}, KEY), {"status": "completed"})
            self.assertEqual(probe.call_args_list, [mock.call("system"), mock.call("direct")])
            opener.return_value.open.assert_called_once()
            self.assertEqual(broker._auto_proxy_mode, "direct")

    def test_failed_paths_send_no_model_post(self):
        with mock.patch.object(broker, "probe_network", return_value=(False, "TLS failed")), \
                mock.patch.object(broker.urllib.request, "build_opener") as opener:
            with self.assertRaises(broker.BrokerError): broker.fetch_deepseek({}, KEY)
            opener.assert_not_called()

    def test_ambiguous_post_failure_is_not_replayed(self):
        with mock.patch.object(broker, "probe_network", return_value=(True, "HTTP 401")), \
                mock.patch.object(broker.urllib.request, "build_opener") as opener:
            opener.return_value.open.side_effect = urllib.error.URLError(ssl.SSLError("TLS failure"))
            with self.assertRaises(broker.BrokerError): broker.fetch_deepseek({}, KEY)
            opener.return_value.open.assert_called_once()
            self.assertEqual(broker._auto_proxy_mode, "direct")


if __name__ == "__main__": unittest.main()
