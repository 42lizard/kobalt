import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
module = importlib.util.spec_from_file_location('variable_font', ROOT / 'tests/fixtures/variable_font.py')
variable_font = importlib.util.module_from_spec(module)
module.loader.exec_module(variable_font)


def font(tables=None):
    # These bytes test fixture validation only, never rendering or font loading.
    axes = b''.join(struct.pack('>4siiiHH', tag, minimum << 16, default << 16, maximum << 16, 0, 256)
                    for tag, minimum, default, maximum in ((b'wght', 100, 900, 900), (b'opsz', 9, 9, 144)))
    tables = tables if tables is not None else {
        b'fvar': struct.pack('>HHHHHHHH', 1, 0, 16, 2, 2, 20, 0, 8) + axes,
        b'gvar': b'variation outlines', b'HVAR': b'variation advances'}
    data = struct.pack('>IHHHH', 0x10000, len(tables), 0, 0, 0)
    offset = 12 + len(tables) * 16
    for tag, contents in tables.items():
        data += struct.pack('>4sIII', tag, 0, offset, len(contents))
        offset += len(contents)
    return data + b''.join(tables.values())


class VariableFont(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / 'source.ttf'
        self.destination = self.root / 'staged'
        self.manifest = self.root / 'fixture.json'
        self.spec = json.loads(variable_font.MANIFEST.read_text())
        (self.root / self.spec['notice']).write_bytes(b'fixture license')
        self.save(font())

    def save(self, data):
        self.source.write_bytes(data)
        self.spec.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.manifest.write_text(json.dumps(self.spec))

    def test_stages_verified_bytes_and_license_and_replaces_old_fixture(self):
        self.destination.mkdir()
        target = self.destination / self.spec['file']
        target.write_bytes(b'old fixture')
        self.assertEqual(variable_font.stage(self.source, self.destination, self.manifest), target)
        self.assertEqual(target.read_bytes(), self.source.read_bytes())
        self.assertEqual((self.destination / 'OFL.txt').read_bytes(), b'fixture license')

    def test_changed_bytes_do_not_replace_existing_fixture(self):
        variable_font.stage(self.source, self.destination, self.manifest)
        target = self.destination / self.spec['file']
        original = target.read_bytes()
        changed = bytearray(original)
        changed[-1] ^= 1
        self.source.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            variable_font.stage(self.source, self.destination, self.manifest)
        self.assertEqual(target.read_bytes(), original)

    def test_missing_linked_and_wrong_size_sources_are_rejected(self):
        self.source.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or linked'):
            variable_font.stage(self.source, self.destination, self.manifest)
        other = self.root / 'other.ttf'
        other.write_bytes(font())
        self.source.symlink_to(other)
        with self.assertRaisesRegex(ValueError, 'Missing or linked'):
            variable_font.stage(self.source, self.destination, self.manifest)
        self.source.unlink()
        self.source.write_bytes(b'short')
        with self.assertRaisesRegex(ValueError, 'size mismatch'):
            variable_font.stage(self.source, self.destination, self.manifest)
        self.assertFalse(self.destination.exists())

    def test_axes_and_required_variation_tables_are_checked(self):
        self.spec['axes']['wght'] = [100, 400, 900]
        self.save(font())
        with self.assertRaisesRegex(ValueError, 'axis mismatch'):
            variable_font.stage(self.source, self.destination, self.manifest)
        self.save(font({b'fvar': b'fake axes', b'gvar': b'outlines'}))
        with self.assertRaisesRegex(ValueError, 'Missing.*variation tables'):
            variable_font.stage(self.source, self.destination, self.manifest)
        self.assertFalse(self.destination.exists())

    def test_invalid_tables_and_axes_are_rejected(self):
        for data, message in ((b'bad', 'header'), (font()[:20], 'directory'),
                              (font({b'fvar': b'short', b'gvar': b'a', b'HVAR': b'b'}), 'Truncated.*axes')):
            with self.subTest(message=message):
                self.save(data)
                with self.assertRaisesRegex(ValueError, message):
                    variable_font.stage(self.source, self.destination, self.manifest)
        self.assertFalse(self.destination.exists())

    def test_manifest_tracks_the_pinned_public_dependency(self):
        spec = json.loads(variable_font.MANIFEST.read_text())
        dependencies = json.loads((ROOT / 'dependencies/manifest.json').read_text())
        pinned = dependencies[spec['source']['dependency']]
        self.assertEqual(pinned['sha256'], spec['source']['archive_sha256'])
        self.assertEqual(pinned['directory'], 'harfbuzz-' + spec['source']['version'])
        notice = variable_font.MANIFEST.with_name(spec['notice']).read_text()
        self.assertIn('Copyright 2020 The Fraunces Project Authors', notice)
        self.assertIn('SIL OPEN FONT LICENSE Version 1.1', notice)


if __name__ == '__main__':
    unittest.main()
