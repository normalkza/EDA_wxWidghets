"""Canvas-aware skill loading, bounded context and edit-tool validation."""

import base64
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

SKILL_DIR = Path(__file__).resolve().parent / "skills" / "circuit-editor"
MAX_PATCH_BYTES = 256 * 1024
REFERENCE = re.compile(r"[A-Za-z_][A-Za-z0-9_-]{0,31}\Z")
BAD_XML = re.compile(r"[^\x09\x0a\x0d\x20-\ud7ff\ue000-\ufffd\U00010000-\U0010ffff]")


def integer(value, minimum=-1000000, maximum=1000000):
    if isinstance(value, str) and re.fullmatch(r"-?[0-9]{1,16}", value):
        value = int(value)
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("画布坐标或数字字段无效。")
    return value


def attrs(node):
    result = dict(node.attrib)
    for name in ("x", "y", "width", "height", "inputCount", "rotation", "pointSize",
                 "minInputs", "maxInputs", "grid", "clockPeriodNs", "clockHighNs", "clockStartNs"):
        if name in result:
            result[name] = integer(result[name], -1000000, 10**15 if name.startswith("clock") else 1000000)
    return result


def parse_context(node):
    if node.tag != "context" or set(node.attrib) != {"phase"} or node.get("phase") not in ("edit", "review"):
        raise ValueError("无效的画布上下文。")
    if [item.tag for item in node] != ["workspace", "image", "history", "feedback"]:
        raise ValueError("画布上下文不完整。")
    for current in node.iter():
        if len(current.attrib) > 20 or (current.tail or "").strip():
            raise ValueError("无效的画布字段。")
    workspace, image, history, feedback = node
    if [item.tag for item in workspace] != ["capabilities", "components", "wires", "texts"]:
        raise ValueError("画布快照不完整。")
    scene = attrs(workspace)
    integer(scene.get("width"), 1, 16384); integer(scene.get("height"), 1, 16384)
    caps, components, wires, texts = workspace
    if len(caps) > 64 or len(components) > 1000 or len(wires) > 4000 or len(texts) > 1000:
        raise ValueError("画布对象过多。")
    scene["capabilities"] = []
    for item in caps:
        if item.tag != "type" or len(item):
            raise ValueError("无效的元件能力列表。")
        scene["capabilities"].append(attrs(item))
    scene["components"] = []
    for item in components:
        if (item.tag != "component" or not re.fullmatch(r"c[1-9][0-9]{0,9}", item.get("ref", ""))
                or not len(item) or item[0].tag != "bounds" or len(item) > 33):
            raise ValueError("无效的元件快照。")
        part = attrs(item); part["bounds"] = attrs(item[0]); part["pins"] = []
        for pin in list(item)[1:]:
            if pin.tag != "pin" or len(pin) or pin.get("direction") not in ("input", "output"):
                raise ValueError("无效的引脚快照。")
            part["pins"].append(attrs(pin))
        scene["components"].append(part)
    scene["wires"] = []
    for item in wires:
        if item.tag != "wire" or len(item) > 512:
            raise ValueError("无效的连线快照。")
        wire = attrs(item); wire["points"] = []
        for point in item:
            if point.tag != "point" or set(point.attrib) != {"x", "y"} or len(point):
                raise ValueError("无效的连线路径。")
            wire["points"].append(attrs(point))
        scene["wires"].append(wire)
    scene["texts"] = []
    for item in texts:
        if item.tag != "text" or len(item) or len(item.text or "") > 65536:
            raise ValueError("无效的画布文本。")
        scene["texts"].append({**attrs(item), "text": item.text or ""})
    if image.attrib or len(image) or history.attrib or feedback.attrib or len(feedback):
        raise ValueError("无效的上下文附件。")
    encoded = image.text or ""
    if encoded:
        if len(encoded) > 700000:
            raise ValueError("画布图像超过大小限制。")
        try:
            png = base64.b64decode(encoded, validate=True)
            if (not png.startswith(b"\x89PNG\r\n\x1a\n") or len(png) < 24 or len(png) > 512 * 1024
                    or png[12:16] != b"IHDR" or not all(1 <= n <= 1400 for n in struct.unpack(">II", png[16:24]))):
                raise ValueError("无效的 PNG 画布图像。")
        except (ValueError, struct.error):
            raise ValueError("无效的 PNG 画布图像。") from None
    messages = []
    if len(history) > 12 or sum(len(item.text or "") for item in history) > 24000:
        raise ValueError("对话上下文过长。")
    for item in history:
        if (item.tag != "message" or set(item.attrib) != {"role"} or item.get("role") not in ("user", "assistant")
                or len(item) or not (item.text or "").strip() or len(item.text) > 6000):
            raise ValueError("无效的对话历史。")
        messages.append({"role": item.get("role"), "content": item.text})
    if len(feedback.text or "") > 16000:
        raise ValueError("执行反馈过长。")
    return {"scene": scene, "image": encoded, "history": messages,
            "feedback": feedback.text or "", "review": node.get("phase") == "review"}


