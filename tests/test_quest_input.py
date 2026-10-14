"""Synthetic format fixtures only: no retail game bytes are stored here."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

MODULE = Path(__file__).resolve().parents[1] / "tools/inspect_game.py"
spec = importlib.util.spec_from_file_location("inspect_game", MODULE)
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def synthetic_xbe(title=inspector.TITLE_ID, region=1, entry=inspector.ENTRY):
    data = bytearray(0x600)
    data[:4] = b"XBEH"
    fields = {0x104: 0x10000, 0x118: 0x10200, 0x11C: 1, 0x120: 0x10400,
              0x128: entry ^ 0xA8FC57AB, 0x200: 176, 0x208: title, 0x2A0: region}
    for offset, value in fields.items():
        struct.pack_into("<I", data, offset, value)
    struct.pack_into("<5I", data, 0x400, 4, inspector.ENTRY, 0x100, 0x500, 0x100)
    return data


def seven_zip_start(offset, size, next_crc=0):
    data = bytearray(b"7z\xbc\xaf\x27\x1c\x00\x04" + bytes(24))
    struct.pack_into("<QQI", data, 12, offset, size, next_crc)
    struct.pack_into("<I", data, 8, zlib.crc32(data[12:32]))
    return data


class InputTests(unittest.TestCase):
    def test_truncated_download_exact_missing_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "download.7z"
            path.write_bytes(seven_zip_start(1000, 39))
            result = inspector.check_7z(path)
            self.assertFalse(result["ok"])
            self.assertEqual(result["required_bytes"], 1071)
            self.assertEqual(result["missing_bytes"], 1039)

    def test_bad_start_crc_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "broken.7z"
            header = seven_zip_start(1000, 39)
            header[12] ^= 1
            path.write_bytes(header)
            with self.assertRaisesRegex(inspector.InputError, "start header"):
                inspector.check_7z(path)

    def test_bad_next_header_crc_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "broken.7z"
            path.write_bytes(seven_zip_start(0, 3) + b"bad")
            with self.assertRaisesRegex(inspector.InputError, "next header"):
                inspector.check_7z(path)

    def test_valid_container_is_not_claimed_as_valid_game(self):
        import py7zr
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "sample.txt"
            source.write_text("test data", encoding="utf-8")
            path = Path(tmp) / "valid.7z"
            with py7zr.SevenZipFile(path, "w") as archive:
                archive.write(source, "sample.txt")
            result = inspector.check_7z(path)
            self.assertTrue(result["ok"])
            self.assertEqual(result["game_compatibility"], "not checked until extracted")

    def test_supported_layout(self):
        result = inspector.check_xbe(synthetic_xbe())
        self.assertTrue(result["ok"])
        self.assertIn("exact revision unverified", result["validation"])

    def test_other_platform(self):
        with self.assertRaisesRegex(inspector.InputError, "original Xbox"):
            inspector.check_xbe(b"PS2" + bytes(4096))

    def test_wrong_title(self):
        with self.assertRaisesRegex(inspector.InputError, "Wrong Xbox title"):
            inspector.check_xbe(synthetic_xbe(title=123))

    def test_pal_executable(self):
        with self.assertRaisesRegex(inspector.InputError, "NTSC-U"):
            inspector.check_xbe(synthetic_xbe(region=4))

    def test_european_title_reports_game_and_incompatible_layout(self):
        with self.assertRaisesRegex(inspector.InputError, "European Xbox NFS") as caught:
            inspector.check_xbe(synthetic_xbe(title=inspector.EUROPE_TITLE_ID, region=4, entry=0x21B77E))
        self.assertEqual(caught.exception.details["title_id"], "0x4541006F")
        self.assertEqual(caught.exception.details["entry"], "0x0021B77E")
        self.assertEqual(caught.exception.details["expected_entry"], "0x0021B1CE")

    def test_changed_entry(self):
        with self.assertRaisesRegex(inspector.InputError, "Unsupported XBE layout"):
            inspector.check_xbe(synthetic_xbe(entry=0x123456))

    def test_truncated_section(self):
        with self.assertRaisesRegex(inspector.InputError, "past end"):
            inspector.check_xbe(synthetic_xbe()[:-1])

    def test_bad_certificate(self):
        data = synthetic_xbe()
        struct.pack_into("<I", data, 0x118, 0)
        with self.assertRaisesRegex(inspector.InputError, "certificate"):
            inspector.check_xbe(data)

    def test_missing_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "default.xbe").write_bytes(synthetic_xbe())
            with self.assertRaisesRegex(inspector.InputError, "NFSUNDER"):
                inspector.inspect(root)


if __name__ == "__main__":
    unittest.main()
