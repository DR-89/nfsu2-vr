#!/usr/bin/env python3
"""Summarize actual game frame intervals captured by the Windows VR host."""
import argparse
import csv
import json
import math
from pathlib import Path


def analyze(path, refresh_hz=None):
    if refresh_hz is not None and (not math.isfinite(refresh_hz) or refresh_hz <= 0):
        raise ValueError('Refresh rate must be finite and positive')
    with path.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.DictReader(stream))
    times = [float(row['frame_ms']) for row in rows]
    if not times or any(not math.isfinite(ms) or ms <= 0 for ms in times):
        raise ValueError('Capture must contain finite, positive frame intervals')
    if [int(row['frame']) for row in rows] != list(range(1, len(rows) + 1)):
        raise ValueError('Capture frame sequence is incomplete')
    ordered = sorted(times)
    def percentile(p):
        return ordered[max(0, math.ceil(len(ordered) * p) - 1)]
    slow_count = max(1, math.ceil(len(times) * .01))
    result = {
        'source': str(path), 'frames': len(times), 'duration_s': sum(times) / 1000,
        'game_fps': len(times) * 1000 / sum(times),
        'p50_ms': percentile(.50), 'p95_ms': percentile(.95), 'p99_ms': percentile(.99),
        'worst_ms': max(times),
        'one_percent_low_fps': slow_count * 1000 / sum(ordered[-slow_count:]),
        'frames_within_72fps_budget_percent': sum(ms <= 1000 / 72 for ms in times) * 100 / len(times),
        'frames_within_120fps_budget_percent': sum(ms <= 1000 / 120 for ms in times) * 100 / len(times),
        'limits': 'Game frame intervals; excludes transport/decoder latency and synthetic frames. Require the matching benchmark complete log. Compare matched resolution, scene, view, refresh, and runtime settings.',
    }
    if refresh_hz is not None:
        period = 1000 / refresh_hz
        result['cadence'] = {
            'refresh_hz': refresh_hz,
            'period_ms': period,
            'intervals_over_1_5_periods': sum(ms > period * 1.5 for ms in times),
            'intervals_over_2_periods': sum(ms > period * 2 for ms in times),
            'limits': 'Long game-frame interval counts, not measured compositor deadline misses. Strict budget percentages remain above; normal paced-frame jitter can straddle the exact refresh period.',
        }
    phase_names = ('draw_ms', 'fence_ms', 'xr_begin_ms', 'xr_end_ms', 'statistics_ms')
    if all(name in rows[0] for name in phase_names):
        phase_rows = [{name: float(row[name]) for name in phase_names} for row in rows]
        if any(not math.isfinite(value) or value < 0 for row in phase_rows for value in row.values()):
            raise ValueError('Phase times must be finite and nonnegative')
        result['phase_mean_ms'] = {name: sum(row[name] for row in phase_rows) / len(times) for name in phase_names}
        result['slowest_frames'] = [
            {'frame': index + 1, 'frame_ms': times[index], **phase_rows[index],
             'other_ms': times[index] - sum(phase_rows[index].values())}
            for index in sorted(range(len(times)), key=lambda i: times[i], reverse=True)[:10]
        ]
        result['phase_limits'] = 'Renderer wall-clock phases at matching frame boundaries, not GPU execution times. Other time includes command decoding, guest/scheduling waits and uninstrumented work; XR begin includes normal frame pacing. No transport/decoder timing is measured.'
        work_names = ('program_compile_ms', 'texture_upload_ms', 'pipeline_create_ms', 'pipeline_cache_ms')
        if all(name in rows[0] for name in (*work_names, 'draws')):
            work_rows = [{name: float(row[name]) for name in work_names} for row in rows]
            draw_counts = [int(row['draws']) for row in rows]
            if any(not math.isfinite(v) or v < 0 for row in work_rows for v in row.values()) or any(n < 0 for n in draw_counts):
                raise ValueError('Work times and draw counts must be finite and nonnegative')
            for frame in result['slowest_frames']:
                index = frame['frame'] - 1
                frame.update(work_rows[index])
                frame['draws'] = draw_counts[index]
            result['work_limits'] = 'Nested work counters can overlap renderer phases; do not add them to phase totals. Zero means no measured shader/texture/pipeline work of that type in the interval.'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--refresh-hz', type=float)
    args = parser.parse_args()
    result = json.dumps(analyze(args.capture, args.refresh_hz), indent=2) + '\n'
    if args.output:
        args.output.write_text(result, encoding='utf-8')
    else:
        print(result, end='')


if __name__ == '__main__':
    main()
