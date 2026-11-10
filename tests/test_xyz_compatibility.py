"""Boundary checks for the read-only porting preflight; no retail data."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("xyz_check", ROOT / "tools/check-xyz-compatibility.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class CompatibilityTests(unittest.TestCase):
    def test_size_and_digest_both_required(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "SPEED2.EXE"
            original = b"synthetic input, not a game"
            file.write_bytes(original)
            metadata = {"sha256": hashlib.sha256(original).hexdigest(), "bytes": len(original)}
            self.assertTrue(CHECK.file_check(file, metadata)["ok"])
            self.assertFalse(CHECK.file_check(file, {**metadata, "bytes": len(original) + 1})["ok"])
            file.write_bytes(b"X" + original[1:])
            self.assertFalse(CHECK.file_check(file, metadata)["ok"])

    def test_missing_input_is_a_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            result = CHECK.file_check(Path(directory) / "absent", {"sha256": "0" * 64})
            self.assertFalse(result["ok"])
            self.assertEqual(result["status"], "unreadable")

    def test_source_drift_and_windows_line_endings(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "protocol.h"
            source.write_bytes(b"one\r\ntwo\r\n")
            catalog = {"upstream": {"files": {"protocol.h": hashlib.sha256(b"one\ntwo\n").hexdigest()}}}
            self.assertTrue(CHECK.check_upstream(root, catalog)["ok"])
            source.write_bytes(b"one\nchanged\n")
            self.assertFalse(CHECK.check_upstream(root, catalog)["ok"])
            source.unlink()
            self.assertFalse(CHECK.check_upstream(root, catalog)["ok"])

    def test_preflight_never_implies_playable_or_importable(self):
        catalog = json.loads(CHECK.CATALOG.read_text(encoding="utf-8"))
        result = CHECK.report(catalog)
        self.assertIsNone(result["reference_inputs_match"])
        self.assertFalse(result["multiplayer_ready"])
        self.assertFalse(result["profile_import_ready"])
        self.assertFalse(result["preflight_writes_runtime"])
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "SPEED2.EXE"
            file.write_bytes(b"fixture")
            catalog["pc_executable"] = {"sha256": hashlib.sha256(b"fixture").hexdigest(), "bytes": 7}
            result = CHECK.report(catalog, Path(directory))
            self.assertTrue(result["reference_inputs_match"])
            self.assertFalse(result["multiplayer_ready"])
            self.assertEqual(file.read_bytes(), b"fixture")

    def test_catalogue_includes_only_active_mods(self):
        catalog = json.loads(CHECK.CATALOG.read_text(encoding="utf-8"))
        ids = [mod["id"] for mod in catalog["mods"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertNotIn("mod-rims", ids)  # disabled in the audited upstream snapshot
        self.assertEqual(len(ids), 16)
        self.assertEqual(set(catalog["targets"]), {"pcvr", "quest3"})
        self.assertEqual({m["id"] for m in catalog["mods"] if m["implemented_by_this_change"]}, {
            "hidden-races", "visual-rep", "uhud", "detailed-map", "subtitles", "songs",
            "gps-distance", "gearbox-whine", "rubberbanding", "hq-vinyls", "stance",
            "extra-options", "crash-damage"})
        for mod in catalog["mods"]:
            self.assertIn(mod["vr_disposition"], {"port_candidate", "native_equivalent", "not_applicable", "adapt_for_vr"})
            if mod["implemented_by_this_change"]:
                self.assertTrue(mod["validation"])
                self.assertTrue(mod["native_files"])
                for name in mod["native_files"]:
                    self.assertTrue((ROOT / name).is_file(), name)
        extra=next(m for m in catalog["mods"] if m["id"] == "extra-options")
        self.assertTrue(all(p["status"] in {"implemented","native_equivalent"}
                            for p in extra["parts"] if "status" in p))


if __name__ == "__main__":
    unittest.main()
