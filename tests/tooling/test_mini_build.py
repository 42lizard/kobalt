"""Check mini package contents, isolation and the attachment size limit."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
FONTTOOLS = ROOT / '.cache/kobalt' / json.loads((ROOT / 'dependencies/manifest.json').read_text())['fonttools']['directory'] / 'Lib'
sys.path.insert(0, str(ROOT / 'tools'))
import assets
import mini_fonts
from targets import output_path, target


class MiniBuildTests(unittest.TestCase):
    def test_mini_manifest_keeps_all_compatibility_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / 'resources/fonts'
            directory.mkdir(parents=True)
            names = [family + '-' + style + '.ttf' for family in ('Libron', 'NV_Bitter')
                     for style in ('Regular', 'Italic', 'Bold', 'BoldItalic')]
            core = dict(files=[dict(file=name) for name in names + ['Other-Regular.ttf']])
            (directory / 'ebook-core.json').write_text(json.dumps(core))
            def source(name, root, mini=False):
                path = root / 'mock' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
                return path
            with patch.object(assets.fetch_fonts, 'verify'), patch.object(assets, 'source_path', side_effect=source):
                document = json.loads(assets.manifest(root, version='v1.0-test', mini=True))
            paths = {item['path'] for item in document['files']}
            expected = {'fonts/' + name for name in assets.UI_FONTS + assets.FALLBACK_FONTS}
            expected |= {'hyph/' + name for name in assets.HYPHENATION}
            expected |= {'fonts/ebook-core/' + name for name in names}
            expected.add('fonts/ebook-core.json')
            self.assertEqual(paths, expected)
            for item in document['files']:
                self.assertEqual(item['sha256'], hashlib.sha256(item['path'].encode()).hexdigest())

    def test_core_manifest_only_lists_supplied_fonts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'resources/fonts/ebook-core.json'
            source.parent.mkdir(parents=True)
            core = dict(revision='a' * 40, files=[dict(file=family + '-' + style + '.ttf')
                        for family in ('Libron', 'NV_Bitter') for style in ('Regular', 'Italic', 'Bold', 'BoldItalic')] + [dict(file='Other-Regular.ttf')])
            source.write_text(json.dumps(core))
            generated = assets.source_path('fonts/ebook-core.json', root, mini=True)
            document = json.loads(generated.read_text())
            self.assertEqual(len(document['files']), 8)
            self.assertEqual(document['revision'], core['revision'])
            self.assertEqual(json.loads(source.read_text()), core)
            core['files'].pop(0)
            source.write_text(json.dumps(core))
            with self.assertRaisesRegex(ValueError, 'all four styles'):
                assets.source_path('fonts/ebook-core.json', root, mini=True)

    def test_size_limit_accepts_boundary_and_removes_oversize_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / 'Kobalt.zip'
            with package.open('wb') as stream:
                stream.truncate(assets.MINI_ZIP_LIMIT)
            assets.check_package_size(package, mini=True)
            with package.open('ab') as stream:
                stream.write(b'x')
            assets.check_package_size(package)
            with self.assertRaisesRegex(ValueError, 'exceeds 25,000,000'):
                assets.check_package_size(package, mini=True)
            self.assertFalse(package.exists())

    def test_mini_builds_never_reuse_full_build_directories(self):
        for name in ('qt5', 'qt6'):
            for temporary in (False, True):
                selected = target(name)
                full = output_path(selected, temporary=temporary)
                mini = output_path(selected, temporary=temporary, mini=True)
                self.assertNotEqual(full, mini)
                self.assertEqual(str(mini), str(full) + '-mini')

    @unittest.skipUnless(FONTTOOLS.is_dir(), 'Pinned FontTools is not cached')
    def test_cjk_keeps_every_face_mapping_metric_and_outline(self):
        sys.path.insert(0, str(FONTTOOLS))
        from fontTools.ttLib import TTCollection
        from fontTools.pens.recordingPen import RecordingPen
        generated = mini_fonts.prepare(ROOT)
        with TTCollection(ROOT / 'resources/fonts' / mini_fonts.CJK) as original, TTCollection(generated) as mini:
            self.assertEqual(len(original.fonts), 5)
            self.assertEqual(len(mini.fonts), 5)
            for before, after in zip(original.fonts, mini.fonts):
                self.assertEqual(before.getGlyphOrder(), after.getGlyphOrder())
                self.assertEqual(before.getBestCmap(), after.getBestCmap())
                self.assertEqual(set(before.keys()), set(after.keys()))
                for tag in before.keys():
                    if tag not in ('GlyphOrder', 'CFF ', 'head'):
                        self.assertEqual(before.getTableData(tag), after.getTableData(tag), tag)
            before = original.fonts[0].getGlyphSet()
            after = mini.fonts[0].getGlyphSet()
            for name in original.fonts[0].getGlyphOrder():
                left, right = RecordingPen(), RecordingPen()
                before[name].draw(left)
                after[name].draw(right)
                self.assertEqual(left.value, right.value, name)


if __name__ == '__main__':
    unittest.main()
