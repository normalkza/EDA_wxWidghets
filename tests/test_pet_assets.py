import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1] / "wx_demo" / "res" / "pet"


class PetAssetTests(unittest.TestCase):
    def test_model_reference_closure_is_local_and_complete(self):
        model_root = (ROOT / "model").resolve()
        manifest = json.loads((model_root / "c_0120.model3.json").read_text(encoding="utf-8"))
        refs = manifest["FileReferences"]
        files = [refs["Moc"], refs["Physics"], *refs["Textures"]]
        if refs.get("DisplayInfo"):
            files.append(refs["DisplayInfo"])
        files.extend(entry["File"] for entry in refs["Expressions"])
        for motions in refs["Motions"].values():
            files.extend(entry["File"] for entry in motions)
        self.assertIn("Idle", refs["Motions"])
        self.assertGreater(len(refs["Textures"]), 0)
        self.assertGreater(len(refs["Expressions"]), 0)
        for relative in files:
            with self.subTest(resource=relative):
                resource = (model_root / relative).resolve()
                self.assertTrue(resource.is_relative_to(model_root))
                self.assertTrue(resource.is_file())
                self.assertGreater(resource.stat().st_size, 0)

    def test_distribution_has_engine_preview_and_matching_windows_loaders(self):
        self.assertGreater((ROOT / "vendor.js").stat().st_size, 100000)
        self.assertEqual((ROOT / "preview.png").read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
        for architecture in ("x64", "x86"):
            self.assertEqual((ROOT / "webview2" / architecture / "WebView2Loader.dll").read_bytes()[:2], b"MZ")
        self.assertFalse(list(ROOT.rglob("live2dcubismcore*.js")))

    def test_reused_client_and_react_runtime_are_packaged_locally(self):
        for filename in ("client.js", "react.js", "react-dom.js", "pet.js", "index.html"):
            with self.subTest(resource=filename):
                resource = ROOT / filename
                self.assertTrue(resource.is_file())
                self.assertGreater(resource.stat().st_size, 0)
                self.assertTrue(resource.read_text(encoding="utf-8").strip())
        catalog = json.loads((ROOT / "catalog.json").read_text(encoding="utf-8"))
        self.assertIsInstance(catalog, (dict, list))
        self.assertTrue(catalog)

    def test_redistribution_keeps_original_credits_and_license_texts(self):
        notice = (ROOT / "licenses" / "THIRD-PARTY-NOTICE.md").read_text(encoding="utf-8")
        for credit in ("上善无形", "ZipZipPipe", "氵六青", "CC BY-NC-SA 4.0"):
            self.assertIn(credit, notice)
        license_text = (ROOT / "licenses" / "WebView2-LICENSE.txt").read_text(encoding="utf-8")
        self.assertIn("Microsoft Corporation", license_text)
        self.assertIn("Redistribution", license_text)


if __name__ == "__main__":
    unittest.main()
