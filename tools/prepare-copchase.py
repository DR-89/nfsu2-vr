#!/usr/bin/env python3
"""Prepare privately owned CopChase v0.3.2 assets for the native Xbox VR port.

This is an offline asset importer, not the pursuit implementation or an
installer. It never runs the PC plugin and never changes the source game.
Generated mod/game assets must remain private and outside version control.
"""
import argparse
import collections
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import stat
import struct
import tempfile
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD_ASSETS = ROOT / 'build/mod-assets'
spec = importlib.util.spec_from_file_location('copchase_regional', ROOT / 'tools/prepare-community-regional.py')
regional = importlib.util.module_from_spec(spec)
spec.loader.exec_module(regional)
PREFIX = 'CopChase v0.3.2/'
ZIP_SHA = 'f7d68dc92dd2cb78d7dcc3e09e1e17b1444156cd8d674c0e3c72997e5d984f8d'
PLUGIN_SHA = '43590d849ba616253e0e975269f7db62537c74013d4f2177fe179ffa0d24ea9e'
DIRECTORY_SHA = '254212625c80eb720daf139290b8d19aa159cb6890bebc75d11c2d3da52fb722'
SHARED_SHA = '7ad9698411ab58da21c2fd6277f169aa4efbd92da094615ecb27355c09cb3769'
SONGS_DIRECTORY_SHA = 'd97ca4681691163425a0033764eaa5e0c43e1d2355736c8ba5f3e6d7cbf2bef2'
GLOBAL_MEMORY_NAME = 'GLOBAL/GLOBALMEMORYFILE.BIN'
GLOBAL_MEMORY_RESTORED = 'GLOBAL/GLOBALMEMORYFILE-RESTORED.BIN'
GLOBAL_MEMORY_SHA = '9438f54a8832dbcaf82e6c13d53078ee47aa92d54f4ec95e4feba4adc0ae6bb2'
RESTORED_GLOBAL_SHA = '40261ad0ccab2864e04a98a8118496034206b83567d3c2af7ab202482d6b2bf8'
RESTORED_MPF_SHA = 'cb3d11a25c5ac1c752aad68bfc045ec72b1df0bb05ef91685d90fae9948b592d'
DEFAULT_RESTORED_MPF = ROOT / 'build/optional-mod-assets/SONGS/MusicSFX.mpf'
SLOTS = ('4DR_SEDAN', '4DR_SEDAN02', 'AMBULANCE', 'BUS', 'COUPE',
         'FIRETRUCK', 'HATCHBACK', 'HATCHBACK02', 'MINIVAN', 'PANELVAN',
         'PARCELVAN', 'PICKUP', 'SUV', 'TAXI', 'TAXI02')
# The package installer's recommended mixed population, in its original order.
MIXED = ('4DR_SEDAN', 'MINIVAN', 'COUPE', 'TAXI02', 'HATCHBACK', 'BUS', 'PICKUP', 'SUV')
ARCHIVE_INDEX = 13  # Restored soundtrack reserves 10, 11 and 12.
SONGS_SIZES = (1626612096, 38932, 6840)
NOTICE = '''CopChase by Vitorini Mods — v0.3.2
https://www.patreon.com/vitorini/posts/copchase-v0-3-2-174278861

Privately converted from the user's original CopChase v0.3.2 ZIP.
The original Terms of Use are preserved in provenance/Terms of Use.txt.
Original and converted mod/game assets retain their respective owners' rights;
they are not covered by the port's source-code licence. Do not redistribute
the assets, or include them in source control or mod packs without permission.

This optional pack contains converted traffic geometry/textures and original
audio/map resources. The Windows ASI is neither installed nor executed.
Preparing these files does not implement or establish playable pursuits.
Use a separate runtime sector-cache namespace when mounting this overlay.
'''


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def hash_name(name):
    value = 0xffffffff
    for ch in name.replace('/', '\\').upper().encode('ascii'):
        value = (value * 33 + ch) & 0xffffffff
    return value


def safe_relative(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name
            and '\0' not in name, 'Invalid relative asset path')
    parts = name.split('/')
    require(all(p and p not in ('.', '..') and not p.endswith((' ', '.')) for p in parts),
            'Unsafe relative asset path')
    require(not PurePosixPath(name).is_absolute(), 'Absolute asset path')
    return PurePosixPath(name)


