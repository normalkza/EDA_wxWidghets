import json
import pathlib
import sys

roots = [pathlib.Path(p) for p in sys.argv[1:]]
for root in roots:
    print("==", root)
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in {".json", ".js", ".html", ".css", ".md"}:
            continue
        raw = path.read_bytes()
        try:
            text = raw.decode("utf-8")
            utf8 = True
        except UnicodeDecodeError as exc:
            print(f"  INVALID-UTF8 {path.relative_to(root)} {exc}")
            continue
        flags = []
        if "\ufffd" in text:
            flags.append("FFFD")
        if path.suffix.lower() == ".json":
            try:
                json.loads(text)
            except Exception as exc:
                flags.append("BADJSON:" + str(exc)[:80])
        # mojibake heuristic: characters typical of GBK-decoded UTF-8 Chinese
        if any(ch in text for ch in "鏃鐨勬槸涓嶅湪鏄") and "catalog" not in path.name:
            pass
        if flags:
            print(f"  {' '.join(flags)} {path.relative_to(root)} ({len(raw)} bytes)")
