import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('install_copchase', ROOT/'tools/install-copchase.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class Staging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_paths(self):
        for path in ('../UDATA/save', '/absolute', 'C:/game', 'a\\b', 'a//b', 'a/./b', 'a/../b', 'a/b.', 'a/b ', 'a\0b'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                installer.relative(path)
        self.assertEqual(installer.relative('resources/audio/test.wav'), Path('resources/audio/test.wav'))

    def test_original_game_and_overlap_rejected(self):
        parent=self.root/'pcvr';parent.mkdir();(parent/'nfsu2-pcvr.exe').touch()
        source=self.root/'source';source.mkdir()
        self.assertEqual(installer.destination(parent,source,True),parent/'COPCHASE')
        for name in ('SPEED2.EXE','default.xbe','NFSUNDER'):
            (parent/name).touch()
            with self.assertRaises(ValueError):installer.destination(parent,source,True)
            (parent/name).unlink()
        with self.assertRaises(ValueError):installer.destination(parent,parent/'COPCHASE',True)
        with self.assertRaises(ValueError):installer.destination(self.root/'offline',source,False)

    def test_verified_atomic_copy_and_unchanged_skip(self):
        source=self.root/'source';source.write_bytes(b'new original resource')
        dest=self.root/'output'/'resource';checksum=installer.digest(source)
        self.assertTrue(installer.stage(source,dest,checksum))
        self.assertFalse(installer.stage(source,dest,checksum))
        self.assertEqual(dest.read_bytes(),source.read_bytes())
        dest.write_bytes(b'old destination')
        source.write_bytes(b'changed during copy')
        with self.assertRaises(ValueError):installer.stage(source,dest,checksum)
        self.assertEqual(dest.read_bytes(),b'old destination')
        self.assertFalse(list(dest.parent.glob('.copchase-*')))

    def test_manifest_trust_and_expected_layout(self):
        source=self.root/'source';source.mkdir();(source/'manifest.json').write_text('{}')
        with self.assertRaises(ValueError):installer.verified(source)
        self.assertFalse(installer.runtime_name('CARS/SUV/GEOMETRY.BIN'))
        self.assertFalse(installer.runtime_name('GLOBAL/GLOBALMEMORYFILE.BIN'))
        self.assertFalse(installer.runtime_name('CopChase.asi'))
        self.assertTrue(installer.runtime_name('resources/audio/tyre_pop.wav'))
        self.assertTrue(installer.runtime_name('ZDIR-SONGS.BIN'))

    def test_verify_only_and_unrelated_files_preserved(self):
        source=self.root/'source';source.mkdir();asset=source/'asset';asset.write_bytes(b'asset')
        parent=self.root/'pcvr';parent.mkdir();(parent/'nfsu2-pcvr.exe').touch()
        sentinel=parent/'UDATA'/'career';sentinel.parent.mkdir();sentinel.write_bytes(b'save')
        files=[(asset,'resources/audio/test.wav',installer.digest(asset))]
        with mock.patch.object(installer,'verified',return_value=files):
            report=installer.install(source,parent,True,True)
            self.assertEqual(report['written'],0);self.assertEqual(report['different'],['resources/audio/test.wav'])
            self.assertFalse((parent/'COPCHASE').exists())
            report=installer.install(source,parent,True)
            self.assertEqual(report['written'],1)
            report=installer.install(source,parent,True,True)
            self.assertEqual(report['different'],[])
        self.assertEqual(sentinel.read_bytes(),b'save')

    def test_symlink_escape(self):
        outside=self.root/'outside';outside.mkdir();link=self.root/'link'
        try:link.symlink_to(outside,target_is_directory=True)
        except OSError:self.skipTest('symlink privilege unavailable')
        with self.assertRaises(ValueError):installer.bounded(self.root,'link/asset')
        with self.assertRaises(ValueError):installer.unlinked(link)


if __name__ == '__main__':
    unittest.main()
