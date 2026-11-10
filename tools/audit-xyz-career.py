#!/usr/bin/env python3
"""Extract private career fixtures from owned Xbox/PC data without changing it.

Output must stay inside ignored build/. This is a format audit, not a save
converter. No game bytes are printed or added to source assets.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def chunks(data):
    offset = 8
    result = {}
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("Truncated child header")
        tag, size = struct.unpack_from("<II", data, offset)
        if size > len(data) - offset - 8 or tag in result:
            raise ValueError("Invalid child extent or duplicate chunk")
        result[tag] = data[offset + 8:offset + 8 + size]
        offset += 8 + size
    return result


def extract(path, output, prefix):
    found = []
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as archive:
        position = 0
        while (position := archive.find(b"\x10\x4a\x03\x80", position)) >= 0:
            start = position
            position += 4
            if start + 8 > len(archive):
                continue
            size = struct.unpack_from("<I", archive, start + 4)[0]
            if not 8 <= size <= 131064 or size > len(archive) - start - 8:
                continue
            data = archive[start:start + 8 + size]
            try:
                tables = chunks(data)
            except ValueError:
                continue
            races, stages = tables.get(0x34A11, b""), tables.get(0x34A18, b"")
            if len(races) != 224 * 136 or len(stages) != 6 * 80:
                continue
            filename = f"{prefix}-{len(found)}.bin"
            target = output / filename
            # Each run gets a new output directory; never replace old fixtures.
            with target.open("xb") as destination:
                destination.write(data)
            found.append({"fixture": filename, "offset": start, "bytes": len(data),
                          "races": 224, "hidden_races": sum(races[i + 127] != 0 for i in range(0, len(races), 136)),
                          "stage_caps": [list(stages[i + 48:i + 53]) for i in range(0, len(stages), 80)],
                          "races_sha256": hashlib.sha256(races).hexdigest(),
                          "stages_sha256": hashlib.sha256(stages).hexdigest()})
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbox_game", type=Path)
    parser.add_argument("--pc-game", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()) or output == (ROOT / "build").resolve():
        parser.error("Use a new private subdirectory inside build/.")
    if output.exists():
        parser.error("Output already exists. Choose a new directory to preserve earlier fixtures.")
    archives = sorted((args.xbox_game / "NFSUNDER").glob("ZZDATA*.BIN"))
    if not archives:
        parser.error("No Xbox ZZDATA archives found.")
    output.mkdir(parents=True)
    result = {"xbox": [], "pc": []}
    for path in archives:
        result["xbox"].extend(extract(path, output, "xbox-" + path.stem.lower()))
    if args.pc_game:
        result["pc"] = extract(args.pc_game / "GLOBAL" / "GLOBALB.BUN", output, "pc-globalb")
    if not result["xbox"] or (args.pc_game and not result["pc"]):
        raise ValueError("Required career tables were not found; no compatibility result.")
    all_records = result["xbox"] + result["pc"]
    result["race_tables_identical"] = len({r["races_sha256"] for r in all_records}) == 1
    result["stage_tables_identical"] = len({r["stages_sha256"] for r in all_records}) == 1
    (output / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Extracted {len(result['xbox'])} Xbox and {len(result['pc'])} PC career fixtures into {output}.")
    print(f"Identical race tables: {result['race_tables_identical']}; identical stage tables: {result['stage_tables_identical']}")


if __name__ == "__main__":
    main()
