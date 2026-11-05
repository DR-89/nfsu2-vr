#!/usr/bin/env python3
"""Check vehicle seat/cabin profiles against privately extracted stock geometry.

Run audit-cockpit-cars.py and audit-cockpit-driver-cards.py first. This does
not modify assets or runtime profiles. Outputs stay in the ignored build
directory; they contain measurements, not a visual acceptance of every car.
"""
import argparse
import json
from pathlib import Path
import re
import struct

import numpy as np

ROOT = Path(__file__).resolve().parent.parent


def read_meshes(directory):
    meta = json.loads((directory / "stock-meshes.json").read_text())
    data = (directory / "stock-meshes.bin").read_bytes()
    meshes, drivers = {}, {}
    offset = 0
    while offset < len(data):
        number, nv, ni, driver = struct.unpack_from("<4I", data, offset)
        offset += 16
        if number >= len(meta) or not nv or not ni or ni % 3:
            raise ValueError("Invalid stock mesh record")
        vertices = np.frombuffer(data, dtype="<f4", count=nv * 4,
                                 offset=offset).reshape(nv, 4)[:, :3].copy()
        offset += nv * 16
        indices = np.frombuffer(data, dtype="<u4", count=ni,
                                offset=offset).reshape(-1, 3).copy()
        offset += ni * 4
        if not np.isfinite(vertices).all() or indices.max() >= nv:
            raise ValueError("Invalid stock mesh geometry")
        meshes[number] = vertices, indices
        if driver:
            car = meta[number]["car"]
            if car in drivers:
                raise ValueError(f"Multiple driver cards: {car}")
            drivers[car] = (vertices.min(0) + vertices.max(0)) * .5
    if len(meshes) != len(meta):
        raise ValueError("Incomplete stock mesh fixture")
    return meta, meshes, drivers


def measurements(meta, meshes, drivers, inventory):
    result = {}
    for car, driver in drivers.items():
        candidates = []
        for number, record in enumerate(meta):
            if record["solid"] != car + "_BASE_A":
                continue
            points, indices = meshes[number]
            # Skyline's windshield and rear glass share a material batch.
            # Restrict that batch to triangles entirely ahead of the driver.
            if car == "SKYLINE" and record["submesh"] == 3:
                front = np.all(points[indices, 0] > driver[0] + .03, axis=1)
                points = points[np.unique(indices[front])]
            if not len(points):
                continue
            low, high = points.min(0), points.max(0)
            extent = high - low
            if (extent[1] > .8 and .20 < extent[2] < .8
                    and .15 < extent[0] < 1.5 and low[0] > driver[0] + .03
                    and low[2] > driver[2] - .3 and abs(low[1] + high[1]) < .1):
                candidates.append((extent[1], low, high, number))
        if not candidates:
            raise ValueError(f"No measured windshield for {car}")
        _, low, high, number = min(candidates, key=lambda item: item[0])
        body = inventory[car][car + "_KIT00_BODY_A"]
        result[car] = dict(driver=driver.tolist(), windshield_lo=low.tolist(),
                           windshield_hi=high.tolist(),
                           windshield_submesh=meta[number]["submesh"],
                           body_halfwidth=(body["high"][1] - body["low"][1]) * .5)
    return result


def profiles(measured):
    reference = measured["PEUGOT"]
    result = []
    for car, measured_car in sorted(measured.items()):
        driver = measured_car["driver"]
        side = 1 if driver[1] > 0 else -1
        # Preserve the accepted Peugeot eye-to-driver-card offset. Use the
        # source driver side, including the two right-hand-drive stock cars.
        seat = [-.1 + driver[0] - reference["driver"][0],
                side * (.35 + abs(driver[1]) - reference["driver"][1]),
                .95 + driver[2] - reference["driver"][2]]
        near = ((measured_car["body_halfwidth"] - .04 - abs(seat[1]))
                / (reference["body_halfwidth"] - .04 - .35))
        far = ((measured_car["body_halfwidth"] - .04 + abs(seat[1]))
               / (reference["body_halfwidth"] - .04 + .35))
        front = ((measured_car["windshield_hi"][0] - seat[0] - .33)
                 / (reference["windshield_hi"][0] + .1 - .33))
        result.append(dict(name=car, seat=seat, near=near, far=far, front=front,
                           # Front backrests end at z=.425; the rear cushion
                           # starts at .500 in the fitted Tripo asset.
                           side=side, rear=.46 if car in ("MIATA", "350Z") else 20,
                           measurement=measured_car))
    return result


def right_window(meta, meshes, row):
    """Lateral distance to the right glass at eye height and its inward lean."""
    car = row["name"]
    forward, left, height = row["seat"]
    forward += .11 if row["side"] < 0 else .038
    # Some front door solids are packed. Prefer the opposite door's matching
    # pane next, then the front edge of the right rear side glass at the B post.
    for part in ("_KIT00_DOOR_RIGHT_A", "_KIT00_DOOR_LEFT_A", "_BASE_A"):
        panes = []
        for i, record in enumerate(meta):
            if record["solid"] != car + part or record["flags"] not in (16768, 49536):
                continue
            v = meshes[i][0]
            lo, hi = v.min(0), v.max(0)
            if hi[0]-lo[0] < .28 or hi[2]-lo[2] < .18 or not lo[2] < height < hi[2]:
                continue
            if (lo[1] if "LEFT" in part else -hi[1]) < .40:
                continue
            sample = np.clip(forward, lo[0]+.10, hi[0]-.10)
            panes.append((abs(sample-forward), i, sample))
        if panes:
            _, index, sample = min(panes)
            v = meshes[index][0]
            # Fit the glass in vehicle space, evaluating it at the mount's
            # longitudinal position so slanted door windows sit at the right depth.
            basis = np.column_stack((np.ones(len(v)), v[:, 0]-sample, v[:, 2]-height))
            distance, _, lean = np.linalg.lstsq(basis, abs(v[:, 1]), rcond=None)[0]
            return [left+distance, -lean]
    raise ValueError(f"No measured side glass for {car}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path, help="stock-solids.json")
    parser.add_argument("meshes", type=Path, help="directory with stock-meshes.bin/json")
    parser.add_argument("output", type=Path, help="private directory inside build/")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Use a private output directory inside build/.")
    inventory = json.loads(args.inventory.read_text())
    meta, meshes, drivers = read_meshes(args.meshes)
    computed = profiles(measurements(meta, meshes, drivers, inventory))
    source = (ROOT / "android/runtime/quest_cockpit_vehicle.h").read_text()
    rows = re.findall(r'\{"([A-Z0-9]+)",\{([^}]+)\},([^}]+)\}', source)
    compiled = {name: [float(x.strip().removesuffix("f"))
                       for x in (seat + "," + rest).split(",")]
                for name, seat, rest in rows if name != "DEFAULT"}
    if set(compiled) != {row["name"] for row in computed}:
        raise ValueError("Runtime and measured vehicle inventories differ")
    for row in computed:
        row["right_window"] = right_window(meta, meshes, row)
        expected = [*row["seat"], row["near"], row["far"], row["front"],
                    row["side"], row["rear"], *row["right_window"]]
        if not np.allclose(compiled[row["name"]], expected, atol=.00000051, rtol=0):
            raise ValueError(f"Profile differs from measured fit: {row['name']}")
    output.mkdir(parents=True, exist_ok=True)
    (output / "profiles.json").write_text(json.dumps(computed, indent=2) + "\n")
    print(f"All {len(computed)} runtime profiles match measured stock geometry.")
    print("This checks the fit derivation, not the full view from a moving headset.")


if __name__ == "__main__":
    main()
