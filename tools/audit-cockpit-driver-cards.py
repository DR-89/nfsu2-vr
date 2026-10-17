#!/usr/bin/env python3
"""Extract private stock mesh fixtures to test driver-card removal across cars.

Run audit-cockpit-cars.py first. This reads the local disc's decoded stock
solids, including roofs, bodies and mirror geometry as negative cases. No
game assets are modified or written outside the ignored build directory.
"""
import argparse
from contextlib import ExitStack
import json
import math
import mmap
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent

# Inspected BASE_A submesh containing the central mirror, and triangle-vertex
# count. This is a private-fixture regression oracle, not runtime car logic.
# It retains the previously working selection on 23 cars and includes the six
# lower mirrors (240SX, Eclipse, Lancer, Miata, RX7 and TT) missed by the old
# height cutoff. These are metadata only; the geometry stays in build/.
CENTER_MIRRORS = {
    "240SX": (1, 6), "3000GT": (0, 6), "350Z": (3, 12), "A3": (0, 6),
    "CELICA": (2, 6), "CIVIC": (2, 6), "COROLLA": (2, 36), "ECLIPSE": (2, 12),
    "ESCALADE": (1, 6), "FOCUS": (2, 42), "G35": (2, 9), "GOLF": (3, 6),
    "GTO": (1, 24), "HUMMER": (3, 9), "IMPREZAWRX": (1, 6), "IS300": (3, 6),
    "LANCEREVO8": (4, 6), "MIATA": (2, 6), "MUSTANGGT": (3, 12),
    "NAVIGATOR": (1, 6), "PEUGOT": (2, 6), "RSX": (1, 12), "RX7": (2, 6),
    "RX8": (4, 18), "SENTRA": (4, 30), "SKYLINE": (2, 12), "SUPRA": (4, 6),
    "TIBURON": (1, 6), "TT": (3, 6),
}


def chunks(data, start, end):
    while start + 8 <= end:
        tag, size = struct.unpack_from("<II", data, start)
        if size > end - start - 8:
            raise ValueError(f"Invalid chunk at {start:x}")
        yield tag, (start + 8, size)
        start += size + 8
    if start != end:
        raise ValueError("Incomplete chunk list")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Use a private output directory inside build/.")
    inventory = json.loads(args.inventory.read_text())
    output.mkdir(parents=True, exist_ok=True)
    records = []
    with ExitStack() as stack:
        files = {}
        for name in {p["archive"] for car in inventory.values() for p in car.values()}:
            f = stack.enter_context((args.game / "NFSUNDER" / name).open("rb"))
            files[name] = stack.enter_context(mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ))
        fixture = stack.enter_context((output / "stock-meshes.bin").open("wb"))
        mirrors = stack.enter_context((output / "stock-center-mirrors.bin").open("wb"))
        for car, parts in sorted(inventory.items()):
            drivers = 0
            for name, part in sorted(parts.items()):
                data = files[part["archive"]]
                h = part["offset"]
                solid = dict(chunks(data, h, h + struct.unpack_from("<I", data, h - 4)[0]))
                a, size = solid[0x80134100]
                mesh = dict(chunks(data, a, a + size))
                va, vs = mesh[0x134B01]
                vertex_end = va + vs
                va = (va + 127) & ~127
                ia, ins = mesh[0x134B03]
                index_end = ia + ins
                ia = (ia + 15) & ~15
                ma, ms = mesh[0x134B02]
                padding = (-ma) % 16
                ma += padding
                ms -= padding
                if ms % 64:
                    raise ValueError(f"Unexpected submesh format: {name}")
                for number in range(ms // 64):
                    header = ma + number * 64
                    low = struct.unpack_from("<3f", data, header)
                    high = struct.unpack_from("<3f", data, header + 12)
                    count, texture, flags, first = struct.unpack_from("<4I", data, header + 24)
                    if count % 3 or ia + (first + count) * 2 > index_end:
                        raise ValueError(f"Invalid triangle indices: {name}/{number}")
                    original = struct.unpack_from(f"<{count}H", data, ia + first * 2)
                    used = sorted(set(original))
                    if not used or va + (used[-1] + 1) * 32 > vertex_end:
                        raise ValueError(f"Invalid vertices: {name}/{number}")
                    vertices = [struct.unpack_from("<3f", data, va + index * 32) for index in used]
                    # Independent submesh bounds catch a wrong vertex stride,
                    # index base or alignment instead of testing garbage data.
                    if any(not math.isfinite(x) or x < low[k] - .011 or x > high[k] + .011
                           for vertex in vertices for k, x in enumerate(vertex)):
                        raise ValueError(f"Vertex outside recorded bounds: {name}/{number}")
                    mapping = {old: new for new, old in enumerate(used)}
                    indices = [mapping[index] for index in original]
                    # Inspected stock BASE solids each contain exactly one
                    # 8-vertex/4-triangle driver impostor. Labels do not call
                    # the runtime classifier under test.
                    expected = int(name == car + "_BASE_A" and len(used) == 8 and count == 12)
                    drivers += expected
                    mirror_part, mirror_count = CENTER_MIRRORS[car]
                    expected_mirror = mirror_count if name == car + "_BASE_A" and number == mirror_part else 0
                    for target, label in ((fixture, expected), (mirrors, expected_mirror)):
                        target.write(struct.pack("<4I", len(records), len(used), count, label))
                        for vertex in vertices:
                            target.write(struct.pack("<4f", *vertex, 1.0))
                        target.write(struct.pack(f"<{count}I", *indices))
                    records.append(dict(car=car, solid=name, submesh=number, vertices=len(used),
                                        indices=count, expected_remove=expected,
                                        expected_center_mirror=expected_mirror, flags=flags,
                                        low=low, high=high))
            if drivers != 1:
                raise ValueError(f"Expected one independently labelled driver in {car}, got {drivers}")
    (output / "stock-meshes.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"Prepared {len(records)} submeshes across {len(inventory)} vehicles; "
          f"{sum(r['expected_remove'] for r in records)} driver cards, remainder must be retained.")


if __name__ == "__main__":
    main()
