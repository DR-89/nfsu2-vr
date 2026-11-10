#!/usr/bin/env python3
"""Convert locally exported HQV1 AVIF artwork to portable, lossless native packs.

Requires Pillow with AVIF support. Artwork is optional and stays out of source
control. NHQ1 stores sorted hashes, dimensions, channel counts, raw DEFLATE and
an Adler checksum. Pixel values match the browser's AVIF decoder inputs.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def convert(job):
    from PIL import Image
    source, destination = map(Path, job)
    blob = source.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    stamp = destination.with_suffix('.json')
    if destination.is_file() and stamp.is_file():
        old = json.loads(stamp.read_text())
        if old.get('source_sha256') == digest:
            with destination.open('rb') as f:
                if hashlib.file_digest(f, 'sha256').hexdigest() == old.get('sha256'):
                    return old
    magic, version, count, start = struct.unpack_from('<4sIII', blob)
    if magic != b'HQV1' or version != 1 or not 1 <= count <= 4096 or start != 16 + count * 16:
        raise ValueError(f'Invalid HQV1 header: {source}')
    destination.parent.mkdir(parents=True, exist_ok=True)
    table = []
    previous = -1
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as out:
        temporary = Path(out.name)
        out.write(struct.pack('<4sIII', b'NHQ1', 1, count, 16 + count * 24))
        out.write(bytes(count * 24))
        try:
            for i in range(count):
                key, offset, size, side, channels, reserved = struct.unpack_from('<IIIHBB', blob, 16 + i * 16)
                if key <= previous or side != 1024 or channels not in (1, 3) or reserved or offset < start or not size or offset + size > len(blob):
                    raise ValueError(f'Invalid HQV1 entry {source}:{i}')
                previous = key
                with Image.open(io.BytesIO(blob[offset:offset + size])) as image:
                    if image.size != (side, side):
                        raise ValueError('AVIF dimensions do not match index')
                    raw = image.convert('L' if channels == 1 else 'RGB').tobytes()
                compressor = zlib.compressobj(6, zlib.DEFLATED, -15)
                encoded = compressor.compress(raw) + compressor.flush()
                table.append((key, out.tell(), len(encoded), side, channels, 0, zlib.adler32(raw), len(raw)))
                out.write(encoded)
            out.seek(16)
            for row in table:
                out.write(struct.pack('<IIIHBBII', *row))
        except BaseException:
            out.close()
            temporary.unlink(missing_ok=True)
            raise
    try:
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    with destination.open('rb') as f:
        output_digest = hashlib.file_digest(f, 'sha256').hexdigest()
    result = dict(model=source.stem, entries=count, bytes=destination.stat().st_size,
                  source_sha256=digest, sha256=output_digest, format='NHQ1',
                  author='AdrienShadow', source='https://nfsmods.xyz/mod/8064')
    stamp.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/xyz-lan-qa/live-mod-assets/HQVINYLS/CARS')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/optional-mod-assets/HQVINYLS')
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--model', action='append')
    args = parser.parse_args()
    files = sorted(args.source.glob('*.hqv'))
    if args.model:
        files = [p for p in files if p.stem in args.model]
    if not files:
        raise SystemExit('No locally exported HQV1 packs found')
    jobs = [(str(p), str(args.output / (p.stem + '.nhq'))) for p in files]
    with ProcessPoolExecutor(max_workers=max(1, min(args.workers, 4))) as pool:
        for result in pool.map(convert, jobs):
            print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
