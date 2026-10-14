#!/usr/bin/env python3
"""Rank bounded Quest GPU pass captures. These diagnostic timings are not FPS."""
import argparse
import collections
import csv
import json
import pathlib


def analyze(path):
    frames = collections.defaultdict(float)
    chunks = set()
    groups = {}
    count = 0
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            frame, command = int(row["frame"]), int(row["command"])
            duration = float(row["gpu_ms"])
            draws, vertices, indices = (int(row[k]) for k in ("draws", "vertices", "indices"))
            if not 0 <= duration < 60000 or min(draws, vertices, indices) < 0:
                raise ValueError(f"Invalid timing or geometry count in row {count + 2}")
            band = "clear" if not draws else "1" if draws == 1 else "2-31" if draws < 32 else "32-255" if draws < 256 else "256+"
            identity = (int(row["kind"]), row["color_va"], row["depth_va"],
                        int(row["width"]), int(row["height"]), int(row["layers"]),
                        row["first_shader"], band)
            if identity not in groups:
                groups[identity] = {"passes": 0, "gpu_ms": 0.0, "max_pass_ms": 0.0,
                                    "draws": 0, "vertices": 0, "indices": 0,
                                    "blend_draws": 0, "alpha_draws": 0}
            group = groups[identity]
            group["passes"] += 1
            group["gpu_ms"] += duration
            group["max_pass_ms"] = max(group["max_pass_ms"], duration)
            for name, value in (("draws", draws), ("vertices", vertices), ("indices", indices),
                                ("blend_draws", int(row["blend_draws"])), ("alpha_draws", int(row["alpha_draws"]))):
                group[name] += value
            frames[frame] += duration
            chunks.add((frame, command))
            count += 1
    if not frames:
        raise ValueError("Capture contains no timed passes")
    output = []
    for identity, totals in groups.items():
        record = dict(zip(("kind", "color_va", "depth_va", "width", "height", "layers", "first_shader", "draw_band"), identity))
        record.update(totals)
        record["gpu_ms_per_captured_frame"] = totals["gpu_ms"] / len(frames)
        record["mean_pass_ms"] = totals["gpu_ms"] / totals["passes"]
        record["mean_draws_per_pass"] = totals["draws"] / totals["passes"]
        output.append(record)
    output.sort(key=lambda group: group["gpu_ms"], reverse=True)
    return {"frames": len(frames), "command_chunks": len(chunks), "passes": count,
            "timed_pass_ms_per_frame": sum(frames.values()) / len(frames),
            "limits": "Per-pass timestamps affect scheduling; not an FPS measurement. Check the device completion log for query/overflow errors and capture completeness.",
            "groups": output, "frame_pass_totals_ms": dict(sorted(frames.items()))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.capture), indent=2) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
