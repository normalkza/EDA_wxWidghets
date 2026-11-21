"""Exercise the native UI/client and real local broker; mock only DeepSeek."""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import assistant_server as broker


def main():
    counts, images = {}, {}
    parse_turn = broker.parse_turn

    def parse_native(data):
        try:
            return parse_turn(data)
        except ValueError as error:
            root = broker.ET.fromstring(data)
            print("Invalid native context:", str(error), [item.tag for item in root],
                  [item.tag for item in root.find("context")] if root.find("context") is not None else [], file=sys.stderr)
            raise

    def upstream(payload, api_key):
        assert api_key == "sk-native-skill-placeholder"
        assert payload["tools"][0]["name"] == "edit_circuit"
        parts = payload["input"][-1]["content"]
        state = json.loads(parts[0]["text"])
        goal = state["goal"]
        turn = counts.get(goal, 0); counts[goal] = turn + 1
        scene = state["workspace"]
        assert scene["components"][0]["ref"] == "c2"
        assert scene["components"][0]["pins"][0]["name"] == "OUT"
        assert parts[1]["type"] == "input_image"
        assert parts[1]["image_url"].startswith("data:image/png;base64,iVBOR")
        if goal == "skill-layout":
            if turn == 0:
                assert scene["components"][0]["x"] == 80 and not scene["texts"]
                images[goal] = parts[1]["image_url"]
                operations = [{"op": "update_component", "ref": "c2", "x": 180},
                              {"op": "add_text", "ref": "n_label", "x": 180, "y": 50, "width": 80, "height": 30, "text": "A"}]
            else:
                assert turn == 1 and state["phase"] == "review" and "执行成功" in state["feedback"]
                assert scene["components"][0]["x"] == 180 and scene["texts"][0]["text"] == "A"
                assert parts[1]["image_url"] != images[goal]
                operations = None
        elif goal == "skill-repair":
            assert "skill-layout" in json.dumps(payload["input"][:-1])
            if turn == 0:
                assert scene["components"][0]["x"] == 180
                operations = [{"op": "update_component", "ref": "c2", "x": 660}]
            elif turn == 1:
                assert "重叠" in state["feedback"] and scene["components"][0]["x"] == 180
                operations = [{"op": "update_component", "ref": "c2", "x": 240}]
            else:
                assert turn == 2 and scene["components"][0]["x"] == 240
                operations = None
        elif goal in ("skill-stale", "skill-cancel"):
            assert turn == 0
            time.sleep(0.3)
            operations = [{"op": "update_component", "ref": "c2", "x": 360}]
        else:
            raise AssertionError("unexpected goal")
        if operations is not None:
            output = [{"type": "function_call", "name": "edit_circuit", "status": "completed", "call_id": "edit_call",
                       "arguments": json.dumps({"summary": "调整当前画布", "operations": operations})}]
        else:
            output = [{"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "已检查当前画布，调整完成。"}]}]
        return {"status": "completed", "error": None, "incomplete_details": None, "output": output}

    with patch.dict(os.environ, {"DEEPSEEK_MODEL": "deepseek-flash"}), patch.object(broker, "fetch_deepseek", side_effect=upstream), \
            patch.object(broker, "parse_turn", side_effect=parse_native):
        with broker.make_server(0, api_key="sk-native-skill-placeholder", session_token="native-skill-test-token") as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
            try:
                result = subprocess.run([sys.argv[1], str(server.server_port)], capture_output=True, timeout=30)
                print(result.stdout.decode("utf-8", errors="replace"), end="")
                print(result.stderr.decode("utf-8", errors="replace"), file=sys.stderr, end="")
                if result.returncode:
                    return result.returncode
                assert counts.get("skill-layout") == 2 and counts.get("skill-repair") == 3 and counts.get("skill-stale") == 1
                print("PASS: verified updated coordinates, PNG changes, conversation continuity and bounded feedback; no paid API requests")
            finally:
                server.shutdown(); worker.join(5)
    return 0


if __name__ == "__main__": sys.exit(main())
