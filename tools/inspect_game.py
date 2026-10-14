#!/usr/bin/env python3
"""Read-only preflight for the private Xbox input; never modifies game files."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
TITLE_ID = 0x4541005A
EUROPE_TITLE_ID = 0x4541006F
ENTRY = 0x0021B1CE  # src/main.c and the fixed-address manual overrides


class InputError(ValueError):
    def __init__(self, message, details=None):
        super().__init__(message)
        self.details = details or {}


def check_7z(path):
    size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(32)
        if len(header) != 32 or header[:6] != b"7z\xbc\xaf\x27\x1c":
            raise InputError("Not a 7z archive (missing signature/start header).")
        if zlib.crc32(header[12:32]) != struct.unpack_from("<I", header, 8)[0]:
            raise InputError("Damaged 7z start header: CRC does not match.")
        offset, length, crc = struct.unpack_from("<QQI", header, 12)
        required = 32 + offset + length
        result = {"kind": "7z", "actual_bytes": size,
                  "required_bytes": required, "missing_bytes": max(0, required - size)}
        if size < required:
            return {**result, "ok": False, "error":
                    f"Incomplete archive: {size:,} of at least {required:,} bytes; "
                    f"{required - size:,} bytes missing."}
        stream.seek(32 + offset)
        actual_crc = 0
        remaining = length
        while remaining:
            chunk = stream.read(min(1024 * 1024, remaining))
            if not chunk:
                raise InputError("Archive changed during inspection; retry when download finishes.")
            actual_crc = zlib.crc32(chunk, actual_crc)
            remaining -= len(chunk)
        if actual_crc != crc:
            raise InputError("Damaged 7z next header: CRC does not match.")
    import py7zr
    with py7zr.SevenZipFile(path) as archive:
        if archive.needs_password():
            raise InputError("Encrypted archive; provide an unpacked Xbox disc instead.")
        members = [{"name": item.filename, "bytes": item.uncompressed}
                   for item in archive.list()]
    # A valid container is not proof of a complete disc or correct game version.
    return {**result, "ok": True, "validation": "container headers only",
            "members": members, "game_compatibility": "not checked until extracted"}


def check_xbe(data):
    if len(data) < 0x178 or data[:4] != b"XBEH":
        raise InputError("Not an original Xbox executable (XBEH signature missing).")

    def u32(offset):
        if not 0 <= offset <= len(data) - 4:
            raise InputError("Truncated XBE header/certificate.")
        return struct.unpack_from("<I", data, offset)[0]

    base = u32(0x104)
    cert = u32(0x118) - base
    if cert < 0 or cert + 176 > len(data):
        raise InputError("XBE certificate lies outside the file.")
    cert_size = u32(cert)
    if cert_size < 176 or cert + cert_size > len(data):
        raise InputError("Invalid or truncated XBE certificate.")
    title = u32(cert + 8)
    region = u32(cert + 160)
    entry = u32(0x128) ^ 0xA8FC57AB  # Xbox retail entry-point encoding
    sections = u32(0x11C)
    table = u32(0x120) - base
    if not 0 < sections <= 256 or table < 0 or table + sections * 56 > len(data):
        raise InputError("Invalid or truncated XBE section table.")
    entry_present = False
    for index in range(sections):
        offset = table + index * 56
        flags, address, virtual_size, raw, length = struct.unpack_from("<5I", data, offset)
        if raw + length > len(data):
            raise InputError(f"XBE section {index} extends past end of file.")
        if flags & 4 and address <= entry < address + min(virtual_size, length):
            entry_present = True
    details = {"kind": "xbe", "title_id": f"0x{title:08X}",
               "entry": f"0x{entry:08X}", "region_mask": region,
               "sha256": hashlib.sha256(data).hexdigest(),
               "title_name": data[cert + 12:cert + 92].decode("utf-16-le", errors="replace").rstrip("\0"),
               "expected_title_id": f"0x{TITLE_ID:08X}",
               "expected_entry": f"0x{ENTRY:08X}"}
    if title == EUROPE_TITLE_ID:
        raise InputError(
            "European Xbox NFS Underground 2 detected. This is the correct game, "
            "but its executable layout is not supported by the NTSC-U manual "
            f"overrides/seeds (entry 0x{entry:08X}, expected 0x{ENTRY:08X}). "
            "A verified regional address map is required before code generation.",
            details)
    if title != TITLE_ID:
        raise InputError(f"Wrong Xbox title: 0x{title:08X}; expected NFSU2 0x{TITLE_ID:08X}.", details)
    if not region & 1:
        raise InputError("This port requires the NTSC-U Xbox executable (North America region).", details)
    if entry != ENTRY or not entry_present:
        raise InputError(f"Unsupported XBE layout: entry 0x{entry:08X}, expected 0x{ENTRY:08X}.", details)
    return {**details, "ok": True,
            "validation": "title, region, entry and section bounds; exact revision unverified"}


def inspect(path):
    path = Path(path).resolve(strict=True)
    if path.is_dir():
        entries = {child.name.lower(): child for child in path.iterdir()}
        xbe = entries.get("default.xbe")
        assets = entries.get("nfsunder")
        if not xbe or not xbe.is_file():
            raise InputError("Extracted disc root must contain default.xbe.")
        if not assets or not assets.is_dir() or not any(assets.iterdir()):
            raise InputError("Extracted disc root must contain a nonempty NFSUNDER directory.")
        return {**check_xbe(xbe.read_bytes()), "kind": "extracted_disc",
                "game_dir": str(path), "assets": "NFSUNDER present; content not verified"}
    if path.suffix.lower() == ".7z":
        return check_7z(path)
    if path.suffix.lower() == ".xbe":
        return check_xbe(path.read_bytes())
    if path.suffix.lower() in (".iso", ".xiso"):
        # Use the same XDVDFS reader as the upstream recompilation toolkit.
        sys.path.insert(0, str(ROOT / "xboxrecomp"))
        from tools.xiso.xdvdfs import Xiso, XisoError
        try:
            with Xiso(path) as image:
                entry = image.find("default.xbe")
                if entry is None or entry.is_dir:
                    raise InputError("Xbox image has no default.xbe in its root.")
                if entry.size > 64 * 1024 * 1024:
                    raise InputError("Unexpectedly large XBE; inspect the image manually.")
                result = check_xbe(image.read(entry))
                return {**result, "kind": "xbox_disc_image",
                        "validation": result["validation"] + "; disc assets not verified"}
        except XisoError as error:
            raise InputError(str(error)) from error
    raise InputError("Expected a .7z archive, Xbox .iso/.xiso, default.xbe, or extracted disc directory.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        result = inspect(args.input)
    except InputError as error:
        result = {**error.details, "ok": False, "error": str(error)}
    except Exception as error:
        result = {"ok": False, "error": str(error)}
    result["source"] = str(args.input.resolve())
    output = json.dumps(result, indent=2, ensure_ascii=False)
    if args.report:
        report = args.report.resolve()
        source = args.input.resolve()
        if report == source or (source.is_dir() and report.is_relative_to(source)):
            parser.error("Report must be outside the private input.")
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0 if result["ok"] else 2


if __name__ == "__main__":
    sys.exit(main())
