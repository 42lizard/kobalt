import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import ui_review
import native_screenshots


class ReviewTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.build = self.root / 'build'

    def captures(self, name, images, ppi=300):
        folder = self.root / name
        folder.mkdir()
        shots = []
        for screen, data in images.items():
            (folder / (screen + '.png')).write_bytes(data)
            shots.append(dict(file=screen + '.png', group='settings', theme='light',
                              ppi=ppi, dpr=1, width=100, height=200))
        (folder / 'manifest.json').write_text(json.dumps(dict(complete=True, screens=shots)))
        return folder

    def test_pairing_preserves_original_differences_and_resolution(self):
        before = self.captures('before', dict(changed=b'a', same=b's', missing=b'm'))
        after = self.captures('after', dict(changed=b'b', same=b's', additional=b'n'))
        result = ui_review.compare(before, after, self.build, 'first')
        self.assertEqual((result['unchanged'], result['differences']), (1, 3))
        records = json.loads((self.build / 'screenshots/ui-review.json').read_text())
        self.assertEqual({item['kind'] for item in records}, {'changed', 'missing', 'additional'})
        ui_review.compare(after, before, self.build, 'resolved', cause='Restored baseline')
        retained = json.loads((self.build / 'screenshots/ui-review.json').read_text())
        self.assertEqual(retained[:3], records)
        self.assertEqual(len(retained), 6)
        after.joinpath('changed.png').write_bytes(b'regenerated')
        changed = next(item for item in records if item['kind'] == 'changed')
        self.assertEqual((self.build / 'screenshots' / changed['after']['image']).read_bytes(), b'b')

    def test_multi_density_identities_and_prior_default_metadata(self):
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'a'))
        manifest = json.loads((after / 'manifest.json').read_text())
        manifest['screens'][0]['reading_ppi'] = 300
        shot = dict(manifest['screens'][0], file='ppi-212/tab.png', ppi=212, reading_ppi=212)
        (after / 'ppi-212').mkdir()
        (after / shot['file']).write_bytes(b'212 image')
        manifest['screens'].append(shot)
        (after / 'manifest.json').write_text(json.dumps(manifest))
        result = ui_review.compare(before, after, self.build, 'density')
        self.assertEqual((result['unchanged'], result['differences']), (1, 1))

    def test_gallery_regeneration_preserves_review_and_links(self):
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'b'))
        ui_review.compare(before, after, self.build, 'first')
        folder = self.build / 'screenshots'
        records = (folder / 'ui-review.json').read_bytes()
        native_screenshots.report(folder, dict(complete=True, groups=[], screens=[], width=100, height=200))
        self.assertEqual((folder / 'ui-review.json').read_bytes(), records)
        page = (folder / 'index.html').read_text()
        self.assertIn('href="ui-review.html"', page)
        self.assertIn('href="../refactor-journal/index.html"', page)

    def test_unrelated_books_do_not_change_settings_environment(self):
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'a'))
        for folder, books in [(before, dict(alice=dict(sha256='alice'))),
                              (after, dict(alice=dict(sha256='alice'), maths=dict(sha256='maths')))]:
            manifest = json.loads((folder / 'manifest.json').read_text())
            manifest['books'] = books
            (folder / 'manifest.json').write_text(json.dumps(manifest))
        result = ui_review.compare(before, after, self.build, 'unchanged')
        self.assertEqual((result['unchanged'], result['differences']), (1, 0))
        manifest['books']['alice']['sha256'] = 'changed'
        (after / 'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'environments differ'):
            ui_review.compare(before, after, self.build, 'different fixture')

    def test_dictionary_provenance_changes_reject_pairing(self):
        before = self.captures('before', dict(definition=b'a'))
        after = self.captures('after', dict(definition=b'a'))
        for folder in (before, after):
            manifest = json.loads((folder / 'manifest.json').read_text())
            manifest['screens'][0].update(group='dictionary', dictionary_reference='pinned',
                                          dictionary_fixture='emulated-management-state')
            manifest['dictionaries'] = {'en-en': {'sha256': 'pinned dictionary'}}
            (folder / 'manifest.json').write_text(json.dumps(manifest))
        self.assertEqual(ui_review.compare(before, after, self.build, 'same')['unchanged'], 1)
        original = json.loads((after / 'manifest.json').read_text())
        for field in ('dictionary', 'dictionary_reference', 'dictionary_fixture'):
            with self.subTest(field=field):
                manifest = json.loads(json.dumps(original))
                if field == 'dictionary':
                    manifest['dictionaries']['en-en']['sha256'] = 'changed'
                else:
                    manifest['screens'][0][field] = 'changed'
                (after / 'manifest.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(ValueError, 'environments differ'):
                    ui_review.compare(before, after, self.build, 'different dictionary')

    def test_unchanged_run_summary_survives_later_comparisons(self):
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'a'))
        ui_review.compare(before, after, self.build, 'all equal')
        folder = self.build / 'screenshots'
        summary = json.loads((folder / 'ui-review-runs.json').read_text())[0]
        self.assertEqual((summary['unchanged'], summary['differences']), (1, 0))
        self.assertIn('all equal</h3>', (folder / 'ui-review.html').read_text())
        self.assertIn('<strong>1</strong> unchanged', (folder / 'ui-review.html').read_text())
        (after / 'tab.png').write_bytes(b'changed')
        ui_review.compare(before, after, self.build, 'later batch')
        runs = json.loads((folder / 'ui-review-runs.json').read_text())
        self.assertEqual(runs[0], summary)
        self.assertEqual(len(runs), 2)
        page = (folder / 'ui-review.html').read_text()
        self.assertIn('all equal</h3>', page)
        self.assertIn('<summary>later batch · 1 record</summary>', page)
        self.assertIn('loading="lazy"', page)

    def test_environment_mismatch_keeps_existing_report(self):
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'b'))
        ui_review.compare(before, after, self.build, 'first')
        report = self.build / 'screenshots/ui-review.json'
        original = report.read_bytes()
        mismatch = self.captures('wrong-dpr', dict(tab=b'b'))
        manifest = json.loads((mismatch / 'manifest.json').read_text())
        manifest['screens'][0]['dpr'] = 2
        (mismatch / 'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'environments differ'):
            ui_review.compare(before, mismatch, self.build, 'invalid')
        self.assertEqual(report.read_bytes(), original)

    def test_font_update_requires_exact_asset_provenance(self):
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'b'))
        for folder, checksum in [(before, 'old assets'), (after, 'new assets')]:
            manifest = json.loads((folder / 'manifest.json').read_text())
            manifest['assets_sha256'] = checksum
            (folder / 'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'environments differ'):
            ui_review.compare(before, after, self.build, 'default strict')
        expected = {'assets_sha256': ('old assets', 'new assets')}
        result = ui_review.compare(before, after, self.build, 'font update',
                                   expected_environment_changes=expected)
        self.assertEqual(result['differences'], 1)
        records = json.loads((self.build / 'screenshots/ui-review.json').read_text())
        self.assertEqual(records[0]['before']['environment']['assets_sha256'], 'old assets')
        self.assertEqual(records[0]['after']['environment']['assets_sha256'], 'new assets')
        manifest['screens'][0]['dpr'] = 2
        (after / 'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'environments differ'):
            ui_review.compare(before, after, self.build, 'different dpr', expected_environment_changes=expected)
        with self.assertRaisesRegex(ValueError, 'environments differ'):
            ui_review.compare(before, after, self.build, 'wrong asset hash',
                              expected_environment_changes={'assets_sha256': ('old assets', 'other assets')})

    def test_journal_append_refresh_and_immutable_images(self):
        image = self.root / 'example.png'
        image.write_bytes(b'first image')
        ui_review.journal(self.build, 'Phase 1', 'First', tests=['passed:pairing'], images=[image])
        entries = self.build / 'refactor-journal/entries.json'
        original = json.loads(entries.read_text())[0]
        image.write_bytes(b'regenerated')
        ui_review.journal(self.build, 'Phase 1', 'Second', reviews=['difference-1'])
        records = json.loads(entries.read_text())
        self.assertEqual(records[0], original)
        self.assertEqual([item['message'] for item in records], ['First', 'Second'])
        self.assertEqual((self.build / 'refactor-journal' / original['images'][0]).read_bytes(), b'first image')
        page = (self.build / 'refactor-journal/index.html').read_text()
        self.assertIn('http-equiv="refresh" content="20"', page)
        self.assertLess(page.index('id="entry-2"'), page.index('<article id="entry-1"'))
        self.assertIn('href="#entry-2"', page)
        self.assertIn('/screenshots/ui-review.html#difference-1', page)

    def test_dashboard_updates_both_pages_and_preserves_append_order(self):
        ui_review.journal(self.build, 'Phase 1', 'Initial progress', tests=['passed:first check'])
        ui_review.journal(self.build, 'Phase 1 report', 'Latest readable progress',
                          files=['tools/ui_review.py'], tests=['blocked:hardware unavailable'])
        for path in ('refactor-journal/index.html', 'screenshots/ui-review.html'):
            page = (self.build / path).read_text()
            self.assertIn('Latest readable progress', page)
            self.assertIn('badge blocked', page)
            self.assertIn('hardware unavailable', page)
            self.assertIn('http-equiv="refresh" content="20"', page)
            self.assertIn('sessionStorage', page)
            self.assertIn('width=device-width', page)
            self.assertIn('<time datetime="', page)
            self.assertIn(' UTC</time>', page)
        journal_page = (self.build / 'refactor-journal/index.html').read_text()
        self.assertEqual(journal_page.count('id="entry-2"'), 1)
        self.assertEqual(journal_page.count('Latest readable progress'), 1)
        entries = json.loads((self.build / 'refactor-journal/entries.json').read_text())
        self.assertEqual([item['message'] for item in entries], ['Initial progress', 'Latest readable progress'])
        before = self.captures('before', dict(tab=b'a'))
        after = self.captures('after', dict(tab=b'b'))
        ui_review.compare(before, after, self.build, 'historical',
                          classification='Retained gallery capture outside Phase 1 inventory')
        ui_review.compare(before, after, self.build, 'current demo')
        page = (self.build / 'screenshots/ui-review.html').read_text()
        self.assertLess(page.index('current demo</h3>'), page.index('historical</h3>'))
        self.assertIn('Historical capture inventory · historical', page)
        self.assertIn('Environment and source metadata', page)
        self.assertIn('target="_blank"', page)
        self.assertIn('max-height:360px', page)
        self.assertIn('id="difference-1"', page)
        self.assertIn('id="difference-2"', page)

    def test_server_serves_only_report_artifacts(self):
        ui_review.journal(self.build, 'Phase 1', 'Start')
        (self.build / 'secret.txt').write_text('excluded')
        (self.build / 'refactor-journal/leak.txt').symlink_to(self.build / 'secret.txt')
        with ui_review.server(self.build) as server:
            worker = threading.Thread(target=server.serve_forever)
            worker.start()
            self.addCleanup(worker.join)
            self.addCleanup(server.shutdown)
            origin = f'http://127.0.0.1:{server.server_port}'
            with urllib.request.urlopen(origin + '/refactor-journal/index.html') as response:
                self.assertIn(b'content="20"', response.read())
            for path in ('/secret.txt', '/refactor-journal/leak.txt', '/refactor-journal/../../secret.txt'):
                with self.assertRaises(urllib.error.HTTPError) as raised:
                    urllib.request.urlopen(origin + path)
                self.assertEqual(raised.exception.code, 404)


if __name__ == '__main__':
    unittest.main()
