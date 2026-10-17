#!/usr/bin/env python3
"""Prepare private car-bound fixtures and the actual compiled body-hide hook.

Reads local Xbox assets without modifying them. This checks the retail body
exclusion for every stock vehicle present, not a complete visual playthrough.
Outputs contain game-derived data and belong in an ignored build directory.
"""
import argparse
import json
import math
import mmap
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent


def function(source, name):
    start = source.index("void " + name + "(void)\n{")
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    # No game-derived fixtures should accidentally become repository assets.
    if not out.is_relative_to(ROOT / "build"):
        parser.error("Use a private output directory inside build/.")
    models = {}
    for path in sorted((args.game / "NFSUNDER").glob("ZZDATA*.BIN")):
        with path.open("rb") as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as data:
            pos = 0
            while (header := data.find(b"\x11\x40\x13\x00", pos)) >= 0:
                pos = header + 4
                size = struct.unpack_from("<I", data, header + 4)[0]
                if not 0xB0 <= size <= 0x200 or header + 8 + size > len(data):
                    continue
                start = header + 0xAC
                end = data.find(b"\0", start, min(header + 8 + size, start + 80))
                if end < 0:
                    continue
                try:
                    name = data[start:end].decode("ascii")
                except UnicodeDecodeError:
                    continue
                if not name.endswith("_A") or "_" not in name:
                    continue
                car = name.split("_")[0]
                if not (name == car + "_BASE_A" or name.startswith(car + "_KIT00_")):
                    continue
                low = struct.unpack_from("<3f", data, header + 0x28)
                high = struct.unpack_from("<3f", data, header + 0x38)
                if not all(math.isfinite(x) and abs(x) < 20 for x in (*low, *high)):
                    continue
                if any(a > b for a, b in zip(low, high)):
                    continue
                models.setdefault(car, {})[name] = dict(low=low, high=high, archive=path.name, offset=header)
    models = {car: parts for car, parts in models.items()
              if car + "_KIT00_BODY_A" in parts and car + "_BASE_A" in parts}
    if not models or "LANCEREVO8" not in models or "PEUGOT" not in models:
        raise ValueError("Expected stock vehicle geometry was not found in this disc.")
    out.mkdir(parents=True, exist_ok=True)
    records = []
    for car, parts in sorted(models.items()):
        # Bounds of decoded stock solids, with the retail 10 cm margin. Some
        # separate/packed attachments are not decoded by this audit. The hook
        # must work for every bound, irrespective of a car's exact part set.
        low = [min(p["low"][i] for p in parts.values()) - .1 for i in range(3)]
        high = [max(p["high"][i] for p in parts.values()) + .1 for i in range(3)]
        records.append(dict(car=car, low=low, high=high, solids=len(parts),
                            old_seat_hidden=all(a <= x <= b for a, x, b in zip(low, (-.1, .35, 1.45), high))))
    with (out / "car-bounds.bin").open("wb") as file:
        file.write(struct.pack("<II", 0x4E434252, len(records)))
        for record in records:
            file.write(struct.pack("<32s6f", record["car"].encode(), *record["low"], *record["high"]))
    (out / "car-bounds.json").write_text(json.dumps(records, indent=2) + "\n")
    (out / "stock-solids.json").write_text(json.dumps(models, indent=2) + "\n")
    generated = next(p for p in (ROOT / "gen").glob("recomp_*.c")
                     if "void sub_0004CA40_gen(void)" in p.read_text())
    body = function(generated.read_text(), "sub_0004CA40_gen")
    wrapper = function((ROOT / "src/recomp_manual.c").read_text(), "sub_0004CA40")
    (out / "cockpit_body_generated.inc").write_text(body + "\n" + wrapper, newline="\n")
    print(f"Prepared {len(records)} stock vehicles / {sum(len(p) for p in models.values())} decoded solids.")
    print("Original fixed seat triggers inside-body hiding:", ", ".join(r["car"] for r in records if r["old_seat_hidden"]))
    print("Compile tests/quest_car_body_test.c with this output directory and gen/ on the include path.")


if __name__ == "__main__":
    main()