def make_payload(mode, question, model, context):
    try:
        skill = (SKILL_DIR / "SKILL.md").read_text(encoding="utf-8-sig")
        tool = json.loads((SKILL_DIR / "tools.json").read_text(encoding="utf-8-sig"))
        if len(skill) > 16000 or tool.get("name") != "edit_circuit" or tool.get("type") != "function":
            raise ValueError
    except (OSError, ValueError):
        raise ValueError("电路编辑 skill 文件缺失或无效，请重新编译并保留程序旁的 skills 目录。") from None
    instructions = skill.split("---", 2)[-1].strip()
    instructions += "\n当前是指导模式，只返回文字。" if mode == "guide" else "\n当前是执行模式，可以调用 edit_circuit。"
    history = list(context["history"])
    if history and history[-1]["role"] == "user" and history[-1]["content"] == question:
        history.pop()
    image_supported = model == "deepseek-flash"
    current = {"goal": question, "phase": "review" if context["review"] else "edit",
               "feedback": context["feedback"], "workspace": context["scene"],
               "image_attached": bool(context["image"] and image_supported)}
    parts = [{"type": "input_text", "text": json.dumps(current, ensure_ascii=False, separators=(",", ":"))}]
    if current["image_attached"]:
        parts.append({"type": "input_image", "image_url": "data:image/png;base64," + context["image"], "detail": "high"})
    payload = {"model": model, "input": history + [{"role": "user", "content": parts}],
               "instructions": instructions, "reasoning": {"effort": "none"}, "stream": False,
               "max_output_tokens": 8192 if mode == "apply" else 2048}
    if mode == "apply":
        payload["tools"] = [tool]
    return payload


COMMON_COMPONENT = {"x", "y", "inputCount", "rotation", "high", "clockPeriodNs", "clockHighNs",
                    "clockStartNs", "probeLabel", "fallingEdge"}
TEXT = {"x", "y", "width", "height", "pointSize", "text"}
RULES = {
    "add_component": (COMMON_COMPONENT | {"type"}, {"type", "x", "y", "inputCount"}),
    "update_component": (COMMON_COMPONENT, set()),
    "delete_component": (set(), set()),
    "add_wire": ({"from", "outPin", "to", "inPin"}, {"from", "outPin", "to", "inPin"}),
    "delete_wire": (set(), set()), "route_wire": ({"points"}, {"points"}),
    "add_text": (TEXT, {"x", "y", "width", "height", "text"}),
    "update_text": (TEXT, set()), "delete_text": (set(), set()),
}


def clean(text, secret):
    return BAD_XML.sub("\ufffd", text.replace(secret, "[已隐藏]"))


