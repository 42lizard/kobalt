import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import ui_icon_vectors as vectors


class VectorAssets(unittest.TestCase):
    def test_all_pinned_shapes_compile_deterministically(self):
        directory = ROOT / 'resources/icons/lucide'
        generated = vectors.generate(directory)
        self.assertEqual(generated, vectors.generate(directory))
        manifest = json.loads((directory / 'manifest.json').read_text())
        for item in manifest['files']:
            self.assertIn('QStringLiteral("' + Path(item['file']).stem + '")', generated)
        commands, stroke = vectors.geometry(directory / 'chart-no-axes-column-increasing.svg',
                                            next(x for x in manifest['files'] if x['file'].startswith('chart-')))
        self.assertEqual(stroke, 4)
        self.assertTrue(commands)

    def test_quarter_arc_keeps_endpoints_and_sweep(self):
        # Both arcs on Lucide's Type icon connect horizontal and vertical strokes.
        self.assertEqual(vectors.path_commands('M4 5a1 1 0 0 1 1-1')[-1],
                         'path.cubicTo(4, 4.44771525017, 4.44771525017, 4, 5, 4);')
        self.assertEqual(vectors.path_commands('M4 5a1 1 0 0 0 1-1')[-1],
                         'path.cubicTo(4.55228474983, 5, 5, 4.55228474983, 5, 4);')

    def test_unsupported_or_incomplete_paths_fail(self):
        for path in ['M0 0Q1 1 2 2', 'M0', 'M0 0A2 1 0 0 1 2 1', 'M0 0a1 1 45 0 1 1 1',
                     'M0 0a1 1 0 1 1 1 1', 'M0 0a1 1 0 0 2 1 1', 'M0 0L1e999 1']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                vectors.path_commands(path)

    def test_changed_svg_and_unsupported_elements_fail(self):
        source = ROOT / 'resources/icons/lucide/x.svg'
        data = source.read_bytes()
        entry = {'sha256': hashlib.sha256(data).hexdigest()}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'x.svg'
            path.write_bytes(data + b' ')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                vectors.geometry(path, entry)
            unsupported = data.replace(b'<path d="M18 6 6 18" />', b'<image href="external.svg" />')
            path.write_bytes(unsupported)
            with self.assertRaisesRegex(ValueError, 'unsupported SVG element'):
                vectors.geometry(path, {'sha256': hashlib.sha256(unsupported).hexdigest()})

    def test_manifest_paths_cannot_escape_asset_directory(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            for filename in ['../x.svg', '/tmp/x.svg']:
                (path / 'manifest.json').write_text(json.dumps({'files': [{'file': filename}]}))
                with self.assertRaisesRegex(ValueError, 'filename'):
                    vectors.generate(path)


if __name__ == '__main__':
    unittest.main()
