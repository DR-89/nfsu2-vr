import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('copchase', ROOT / 'tools/prepare-copchase.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
r = c.regional


class CopChaseImport(unittest.TestCase):
    @staticmethod
    def texture_pack():
        blobs = [(key, r.codec.jdlz_pack(bytes([key]) * (key * 33))) for key in (1, 2, 3)]
        hashes = b''.join(struct.pack('<II', key, 0) for key, _ in blobs)
        info = r.chunk(0x33310002, hashes) + r.chunk(0x33310003, bytes(24 * len(blobs)))
        output = bytearray(r.chunk(0xb3300000, r.chunk(0xb3310000, info) + r.chunk(0xb3320000, r.chunk(0x33320002, b''))))
        table, payload = 8 + 8 + len(hashes) + 8 + 8, len(output)
        for i, (key, blob) in enumerate(blobs):
            output += bytes(r.align(len(output), 128) - len(output))
            at = len(output)
            output += blob
            struct.pack_into('<6I', output, table + i * 24, key, at, len(blob), key * 33, 0x100, 0)
        r.put32(output, 4, len(output) - 8)
        parent = payload - 16
        r.put32(output, parent + 4, len(output) - parent - 8)
        r.put32(output, payload - 4, len(output) - payload)
        return bytes(output)

    def test_shared_skin_merge_preserves_other_entries(self):
        original = self.texture_pack()
        replacement = r.codec.jdlz_pack(b'new-native-skin' * 71)
        output, unchanged = c.replace_skins(original, {2: (replacement, len(b'new-native-skin' * 71))})
        self.assertEqual(unchanged, 2)
        _, _, before = c.texture_layout(original)
        _, _, after = c.texture_layout(output)
        for old, new in zip(before, after):
            actual = output[new[1]:new[1] + new[2]]
            if old[0] == 2:
                self.assertEqual(actual, replacement)
            else:
                self.assertEqual(actual, original[old[1]:old[1] + old[2]])
                self.assertEqual(old[2:], new[2:])
        with self.assertRaisesRegex(ValueError, 'Missing shared traffic skin'):
            c.replace_skins(original, {4: (replacement, 994)})
        malformed = bytearray(original)
        _, layout, _ = c.texture_layout(original)
        r.put32(malformed, layout[0x33310003][0] + 4, len(malformed))
        with self.assertRaisesRegex(ValueError, 'Invalid Xbox texture record'):
            c.replace_skins(malformed, {})

    def test_archive_alignment_and_duplicate_directory_replacement(self):
        files = {'CARS/TEXTURES.BIN': b'x' * 2100, 'CARS/BUS/GEOMETRY.BIN': b'g' * 2031}
        archive, locations = c.build_archive(files)
        self.assertEqual(len(archive) % 2048, 0)
        for item in locations.values():
            at = item['sector'] * 2048
            self.assertEqual(archive[at:at + item['bytes']], files[item['name']])
        shared, bus = c.hash_name('CARS/TEXTURES.BIN'), c.hash_name('CARS/BUS/GEOMETRY.BIN')
        rows = [(shared, 1, 4, 10, 100, 0x123), (17, 3, 4, 6, 200, 0x456),
                (bus, 2, 9, 15, 300, 0x789), (shared, 6, 7, 18, 100, 0xabc)]
        directory = b''.join(struct.pack('<6I', *row) for row in rows)
        output, plan = c.patch_directory(directory, locations, 5000)
        self.assertEqual(sum(len(item['original_rows']) for item in plan), 3)
        for old, new in zip(rows, struct.iter_unpack('<6I', output)):
            if old[0] in locations:
                item = locations[old[0]]
                self.assertEqual(new, (old[0], 13, item['sector'], 5000 + item['sector'], item['bytes'], old[5]))
            else:
                self.assertEqual(new, old)
        with self.assertRaisesRegex(ValueError, 'absent from original directory'):
            c.patch_directory(directory[:24], locations, 5000)

    def test_preloaded_shared_index_matches_rebuilt_stream(self):
        original = self.texture_pack()
        replacement = r.codec.jdlz_pack(b'replacement-skin' * 257)
        rebuilt, _ = c.replace_skins(original, {2: (replacement, len(b'replacement-skin' * 257))})
        _, layout, _ = c.texture_layout(original)
        table, size = layout[0x33310003]
        prefix_size = table + size
        memory = bytearray(struct.pack('<4I', 0, 0, 0x53219999, 1))
        memory += struct.pack('<5I', c.hash_name('CARS/TEXTURES.BIN'), 128, len(original), prefix_size, 0x12345678)
        memory += b'P' * (128 - len(memory)) + original[:prefix_size] + b'unrelated-music-and-models'
        patched, validation = c.replace_memory_texture_index(memory, original, rebuilt)
        self.assertEqual(len(patched), len(memory))
        self.assertEqual(r.u32(patched, 24), len(rebuilt))
        self.assertEqual(patched[128:128 + prefix_size], rebuilt[:prefix_size])
        self.assertEqual(patched[128 + prefix_size:], b'unrelated-music-and-models')
        self.assertEqual(r.u32(patched, 32), 0x12345678)
        self.assertEqual(validation['index_offset'], 128 + table)
        bad = bytearray(memory); bad[128 + table + 4] ^= 1
        with self.assertRaisesRegex(ValueError, 'shared prefix differs'):
            c.replace_memory_texture_index(bad, original, rebuilt)
        bad = bytearray(rebuilt); bad[table + 16] ^= 1
        with self.assertRaises(ValueError):
            c.replace_memory_texture_index(memory, original, bad)
        bad = bytearray(memory); r.put32(bad, 20, len(memory))
        with self.assertRaisesRegex(ValueError, 'entry bounds'):
            c.replace_memory_texture_index(bad, original, rebuilt)

    def test_soundtrack_selects_distinct_metadata_payload_in_same_archive(self):
        _, locations = c.build_archive({c.GLOBAL_MEMORY_NAME: b'plain-index',
                                       c.GLOBAL_MEMORY_RESTORED: b'restored-index',
                                       'CARS/TEXTURES.BIN': b'stream'})
        plain = c.overlay_locations(locations, False)
        restored = c.overlay_locations(locations, True)
        key = c.hash_name(c.GLOBAL_MEMORY_NAME)
        self.assertEqual(plain.keys(), restored.keys())
        self.assertNotIn(c.hash_name(c.GLOBAL_MEMORY_RESTORED), plain)
        self.assertNotEqual(plain[key]['sector'], restored[key]['sector'])
        directory = b''.join(struct.pack('<6I', key, 2, i, i, 38932, 123) for i in (1, 2))
        for selection in (plain, restored):
            result, plan = c.patch_directory(directory, {key: selection[key]}, 1000)
            self.assertEqual(len(plan[0]['original_rows']), 2)
            self.assertTrue(all(row[1:5] == (13, selection[key]['sector'], 1000 + selection[key]['sector'], selection[key]['bytes'])
                                for row in struct.iter_unpack('<6I', result)))
        with self.assertRaisesRegex(ValueError, 'Prepare the verified'):
            c.restored_memory(bytes(38932), bytes(6840))

    def test_path_bounds_and_case_collisions(self):
        for path in ('../escape', '/escape', 'C:/escape', 'a\\b', 'a//b', 'a/./b', 'a/../b', 'a./b'):
            with self.assertRaises(ValueError):
                c.safe_relative(path)
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            allowed = base / 'build/mod-assets'
            self.assertEqual(c.validate_output(allowed / 'copchase', base / 'game', base / 'mod.zip', allowed),
                             (allowed / 'copchase').resolve())
            for output in (base / 'game', allowed, base / 'elsewhere'):
                with self.assertRaises(ValueError):
                    c.validate_output(output, base / 'game', base / 'mod.zip', allowed)
            with self.assertRaises(ValueError):
                c.validate_output(allowed / 'copchase', allowed / 'copchase/game', base / 'mod.zip', allowed)
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, 'w') as archive:
                archive.writestr(c.PREFIX + 'A.bin', b'1')
                archive.writestr(c.PREFIX + 'a.bin', b'2')
            with zipfile.ZipFile(buffer) as archive:
                with self.assertRaisesRegex(ValueError, 'case-colliding'):
                    c.validate_zip_layout(archive)

    def test_wave_payload_is_validated(self):
        buffer = io.BytesIO()
        with wave.open(buffer, 'wb') as audio:
            audio.setnchannels(1)
            audio.setsampwidth(2)
            audio.setframerate(24000)
            audio.writeframes(bytes(240))
        data = buffer.getvalue()
        self.assertEqual(c.validate_audio(data), (1, 24000, 120))
        with self.assertRaisesRegex(ValueError, 'Truncated'):
            c.validate_audio(data[:-10])

    def test_native_audio_manifest_matches_binary_enumeration(self):
        heat10 = {f'heat10_m{number:05d}.wav' for number in (2932, 2933, 2935, 2936, 2937, 2938, 2939)}
        events = {'status': ['status_01.wav'], 'scanner': [f'scanner_{i:02d}.wav' for i in range(35)]}
        available, missing = c.native_audio_events(events, heat10 | {'status_01.wav'})
        self.assertEqual(available, {'status': ['status_01.wav'], 'heat10': sorted(heat10)})
        self.assertEqual(missing, {'scanner': events['scanner']})
        self.assertEqual(len(events), 2)  # Original provenance is unchanged.
        with self.assertRaisesRegex(ValueError, 'unlisted'):
            c.native_audio_events(events, heat10 | {'status_01.wav', 'unknown.wav'})
        with self.assertRaisesRegex(ValueError, 'unlisted'):
            c.native_audio_events(events, {'status_01.wav'})


if __name__ == '__main__':
    unittest.main()
