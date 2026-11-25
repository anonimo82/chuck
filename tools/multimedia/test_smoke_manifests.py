"""Fast infrastructure tests: no ChucK or external dependencies."""
import json
import pathlib
import tempfile
import unittest
import run_smoke_tests as runner

class ManifestTests(unittest.TestCase):
    def test_repository_manifests(self):
        global_groups, local_groups = runner.manifests()
        self.assertEqual(set(global_groups), set(runner.GROUPS))
        self.assertTrue(set(local_groups).issubset(global_groups))

    def test_selection(self):
        all_groups = list(runner.GROUPS)
        local = ["text-recent"]
        self.assertEqual(runner.select_groups("Full", all_groups, local, None, None), all_groups)
        for mode in ("Local", "Incremental"):
            self.assertEqual(runner.select_groups(mode, all_groups, local, None, None), local)
            self.assertEqual(runner.select_groups(mode, all_groups, [], None, None), [])
        self.assertEqual(runner.select_groups("Failed", all_groups, local, None, ["raster"]), ["raster", "text-recent"])
        self.assertEqual(runner.select_groups("Full", all_groups, local, ["audio"], None), ["audio"])
        with self.assertRaises(ValueError):
            runner.select_groups("Failed", all_groups, local, None, [])

    def test_invalid_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "manifest.json"
            for data in ({"version": 2, "groups": []},
                         {"version": 1, "groups": ["unknown"]},
                         {"version": 1, "groups": ["audio", "audio"]},
                         {"version": 1, "groups": [1]},
                         {"version": 1, "groups": "audio"}):
                path.write_text(json.dumps(data), encoding="utf-8")
                with self.assertRaises(ValueError):
                    runner.load_manifest(path)
            path.write_text(json.dumps({"version": 1, "groups": ["audio"]}), encoding="utf-8")
            self.assertEqual(runner.load_manifest(path), ["audio"])

if __name__ == "__main__":
    unittest.main()