def bounded_path(root, name):
    relative = safe_relative(name)
    base = root.resolve()
    path = (base / relative).resolve()
    require(path != base and path.is_relative_to(base), 'Asset path escapes output root')
    return path


def validate_output(output, game, archive, allowed=BUILD_ASSETS):
    output = output.resolve()
    root = allowed.resolve()
    if allowed == BUILD_ASSETS:
        require(root.is_relative_to(ROOT.resolve() / 'build'),
                'Private asset root resolves outside the workspace build directory')
    require(output != root and output.is_relative_to(root),
            'Output must be a private subdirectory of build/mod-assets/')
    game = game.resolve()
    require(not output.is_relative_to(game) and not game.is_relative_to(output),
            'Output overlaps the source game')
    require(not archive.resolve().is_relative_to(output), 'Output overlaps the input archive')
    return output


def write_asset(root, name, data, files, source=None, kind=None):
    path = bounded_path(root, name)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Resolve again after creating parents, so existing junctions cannot redirect
    # a generated file outside the explicitly bounded private output directory.
    path = bounded_path(root, name)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.copchase-', delete=False) as out:
            temporary = Path(out.name)
            out.write(data)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    record = {'bytes': len(data), 'sha256': sha(data)}
    if source is not None:
        record['source_sha256'] = sha(source)
    if kind:
        record['kind'] = kind
    files[name] = record


def validate_zip_layout(archive):
    seen = set()
    for entry in archive.infolist():
        name = entry.filename
        safe_relative(name.rstrip('/'))
        require(name.startswith(PREFIX), 'Unexpected archive root/version')
        require(name.casefold() not in seen, 'Duplicate or case-colliding archive path')
        seen.add(name.casefold())
        require(not entry.flag_bits & 1, 'Encrypted ZIP member unsupported')
        require(not stat.S_ISLNK(entry.external_attr >> 16), 'ZIP symlink unsupported')
        require(0 <= entry.file_size <= 64 * 1024 * 1024, 'Oversized ZIP member')
    require(len(seen) == 1124 and sum(i.file_size for i in archive.infolist()) == 289515531,
            'Unexpected v0.3.2 package layout')
    required = {PREFIX + 'files/CARS/' + slot + '/' + name for slot in SLOTS
                for name in ('GEOMETRY.BIN', 'TEXTURES.BIN')}
    actual = {i.filename for i in archive.infolist() if i.filename.startswith(PREFIX + 'files/CARS/')}
    require(actual == required, 'Unexpected traffic-model file set')
    require(sha(archive.read(PREFIX + 'files/scripts/CopChase.asi')) == PLUGIN_SHA,
            'Unsupported CopChase plugin version')


def unpack_texture(blob):
    require(len(blob) >= 16 and 0 < regional.u32(blob, 8) <= 8 * 1024 * 1024,
            'Invalid compressed texture size')
    if blob[:4] == b'HUFF':
        raw = regional.codec.huff(blob, 8 * 1024 * 1024)
    elif blob[:4] == b'JDLZ':
        raw = regional.codec.jdlz_game(blob)
    else:
        raise ValueError('Unsupported texture compression')
    require(raw is not None and len(raw) == regional.u32(blob, 8), 'Invalid compressed texture')
    return raw


