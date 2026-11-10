#!/usr/bin/env python3
"""Convert the locally exported restored soundtrack to Xbox EA-XA v2 PCM frames.

The original EA player, playlist and volume controls remain responsible for
playback. The conversion preserves decoded signed 16-bit samples exactly and
does not write to the owned game archives. Requires vgmstream-cli and FFmpeg.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
MAP_SHA = '67b5f62e79f409e5619b0e25fab5ce0ace5100c2c0d1626815109375eeaeed19'
MUSIC_SHA = '42ed0263eac2c388fb6aa023e22cf970c1636bf810ecebb0e211693442849f05'


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def tag(number, value):
    data = value.to_bytes(max(1, (value.bit_length() + 7) // 8), 'big')
    return bytes((number, len(data))) + data


def chunk(kind, data):
    return kind + struct.pack('<I', len(data) + 8) + data


def encode_pcm(out, pcm, rate):
    import numpy as np
    samples = np.frombuffer(pcm, dtype='<i2').reshape(-1, 2)
    total = len(samples)
    # Retail Xbox GSTR stream header; its default codec is EA-XA v2 with
    # big-endian block counts and channel offsets. 0xEE frames carry PCM.
    header = b'GSTR\x01\0\0\0' + tag(6, 101) + b'\xfd' + tag(0x85, total) + tag(0x82, 2) + tag(0x84, rate) + b'\xff'
    header += bytes(-len(header) % 4)
    out.write(chunk(b'SCHl', header))
    block_samples = 56 * 28
    out.write(chunk(b'SCCl', struct.pack('>I', (total + block_samples - 1) // block_samples)))
    for at in range(0, total, block_samples):
        segment = samples[at:at + block_samples]
        frames = (len(segment) + 27) // 28
        padded = np.zeros((frames * 28, 2), dtype='>i2')
        padded[:len(segment)] = segment
        channels = []
        for ch in range(2):
            data = np.zeros((frames, 61), dtype='u1')
            data[:, 0] = 0xee
            data[:, 5:] = np.frombuffer(padded[:, ch].tobytes(), dtype='u1').reshape(frames, 56)
            channels.append(data.tobytes())
        payload = struct.pack('>III', len(segment), 0, len(channels[0])) + b''.join(channels)
        payload += bytes(-len(payload) % 4)
        out.write(chunk(b'SCDl', payload))
    out.write(chunk(b'SCEl', b''))
    return total


def decode(decoder, ffmpeg, source, stream):
    process = subprocess.Popen([str(decoder), '-s', str(stream), '-p', str(source)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        result = subprocess.run([str(ffmpeg), '-v', 'error', '-i', 'pipe:0', '-f', 's16le', '-ac', '2', 'pipe:1'],
                                stdin=process.stdout, capture_output=True)
    finally:
        process.stdout.close()
    code = process.wait()
    if code or result.returncode or not result.stdout or len(result.stdout) % 4:
        raise ValueError(f'Could not decode stream {stream}: {result.stderr.decode(errors="replace")}')
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build/xyz-lan-qa/live-mod-assets/pfdata')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/optional-mod-assets/SONGS')
    parser.add_argument('--decoder', type=Path, default=ROOT / 'build/deps/vgmstream-r2168/vgmstream-cli.exe')
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    source_map, source_music = args.source / 'MusicSFx.mpf', args.source / 'MusicSFx.mus'
    if digest(source_map) != MAP_SHA or digest(source_music) != MUSIC_SHA:
        raise SystemExit('Unexpected source hashes; no destination changes')
    args.output.mkdir(parents=True, exist_ok=True)
    mapping = bytearray(source_map.read_bytes())
    if len(mapping) != 6840 or mapping[:4] != b'xDFP':
        raise SystemExit('Unsupported interactive music map')
    records = []
    with tempfile.NamedTemporaryFile(dir=args.output, delete=False) as output:
        temporary = Path(output.name)
        try:
            for index in range(43):
                offset, duration = struct.unpack_from('<II', mapping, 6496 + index * 8)
                # The exported PC header contains an explicit sample-rate tag.
                with source_music.open('rb') as stream:
                    stream.seek(offset * 128)
                    h = stream.read(128)
                size = struct.unpack_from('<I', h, 4)[0]
                if h[:4] != b'SCHl' or h[8:10] != b'PT' or size > 128:
                    raise ValueError('Unexpected source stream header')
                tags = {}; pos = 12
                while pos < size:
                    key = h[pos]; pos += 1
                    if key == 255: break
                    if key in (252, 253, 254): continue
                    length = h[pos]; pos += 1
                    tags[key] = int.from_bytes(h[pos:pos + length], 'big'); pos += length
                rate = tags.get(0x84, 22050)
                if rate not in (32000, 44100, 48000) or tags.get(0x82) != 2:
                    raise ValueError('Unsupported stream audio shape')
                pcm = decode(args.decoder, args.ffmpeg, source_map, index + 1)
                if len(pcm) // 4 != tags.get(0x85):
                    raise ValueError('Decoded sample count differs from header')
                start = output.tell()
                total = encode_pcm(output, pcm, rate)
                output.write(bytes(-output.tell() % 128))
                struct.pack_into('<II', mapping, 6496 + index * 8, start // 128, duration)
                record = dict(stream=index + 1, offset=start, bytes=output.tell() - start, rate=rate,
                              samples=total, pcm_sha256=hashlib.sha256(pcm).hexdigest())
                records.append(record)
                print(json.dumps(record), flush=True)
        except BaseException:
            output.close(); temporary.unlink(missing_ok=True); raise
    try:
        os.replace(temporary, args.output / 'MusicSFX.mus')
        (args.output / 'MusicSFX.mpf').write_bytes(mapping)
    finally:
        temporary.unlink(missing_ok=True)
    result = dict(source_map_sha256=MAP_SHA, source_music_sha256=MUSIC_SHA,
                  map_sha256=digest(args.output / 'MusicSFX.mpf'),
                  music_sha256=digest(args.output / 'MusicSFX.mus'), streams=records,
                  encoding='EA-XA v2 lossless PCM escape frames; Xbox GSTR')
    (args.output / 'conversion.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
