"""Version metadata follows the release tag."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import version


class VersionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.git('init', '-q')
        (self.root / 'VERSION').write_text('v1.0-beta.1\n')
        self.git('add', 'VERSION')
        self.git('commit', '-qm', 'Fixture')
        environment = patch.dict(os.environ, {'KOBALT_RELEASE_VERSION': ''})
        environment.start()
        self.addCleanup(environment.stop)

    def git(self, *arguments):
        subprocess.run(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false', '-c', 'core.hooksPath=/dev/null',
                        *arguments], cwd=self.root, check=True, capture_output=True)

    def test_untagged_checkout_uses_version_file(self):
        self.assertEqual(version.release_version(self.root), 'v1.0-beta.1')

    def test_invalid_version_file_is_rejected(self):
        (self.root / 'VERSION').write_text('invalid\n')
        with self.assertRaises(ValueError):
            version.release_version(self.root)

    def test_missing_version_file_is_rejected(self):
        (self.root / 'VERSION').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing VERSION'):
            version.release_version(self.root)

    def test_tag_at_head_decides_beta_version(self):
        self.git('tag', 'v1.0-beta.2')
        self.assertEqual(version.release_version(self.root), 'v1.0-beta.2')

    def test_tag_on_older_commit_does_not_name_current_build(self):
        self.git('tag', 'v1.0-beta.1')
        self.git('commit', '--allow-empty', '-qm', 'Next fixture')
        self.assertEqual(version.release_version(self.root), 'v1.0-beta.1')

    def test_ambiguous_tags_require_an_explicit_version(self):
        self.git('tag', 'v1.0-beta.1')
        self.git('tag', 'v1.0-beta.2')
        with self.assertRaisesRegex(ValueError, 'Multiple version tags'):
            version.release_version(self.root)
        with patch.dict(os.environ, {'KOBALT_RELEASE_VERSION': 'v1.0-beta.2'}):
            self.assertEqual(version.release_version(self.root), 'v1.0-beta.2')

    def test_explicit_version_is_validated(self):
        with patch.dict(os.environ, {'KOBALT_RELEASE_VERSION': 'not-a-version'}):
            with self.assertRaises(ValueError):
                version.release_version(self.root)


if __name__ == '__main__':
    unittest.main()