def texture_layout(data):
    root = list(regional.chunks(data))
    require(len(root) == 1 and root[0][0] == 0xb3300000, 'Invalid Xbox texture pack root')
    parents, layout = {}, {}
    for tag, at, size in regional.chunks(data, 8, len(data)):
        if not tag:
            continue
        require(tag in (0xb3310000, 0xb3320000) and tag not in parents,
                'Unknown or duplicate Xbox texture parent')
        parents[tag] = (at, size)
        for key, start, count in regional.chunks(data, at, at + size):
            if key:
                require(key not in layout, 'Duplicate Xbox texture chunk')
                layout[key] = (start, count)
    require(all(key in layout for key in (0x33310002, 0x33310003, 0x33320002)),
            'Missing Xbox compressed texture index')
    table, size = layout[0x33310003]
    hashes, hash_size = layout[0x33310002]
    payload, payload_size = layout[0x33320002]
    require(size % 24 == 0 and size // 24 == hash_size // 8 and hash_size % 8 == 0
            and 0 < size // 24 <= 4096 and payload + payload_size == len(data),
            'Invalid Xbox texture index bounds')
    rows = [struct.unpack_from('<6I', data, table + i * 24) for i in range(size // 24)]
    previous = -1
    for i, (key, at, packed, raw, flags, reserved) in enumerate(rows):
        require(key > previous and struct.unpack_from('<II', data, hashes + i * 8) == (key, 0)
                and flags == 0x100 and reserved == 0 and payload <= at < at + packed <= len(data)
                and packed >= 16 and 0 < raw <= 8 * 1024 * 1024,
                'Invalid Xbox texture record')
        require(regional.u32(data, at + 8) == raw, 'Mismatched Xbox texture decompression size')
        previous = key
    return parents, layout, rows


def replace_skins(original, skins):
    parents, layout, rows = texture_layout(original)
    require(set(skins) <= {row[0] for row in rows}, 'Missing shared traffic skin')
    table = layout[0x33310003][0]
    payload = layout[0x33320002][0]
    out = bytearray(original[:regional.align(payload, 128)])
    for i, (key, at, size, raw, flags, reserved) in enumerate(rows):
        blob = original[at:at + size]
        if key in skins:
            blob, raw = skins[key]
            require(len(unpack_texture(blob)) == raw, 'Invalid replacement skin')
        out += bytes(regional.align(len(out), 128) - len(out))
        start = len(out)
        out += blob
        struct.pack_into('<6I', out, table + i * 24, key, start, len(blob), raw, flags, reserved)
    regional.put32(out, 4, len(out) - 8)
    regional.put32(out, parents[0xb3320000][0] - 4, len(out) - parents[0xb3320000][0])
    regional.put32(out, payload - 4, len(out) - payload)
    _, _, updated = texture_layout(out)
    for old, new in zip(rows, updated):
        key, at, size, raw, flags, reserved = old
        blob = out[new[1]:new[1] + new[2]]
        if key in skins:
            require(blob == skins[key][0] and new[3] == skins[key][1], 'Skin replacement differs')
        else:
            require(blob == original[at:at + size] and new[2:] == old[2:],
                    'Unrelated shared texture changed')
    return bytes(out), len(rows) - len(skins)


def solid_meshes(data):
    top = list(regional.chunks(data))[0]
    solids = {}
    for tag, at, size in regional.chunks(data, top[1], top[1] + top[2]):
        if tag != 0x80134010:
            continue
        children = list(regional.chunks(data, at, at + size))
        parts = {k: (a, n) for k, a, n in children}
        require(len(parts) == len(children) and 0x134011 in parts and 0x80134100 in parts,
                'Invalid or duplicate solid chunks')
        start, count = parts[0x134011]
        name = data[start + 0xa4:start + count].split(b'\0')[0].decode('ascii')
        require(name and name not in solids, 'Missing or duplicate solid name')
        start, count = parts[0x80134100]
        mesh = {k: (a, n) for k, a, n in regional.chunks(data, start, start + count)}
        solids[name] = (parts, mesh)
    return solids


def validate_geometry(source, output, slot):
    """Check the converted streams independently of converter serialization."""
    old, new = solid_meshes(source), solid_meshes(output)
    expected = {slot + '_' + suffix for suffix in ('BODY_A', 'BODY_B', 'BODY_C', 'TIRE_FRONT_A', 'TIRE_FRONT_B')}
    require(old.keys() == new.keys() == expected, 'Unexpected police model solids')
    totals = {'solids': len(old), 'vertices': 0, 'indices': 0, 'strips': 0}
    for name, (old_parts, old_mesh) in old.items():
        parts, mesh = new[name]
        require(old_parts.keys() == parts.keys(), 'Converted solid metadata differs')
        for key in old_parts:
            if key != 0x80134100:
                a, n = old_parts[key]
                b, m = parts[key]
                require(source[a:a + n] == output[b:b + m], 'Converted solid metadata changed')
        header = regional.align(old_mesh[0x134900][0], 16)
        vertices, strips = regional.u32(source, header + 52), regional.u32(source, header + 16)
        native_header = regional.align(mesh[0x134b00][0], 16)
        require(struct.unpack_from('<I4H', output, native_header)
                == (15, regional.u32(source, header + 12), vertices, strips, 32),
                'Converted mesh header differs')
        pc_vertices = regional.align(old_mesh[0x134b01][0], 128)
        native_vertices = regional.align(mesh[0x134b01][0], 128)
        for i in range(vertices):
            v = source[pc_vertices + i * 36:pc_vertices + (i + 1) * 36]
            w = output[native_vertices + i * 32:native_vertices + (i + 1) * 32]
            require(w == v[:12] + v[28:36] + v[12:24], 'Converted vertex differs')
        indices = output[regional.align(mesh[0x134b03][0], 16):sum(mesh[0x134b03])]
        require(indices == source[regional.align(old_mesh[0x134b03][0], 16):sum(old_mesh[0x134b03])],
                'Converted index stream differs')
        strip_at = regional.align(mesh[0x134b02][0], 16)
        for i in range(strips):
            at = strip_at + i * 64
            low, high = struct.unpack_from('<3f', output, at), struct.unpack_from('<3f', output, at + 12)
            count, _, _, first = struct.unpack_from('<4I', output, at + 24)
            require(count % 3 == 0 and first + count <= len(indices) // 2, 'Invalid converted strip')
            for index in struct.unpack_from(f'<{count}H', indices, first * 2):
                require(index < vertices, 'Converted index outside vertex stream')
                pos = struct.unpack_from('<3f', output, native_vertices + index * 32)
                require(all(math.isfinite(x) and low[k] - .011 <= x <= high[k] + .011
                            for k, x in enumerate(pos)), 'Converted geometry outside strip bounds')
        totals['vertices'] += vertices
        totals['indices'] += len(indices) // 2
        totals['strips'] += strips
    return totals


def read_original(game):
    disc = game / 'NFSUNDER'
    directory = (disc / 'ZDIR.BIN').read_bytes()
    require(len(directory) == 32688 and sha(directory) == DIRECTORY_SHA, 'Unsupported original Xbox ZDIR')
    require(not any((disc / f'ZZDATA{i}.BIN').exists() for i in range(10, 16)),
            'Source game already contains an archive overlay')
    sizes = [(disc / f'ZZDATA{i}.BIN').stat().st_size for i in range(10)]
    base = sum((size + 2047) // 2048 for size in sizes)
    require(base == 1188935, 'Unsupported original Xbox archive sizes')
    matching = [struct.unpack_from('<6I', directory, i) for i in range(0, len(directory), 24)
                if regional.u32(directory, i) == hash_name('CARS/TEXTURES.BIN')]
    require(len(matching) == 23, 'Unexpected shared-texture duplicate count')
    shared = None
    for _, archive, sector, _, size, _ in matching:
        require(archive < 10 and sector * 2048 + size <= sizes[archive], 'Invalid original shared texture row')
        with (disc / f'ZZDATA{archive}.BIN').open('rb') as stream:
            stream.seek(sector * 2048)
            candidate = stream.read(size)
        require(len(candidate) == 1795840 and sha(candidate) == SHARED_SHA,
                'Original shared texture duplicate differs')
        shared = candidate
    return directory, shared, base, sizes


def read_global_memory(game, directory):
    rows = [struct.unpack_from('<6I', directory, at) for at in range(0, len(directory), 24)
            if regional.u32(directory, at) == hash_name(GLOBAL_MEMORY_NAME)]
    require(len(rows) == 2, 'Unexpected GLOBALMEMORYFILE duplicate count')
    result = None
    for _, archive, sector, _, size, _ in rows:
        require(archive < 10 and size == 38932, 'Unsupported original GLOBALMEMORYFILE row')
        with (game / f'NFSUNDER/ZZDATA{archive}.BIN').open('rb') as stream:
            stream.seek(sector * 2048)
            data = stream.read(size)
        require(sha(data) == GLOBAL_MEMORY_SHA, 'Unsupported original GLOBALMEMORYFILE')
        result = data
    return result


def memory_entry(memory, name):
    require(len(memory) >= 16 and regional.u32(memory, 8) == 0x53219999,
            'Invalid global memory-file header')
    count = regional.u32(memory, 12)
    require(0 < count <= 64 and 16 + count * 20 <= len(memory), 'Invalid global memory-file count')
    matches = []
    for at in range(16, 16 + count * 20, 20):
        key, offset, total, embedded, _ = struct.unpack_from('<5I', memory, at)
        require(offset >= 16 + count * 20 and offset + embedded <= len(memory) and embedded <= total,
                'Invalid global memory-file entry bounds')
        if key == hash_name(name):
            matches.append((at, offset, total, embedded))
    require(len(matches) == 1, 'Missing or duplicate global memory-file entry: ' + name)
    return matches[0]


def replace_memory_texture_index(memory, original, replacement):
    # The title preloads this prefix from GLOBALMEMORYFILE before streaming
    # CARS/TEXTURES. Updating only the latter leaves the old offsets/sizes live.
    entry, offset, total, embedded = memory_entry(memory, 'CARS/TEXTURES.BIN')
    require(total == len(original) and memory[offset:offset + embedded] == original[:embedded],
            'Global memory-file shared prefix differs from original texture pack')
    _, old_layout, old_rows = texture_layout(original)
    _, new_layout, new_rows = texture_layout(replacement)
    table, size = old_layout[0x33310003]
    require(new_layout[0x33310003] == (table, size) and table + size <= embedded,
            'Shared index no longer fits the embedded memory-file prefix')
    require(len(old_rows) == len(new_rows) and all(a[0] == b[0] and a[4:] == b[4:]
            for a, b in zip(old_rows, new_rows)), 'Shared index keys or flags changed')
    expected = bytearray(original[:embedded])
    regional.put32(expected, 4, len(replacement) - 8)
    expected[table:table + size] = replacement[table:table + size]
    require(replacement[:embedded] == expected, 'Unexpected shared-prefix metadata changes')
    result = bytearray(memory)
    regional.put32(result, entry + 8, len(replacement))
    result[offset:offset + embedded] = expected
    allowed = [(entry + 8, entry + 12), (offset + 4, offset + 8), (offset + table, offset + table + size)]
    require(len(result) == len(memory) and all(a == b or any(lo <= i < hi for lo, hi in allowed)
            for i, (a, b) in enumerate(zip(memory, result))), 'Unrelated global memory-file bytes changed')
    require(result[offset + table:offset + table + size] == replacement[table:table + size],
            'Embedded texture index differs from streamed pack')
    return bytes(result), {'entry_offset': entry, 'prefix_offset': offset, 'prefix_bytes': embedded,
                           'index_offset': offset + table, 'index_bytes': size, 'entries': len(new_rows),
                           'changed_ranges': allowed, 'unrelated_bytes_preserved': True}


def restored_memory(memory, mpf):
    require(len(mpf) == 6840 and sha(mpf) == RESTORED_MPF_SHA,
            'Prepare the verified restored soundtrack MPF before composing CopChase assets')
    _, offset, total, embedded = memory_entry(memory, 'SOUND/PFDATA/MUSICSFX.MPF')
    require(total == embedded == len(mpf), 'Unsupported embedded soundtrack MPF length')
    result = memory[:offset] + mpf + memory[offset + embedded:]
    require(sha(result) == RESTORED_GLOBAL_SHA, 'Restored global memory-file composition differs')
    return result


def overlay_locations(locations, restored):
    alternate = hash_name(GLOBAL_MEMORY_RESTORED)
    result = {key: item for key, item in locations.items() if key != alternate}
    require(alternate in locations and hash_name(GLOBAL_MEMORY_NAME) in result, 'Missing composed memory-file payload')
    if restored:
        result[hash_name(GLOBAL_MEMORY_NAME)] = dict(locations[alternate], name=GLOBAL_MEMORY_NAME,
                                                   physical_name=GLOBAL_MEMORY_RESTORED)
    return result


def build_archive(files):
    data, locations = bytearray(), {}
    for name, content in sorted(files.items()):
        key = hash_name(name)
        require(key not in locations, 'Archive name hash collision')
        data += bytes(regional.align(len(data), 2048) - len(data))
        locations[key] = {'name': name, 'sector': len(data) // 2048, 'bytes': len(content), 'sha256': sha(content)}
        data += content
    data += bytes(regional.align(len(data), 2048) - len(data))
    return bytes(data), locations


def patch_directory(original, locations, base):
    require(len(original) % 24 == 0 and base >= 0, 'Invalid archive directory')
    output, plan = bytearray(original), []
    for key, item in locations.items():
        rows = []
        require(item['sector'] >= 0 and item['bytes'] > 0 and base + item['sector'] <= 0xffffffff,
                'Invalid overlay archive location')
        for at in range(0, len(original), 24):
            row = struct.unpack_from('<6I', original, at)
            if row[0] != key:
                continue
            struct.pack_into('<4I', output, at + 4, ARCHIVE_INDEX, item['sector'], base + item['sector'], item['bytes'])
            rows.append({'index': at // 24, 'original': list(row)})
        require(rows, f'Overlay hash absent from original directory: {key:08X}')
        plan.append(dict(item, name_hash=f'{key:08X}', archive_index=ARCHIVE_INDEX,
                         global_sector=base + item['sector'], original_rows=rows))
    for at in range(0, len(original), 24):
        row, changed = struct.unpack_from('<6I', original, at), struct.unpack_from('<6I', output, at)
        if row[0] not in locations:
            require(changed == row, 'Unrelated archive directory row changed')
        else:
            require(changed[0] == row[0] and changed[5] == row[5], 'Unknown directory metadata changed')
    return bytes(output), plan


def soundtrack_directory(original):
    # Reproduce only the existing verified soundtrack's directory mapping.
    # Its audio files are not copied and must be verified by the runtime.
    output = bytearray(original)
    for at in range(0, len(output), 24):
        key = regional.u32(output, at)
        if key == 1628346117:
            struct.pack_into('<4I', output, at + 4, 10, 0, 850398, SONGS_SIZES[0])
        elif key == 2228279645:
            struct.pack_into('<2I', output, at + 4, 11, 0)
        elif key == 1628345939:
            struct.pack_into('<2I', output, at + 4, 12, 0)
    require(sha(output) == SONGS_DIRECTORY_SHA, 'Restored soundtrack directory layout differs')
    return bytes(output)


def read_skins(archive):
    path = PREFIX + 'installer/skins/'
    skins = {}
    for line in archive.read(path + 'skins.txt').decode('ascii').splitlines():
        name, key_text, size_text, flags_text = line.split()
        require(name.endswith('_SKIN1') and name[:-6] in SLOTS and name[:-6] not in skins,
                'Unknown or duplicate police skin')
        key = int(key_text, 16)
        require(int(flags_text) == 256 and key == hash_name(name), 'Unexpected skin hash or flags')
        source = archive.read(path + name + '.dat')
        raw = unpack_texture(source)
        require(len(raw) == int(size_text) and regional.u32(raw, len(raw) - 156 + 0x24) == key,
                'Police skin metadata differs')
        native = regional.texture_block(raw)
        encoded = regional.codec.jdlz_pack(native)
        require(unpack_texture(encoded) == native, 'Police skin JDLZ round trip failed')
        skins[name[:-6]] = {'key': key, 'blob': encoded, 'bytes': len(native), 'source_sha256': sha(source)}
    require(skins.keys() == set(SLOTS), 'Incomplete police-skin set')
    return skins


def validate_audio(data):
    with wave.open(io.BytesIO(data), 'rb') as stream:
        require(stream.getcomptype() == 'NONE' and stream.getsampwidth() == 2
                and stream.getnchannels() in (1, 2) and stream.getframerate() in (22050, 24000, 32000, 36000)
                and stream.getnframes() > 0, 'Unsupported CopChase WAV format')
        frames, channels, rate = stream.getnframes(), stream.getnchannels(), stream.getframerate()
        require(len(stream.readframes(frames)) == frames * channels * 2, 'Truncated CopChase WAV')
    return channels, rate, frames


def native_audio_events(events, wavs):
    require(isinstance(events, dict) and events, 'Invalid audio event manifest')
    native_events, absent = {}, {}
    for event, clips in events.items():
        require(isinstance(event, str) and isinstance(clips, list) and clips
                and all(isinstance(clip, str) and safe_relative(clip) and '/' not in clip for clip in clips),
                'Invalid audio manifest entry')
        available = [clip for clip in clips if clip in wavs]
        missing = [clip for clip in clips if clip not in wavs]
        if available:
            native_events[event] = available
        if missing:
            absent[event] = missing
    # The pinned original release retains 35 scanner references but supplies
    # none of those samples. Preserve the original manifest as provenance and
    # provide an explicit usable event list without inventing replacement audio.
    require(set(absent) == {'scanner'} and len(absent['scanner']) == 35
            and all(clip.startswith('scanner_') for clip in absent['scanner']),
            'Unexpected missing audio resources in source package')
    # ASI 0x10024D00 enumerates <bank>_*.wav. The package manifest predates
    # these seven heat-10 lines; they are present and selected by the binary.
    unlisted = wavs - {clip for clips in events.values() for clip in clips}
    expected = {f'heat10_m{number:05d}.wav' for number in (2932, 2933, 2935, 2936, 2937, 2938, 2939)}
    require(unlisted == expected and 'heat10' not in native_events, 'Unexpected unlisted audio resources')
    native_events['heat10'] = sorted(unlisted)
    return native_events, absent


def copy_resources(archive, output, files):
    prefix = PREFIX + 'files/scripts/CopChase/'
    names = [i.filename[len(prefix):] for i in archive.infolist() if i.filename.startswith(prefix) and not i.is_dir()]
    wavs = {name[6:] for name in names if name.startswith('audio/') and name.endswith('.wav')}
    require(len(names) == 1068 and len(wavs) == 1063, 'Unexpected CopChase resources')
    events = json.loads(archive.read(prefix + 'audio/manifest.json'))
    native_events, absent = native_audio_events(events, wavs)
    formats, seconds = collections.Counter(), 0.0
    for name in names:
        data = archive.read(prefix + name)
        if name.endswith('.wav'):
            channels, rate, frames = validate_audio(data)
            formats[f'PCM16/{channels}ch/{rate}Hz'] += 1
            seconds += frames / rate
        write_asset(output, 'resources/' + name, data, files, source=data, kind='original-resource')
    write_asset(output, 'resources/audio/native-manifest.json',
                (json.dumps(native_events, indent=2) + '\n').encode('utf-8'), files,
                source=archive.read(prefix + 'audio/manifest.json'), kind='validated-native-audio-events')
    for name in ('README.txt', 'Terms of Use.txt', 'CHANGELOG.txt'):
        data = archive.read(PREFIX + name)
        write_asset(output, 'provenance/' + name, data, files, source=data)
    data = archive.read(PREFIX + 'files/scripts/CopChase.ini')
    write_asset(output, 'provenance/CopChase.ini', data, files, source=data, kind='reference-PC-settings')
    return {'files': len(wavs), 'formats': dict(sorted(formats.items())), 'seconds': round(seconds, 6),
            'native_manifest': 'resources/audio/native-manifest.json', 'missing_source_references': absent,
            'additional_native_bank': {'heat10': native_events['heat10']},
            'loading': 'Stream music; load short samples on demand. Do not preload the complete audio directory.'}


def prepare(archive_path, game, output, density, restored_mpf=DEFAULT_RESTORED_MPF):
    archive_path, game = archive_path.resolve(), game.resolve()
    output = validate_output(output, game, archive_path)
    require(archive_path.stat().st_size == 268858142 and file_sha(archive_path) == ZIP_SHA,
            'Unsupported archive: requires the original CopChase-v0.3.2.zip SHA256 ' + ZIP_SHA)
    require(density in ('mixed', 'all'), 'Unknown police density')
    directory, original_shared, base, archive_sizes = read_original(game)
    global_memory = read_global_memory(game, directory)
    global_restored = restored_memory(global_memory, Path(restored_mpf).read_bytes())
    slots = MIXED if density == 'mixed' else SLOTS
    files, converted, validation = {}, {}, {}
    with zipfile.ZipFile(archive_path) as archive:
        validate_zip_layout(archive)
        skins = read_skins(archive)
        for slot in SLOTS:
            print(f'Converting {slot}', flush=True)
            for name in ('GEOMETRY.BIN', 'TEXTURES.BIN'):
                relative = f'CARS/{slot}/{name}'
                source = archive.read(PREFIX + 'files/' + relative)
                data = regional.geometry(source) if name == 'GEOMETRY.BIN' else regional.textures(source)
                if name == 'GEOMETRY.BIN':
                    validation[slot] = validate_geometry(source, data, slot)
                write_asset(output, relative, data, files, source=source, kind='converted-native-asset')
                if slot in slots:
                    converted[relative] = data
        shared, unchanged = replace_skins(original_shared, {skins[s]['key']: (skins[s]['blob'], skins[s]['bytes']) for s in slots})
        converted['CARS/TEXTURES.BIN'] = shared
        write_asset(output, 'CARS/TEXTURES.BIN', shared, files, source=original_shared, kind='merged-native-shared-textures')
        for name, source in ((GLOBAL_MEMORY_NAME, global_memory), (GLOBAL_MEMORY_RESTORED, global_restored)):
            converted[name], memory_validation = replace_memory_texture_index(source, original_shared, shared)
            write_asset(output, name, converted[name], files, source=source, kind='composed-global-memory-file')
        packed, locations = build_archive(converted)
        write_asset(output, 'ZZDATA13.BIN', packed, files, kind='native-archive-overlay')
        directory_plain, plan = patch_directory(directory, overlay_locations(locations, False), base)
        write_asset(output, 'ZDIR.BIN', directory_plain, files, source=directory, kind='directory-with-original-soundtrack')
        songs_directory = soundtrack_directory(directory)
        songs_base = base + sum((size + 2047) // 2048 for size in SONGS_SIZES)
        directory_songs, songs_plan = patch_directory(songs_directory, overlay_locations(locations, True), songs_base)
        write_asset(output, 'ZDIR-SONGS.BIN', directory_songs, files, source=songs_directory, kind='directory-with-restored-soundtrack')
        print('Validating and copying audio/map resources', flush=True)
        audio = copy_resources(archive, output, files)
    write_asset(output, 'NOTICE.txt', NOTICE.encode('utf-8'), files)
    manifest = {
        'format': 'NCPCH1', 'version': '0.3.2', 'credit': 'CopChase by Vitorini Mods',
        'source_url': 'https://www.patreon.com/vitorini/posts/copchase-v0-3-2-174278861',
        'source_archive': {'name': archive_path.name, 'bytes': 268858142, 'sha256': ZIP_SHA, 'plugin_sha256': PLUGIN_SHA},
        'asset_preparation_only': True, 'density': density, 'active_slots': list(slots), 'available_slots': list(SLOTS),
        'original': {'directory_sha256': DIRECTORY_SHA, 'shared_textures_sha256': SHARED_SHA,
                     'global_memory_sha256': GLOBAL_MEMORY_SHA, 'archive_sizes': archive_sizes},
        'overlay': {'archive_index': ARCHIVE_INDEX, 'archive': 'ZZDATA13.BIN', 'rows': plan,
                    'duplicate_rows_patched': sum(len(row['original_rows']) for row in plan),
                    'variants': {
                        'original_soundtrack': {'directory': 'ZDIR.BIN', 'input_directory_sha256': DIRECTORY_SHA,
                                                'archive_base_sector': base, 'required_other_archives': []},
                        'restored_soundtrack': {'directory': 'ZDIR-SONGS.BIN', 'input_directory_sha256': SONGS_DIRECTORY_SHA,
                                                'archive_base_sector': songs_base, 'required_other_archives': [10, 11, 12],
                                                'global_memory_row': next(row for row in songs_plan if row['name'] == GLOBAL_MEMORY_NAME)}},
                    'runtime_cache_namespace': f'copchase-0.3.2-{density}-v2',
                    'mount_requirements': 'Verify files and source disc; choose the matching soundtrack variant; use an isolated sector cache. '
                                          'Global sector values use cumulative archive sizes; runtime integration must validate mounting.'},
        'skin_sources': {slot: {'name_hash': f"{skins[slot]['key']:08X}", 'sha256': skins[slot]['source_sha256']} for slot in SLOTS},
        'validation': {'models': validation, 'unrelated_shared_textures_byte_exact': unchanged,
                       'global_memory_file': memory_validation, 'restored_mpf_sha256': RESTORED_MPF_SHA, 'audio': audio},
        'files': dict(sorted(files.items())),
        'remaining_runtime_work': ['Native pursuit logic and traffic-slot selection', 'Map/sightmap decoding and coordinate validation',
                                   'Audio events and streaming', 'PCVR and Quest rendering/gameplay validation']}
    write_asset(output, 'manifest.json', (json.dumps(manifest, indent=2) + '\n').encode('utf-8'), {})
    print(f'Prepared private assets: {output}\n{len(slots)} archive slots; {len(SLOTS)} converted models; '
          f'{manifest["overlay"]["duplicate_rows_patched"]} directory rows; {audio["files"]} WAV files.\n'
          'No game files changed; native pursuit/runtime integration is still required.', flush=True)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='Original local CopChase-v0.3.2.zip')
    parser.add_argument('--game', type=Path, default=ROOT / 'game', help='Read-only original Xbox game root')
    parser.add_argument('--output', type=Path, default=BUILD_ASSETS / 'copchase')
    parser.add_argument('--density', choices=('mixed', 'all'), default='mixed')
    parser.add_argument('--restored-mpf', type=Path, default=DEFAULT_RESTORED_MPF,
                        help='Verified prepared soundtrack metadata (6840 bytes), used for the composed music variant')
    args = parser.parse_args()
    try:
        prepare(args.archive, args.game, args.output, args.density, args.restored_mpf)
    except (OSError, ValueError, KeyError, struct.error, zipfile.BadZipFile, wave.Error) as error:
        parser.exit(1, f'CopChase preparation failed: {error}\n')


if __name__ == '__main__':
    main()
