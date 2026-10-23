"""PocketBook releases must contain tested device packages and matching sources."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import draft_release as github
import fetch_pocketbook_firmware as firmware
import release_sources
from pocketbook.prepare_firmware import SWUPDATE_SHA256

TAG = 'v1.0-beta.3-pocketbook.1'
COMMIT = 'a' * 40


class PocketBookReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / 'artifacts'
        self.output.mkdir()
        (self.root / 'CHANGELOG.md').write_text('## ' + TAG + '\n\nPocketBook beta.\n')
        (self.root / 'pocketbook').mkdir()
        (self.root / 'pocketbook/release-installation.md').write_text(
            (ROOT / 'pocketbook/release-installation.md').read_text())
        for key, name in github.POCKETBOOK_ARTIFACTS.items():
            path = self.output / name
            path.write_bytes(key.encode())
            record = {'commit': COMMIT, 'dirty': False, 'file': name, 'version': TAG,
                      'sha256': github.digest(path), 'target': 'PB743K3', 'targets': ['pocketbook'],
                      'runtime_tests': True, 'firmware_sha256': SWUPDATE_SHA256}
            (self.output / (key + '.json')).write_text(json.dumps(record))
        mock = patch.object(github, 'checkout', return_value={'commit': COMMIT, 'dirty': False})
        mock.start(); self.addCleanup(mock.stop)
        mock = patch.object(github.subprocess, 'check_output', return_value=COMMIT)
        mock.start(); self.addCleanup(mock.stop)

    def draft(self, existing=None):
        response = subprocess.CompletedProcess([], 1)
        if existing is not None:
            response = subprocess.CompletedProcess([], 0, json.dumps(existing))
        with patch.object(github.subprocess, 'run', return_value=response) as run:
            github.draft(self.root, self.output, TAG, 'owner/kobalt', 'pocketbook')
        return [call.args[0] for call in run.call_args_list]

    def test_draft_uploads_only_pocketbook_sources_and_checksums(self):
        creation = self.draft()[-1]
        self.assertEqual(creation[:3], ['gh', 'release', 'create'])
        self.assertIn('--draft', creation)
        self.assertIn('--prerelease', creation)
        assets = {Path(arg).name for arg in creation if str(self.output) in arg}
        self.assertEqual(assets, {'Kobalt-PB743K3.zip', 'Kobalt-source.tar.gz',
                                 'SHA256SUMS', 'BUILD-INFO.json', 'release-notes.md'})
        notes = (self.output / 'release-notes.md').read_text()
        self.assertIn('/' + TAG + '/Kobalt-PB743K3.zip', notes)
        self.assertIn('U743k3.6.10.2854', notes)
        self.assertNotIn('Kobo', notes)
        self.assertNotIn('{{', notes)
        info = json.loads((self.output / 'BUILD-INFO.json').read_text())
        self.assertEqual(set(info['artifacts']), {'pocketbook', 'source'})
        self.assertEqual(len((self.output / 'SHA256SUMS').read_text().splitlines()), 2)

    def test_invalid_package_or_source_is_rejected_before_remote_writes(self):
        for key, field, value in [('pocketbook', 'runtime_tests', False),
                                  ('pocketbook', 'firmware_sha256', 'wrong'),
                                  ('pocketbook', 'target', 'qt5'),
                                  ('pocketbook', 'dirty', True),
                                  ('pocketbook', 'sha256', 'wrong'),
                                  ('source', 'targets', ['qt5', 'qt6']),
                                  ('source', 'commit', 'b' * 40)]:
            with self.subTest(field=field):
                path = self.output / (key + '.json')
                original = path.read_text()
                record = json.loads(original)
                record[field] = value
                path.write_text(json.dumps(record))
                with patch.object(github.subprocess, 'run') as run:
                    with self.assertRaises(ValueError):
                        github.draft(self.root, self.output, TAG, 'owner/kobalt', 'pocketbook')
                    run.assert_not_called()
                path.write_text(original)

    def test_published_release_is_preserved(self):
        with self.assertRaisesRegex(ValueError, 'published release'):
            self.draft({'isDraft': False, 'tagName': TAG})

    def test_rerun_removes_kobo_packages_from_existing_draft(self):
        commands = self.draft({'isDraft': True, 'tagName': TAG,
                               'assets': [{'name': 'Kobalt-qt5.zip'}, {'name': 'Kobalt-qt6.zip'}]})
        removed = [command[4] for command in commands if command[2] == 'delete-asset']
        self.assertEqual(removed, ['Kobalt-qt5.zip', 'Kobalt-qt6.zip'])


class FirmwareDownloadTests(unittest.TestCase):
    def test_extracts_only_verified_firmware(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            with zipfile.ZipFile(output / 'PB743K3.zip', 'w') as archive:
                archive.writestr('SWUPDATE.BIN', b'valid firmware')
                archive.writestr('../unrelated.txt', b'ignored')
            checksum = hashlib.sha256(b'valid firmware').hexdigest()
            with patch.object(firmware, 'SWUPDATE_SHA256', checksum), patch.object(firmware, 'download'):
                binary = firmware.fetch(output)
            self.assertEqual(binary.read_bytes(), b'valid firmware')
            self.assertFalse((output.parent / 'unrelated.txt').exists())

    def test_verified_firmware_needs_no_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            binary = output / 'SWUPDATE.BIN'
            binary.write_bytes(b'firmware')
            checksum = hashlib.sha256(binary.read_bytes()).hexdigest()
            with patch.object(firmware, 'SWUPDATE_SHA256', checksum), patch.object(firmware, 'download') as download:
                self.assertEqual(firmware.fetch(output), binary)
                download.assert_not_called()

    def test_corrupt_download_does_not_replace_previous_firmware(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            binary = output / 'SWUPDATE.BIN'
            binary.write_bytes(b'previous input')
            with zipfile.ZipFile(output / 'PB743K3.zip', 'w') as archive:
                archive.writestr('SWUPDATE.BIN', b'corrupt firmware')
            with patch.object(firmware, 'download'):
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    firmware.fetch(output)
            self.assertEqual(binary.read_bytes(), b'previous input')


class PocketBookSourceTests(unittest.TestCase):
    def test_source_bundle_selects_pocketbook_sdk_without_kobo_archives_or_firmware(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('dependencies', 'pocketbook', 'resources/fonts'):
                (root / name).mkdir(parents=True)
            (root / 'dependencies/manifest.json').write_text('{}')
            (root / 'dependencies/release-sources.json').write_text(json.dumps({
                'qt5-base': {'url': 'kobo'},
                'ebook-fonts': {'url': 'https://example.org/fonts/pin', 'file': 'fonts.tar.gz'}}))
            sdk = {'sdk': {'url': 'https://example.org/sdk', 'file': 'sdk.tar.gz'}}
            (root / 'pocketbook/release-sources.json').write_text(json.dumps(sdk))
            (root / 'resources/fonts/ebook-core.json').write_text('{"revision": "pin", "files": []}')
            with patch.object(release_sources.subprocess, 'check_output', return_value=b''), \
                    patch.object(release_sources, 'download') as download, \
                    patch('fetch_fonts.fetch'):
                files = release_sources.inputs(root, target='pocketbook')
            self.assertEqual(set(files), {'.cache/release-sources/fonts.tar.gz',
                                          '.cache/release-sources/sdk.tar.gz'})
            self.assertEqual(download.call_count, 2)


if __name__ == '__main__':
    unittest.main()