def parse_patch(arguments, decode_json, secret):
    try:
        document = decode_json(arguments)
        if (not isinstance(document, dict) or set(document) != {"summary", "operations"}
                or not isinstance(document["summary"], str) or len(document["summary"]) > 2000
                or not isinstance(document["operations"], list) or not 1 <= len(document["operations"]) <= 128):
            raise ValueError
        reply = ET.Element("assistant_reply", {"kind": "patch"})
        ET.SubElement(reply, "summary").text = clean(document["summary"], secret)
        operations = ET.SubElement(reply, "operations")
        for operation in document["operations"]:
            if not isinstance(operation, dict) or operation.get("op") not in RULES:
                raise ValueError
            allowed, required = RULES[operation["op"]]
            fields = set(operation) - {"op", "ref"}
            if (set(operation) - allowed - {"op", "ref"} or not required <= fields
                    or not isinstance(operation.get("ref"), str) or not REFERENCE.fullmatch(operation["ref"])
                    or (operation["op"].startswith("update_") and not fields)):
                raise ValueError
            node = ET.SubElement(operations, "op", {"kind": operation["op"], "ref": operation["ref"]})
            for key in fields:
                value = operation[key]
                if key == "points":
                    if not isinstance(value, list) or len(value) > 64 or len(value) == 1:
                        raise ValueError
                    previous = None
                    for point in value:
                        if not isinstance(point, dict) or set(point) != {"x", "y"} or any(type(n) is not int for n in point.values()):
                            raise ValueError
                        x, y = integer(point["x"]), integer(point["y"])
                        if previous and (previous == (x, y) or (previous[0] != x and previous[1] != y)):
                            raise ValueError
                        previous = x, y
                        ET.SubElement(node, "point", {"x": str(x), "y": str(y)})
                elif key in ("high", "fallingEdge"):
                    if type(value) is not bool:
                        raise ValueError
                    ET.SubElement(node, "field", {"name": key}).text = "true" if value else "false"
                elif key in ("type", "from", "to", "outPin", "inPin", "text", "probeLabel"):
                    if not isinstance(value, str) or len(value) > (4096 if key == "text" else 128):
                        raise ValueError
                    if key not in ("text", "probeLabel") and not REFERENCE.fullmatch(value):
                        raise ValueError
                    ET.SubElement(node, "field", {"name": key}).text = clean(value, secret)
                else:
                    if type(value) is not int:
                        raise ValueError
                    integer(value, -1000000, 10**15 if key.startswith("clock") else 1000000)
                    ET.SubElement(node, "field", {"name": key}).text = str(value)
        if len(ET.tostring(reply, encoding="utf-8")) > MAX_PATCH_BYTES:
            raise ValueError
        return reply
    except (ValueError, TypeError, KeyError, RecursionError):
        raise ValueError("助手返回的画布操作无效，没有应用。请简化要求后重试。") from None


def interpret_response(document, mode, secret, decode_json):
    if (not isinstance(document, dict) or document.get("status") != "completed" or document.get("error") is not None
            or document.get("incomplete_details") is not None or not isinstance(document.get("output"), list)):
        raise ValueError("DeepSeek 没有完整返回结果，请重试。")
    messages, calls = [], []
    for item in document["output"]:
        if not isinstance(item, dict) or item.get("status") not in (None, "completed"):
            raise ValueError("DeepSeek 返回的响应无效。")
        if item.get("type") == "function_call":
            calls.append(item)
        elif item.get("type") == "message":
            if item.get("role", "assistant") != "assistant" or not isinstance(item.get("content"), list):
                raise ValueError("DeepSeek 返回的文字无效。")
            for part in item["content"]:
                if not isinstance(part, dict) or part.get("type") != "output_text" or not isinstance(part.get("text"), str):
                    raise ValueError("DeepSeek 返回的文字无效。")
                messages.append(part["text"])
        elif item.get("type") != "reasoning":
            raise ValueError("DeepSeek 返回了不支持的操作。")
    if calls:
        if (mode != "apply" or len(calls) != 1 or calls[0].get("name") != "edit_circuit"
                or not isinstance(calls[0].get("call_id"), str) or not calls[0]["call_id"].strip()
                or not isinstance(calls[0].get("arguments"), str)):
            raise ValueError("助手返回了无效的编辑工具调用，没有应用。")
        return parse_patch(calls[0]["arguments"], decode_json, secret)
    text = clean("\n".join(messages).strip(), secret)[:6000]
    if not text:
        raise ValueError("DeepSeek 没有返回内容。")
    reply = ET.Element("assistant_reply", {"kind": "text"}); ET.SubElement(reply, "text").text = text
    return reply
