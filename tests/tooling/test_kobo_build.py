"""Exercise the shell entry point without running containers."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class KoboBuildScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'checkout with spaces'
        self.root.mkdir()
        self.source = Path(self.temp.name) / 'public source'
        self.source.mkdir()
        self.script = self.root / 'build-kobo.sh'
        self.script.write_bytes((ROOT / 'build-kobo.sh').read_bytes())
        self.bin = self.root / 'bin'; self.bin.mkdir()
        self.calls = self.root / 'calls'
        self.environment = dict(os.environ, PATH=str(self.bin) + ':' + os.environ['PATH'],
                                CALLS=str(self.calls), SOURCE=str(self.source), DIRTY='0', FAIL='0', CHANGE='0')
        self.tool('git', '#!/bin/sh\ncase "$3 $4" in\n  \'rev-parse --show-toplevel\') echo "$2" ;;\n  \'rev-parse HEAD\') echo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa ;;\n  \'status --porcelain\')\n    if [ "$DIRTY" = 1 ] || [ -f "$CALLS.changed" ]; then echo \' M source.cpp\'; fi ;;\n  *) exit 2 ;;\nesac\n')
        self.tool('python3', '#!/bin/sh\nif [ "$1" = -c ]; then echo "$SOURCE"; exit 0; fi\nprintf \'%s\\n\' "$*" >> "$CALLS"\n[ "$CHANGE" != 1 ] || touch "$CALLS.changed"\n[ "$FAIL" != 1 ] || exit 1\n')

    def tool(self, name, contents):
        path = self.bin / name; path.write_text(contents); path.chmod(0o755)

    def run_script(self, *arguments):
        return subprocess.run(['sh', str(self.script), *arguments], env=self.environment,
                              cwd=self.temp.name, capture_output=True, text=True)

    def test_builds_both_qt_targets_without_selecting_a_device(self):
        result = self.run_script('--jobs', '3')
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls.read_text().splitlines()
        self.assertEqual(len(calls), 2)
        for target, call in zip(('qt5', 'qt6'), calls):
            self.assertIn('--target ' + target, call)
            self.assertIn('--jobs 3', call)
            self.assertNotIn('clara', call)
            self.assertIn('--sdk-image ghcr.io/nicoverbruggen/kobuild:' + target + '-v0.1.0', call)
            self.assertNotIn('--personal', call)

    def test_failed_first_target_never_runs_second(self):
        self.environment['FAIL'] = '1'
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertEqual(len(self.calls.read_text().splitlines()), 1)

    def test_mini_builds_accept_uncommitted_fixes(self):
        self.environment['DIRTY'] = '1'
        result = self.run_script('--mini')
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls.read_text().splitlines()
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertIn('--mini --allow-dirty', call)
        self.assertIn('/build/mini/', result.stdout)

    def test_dirty_source_prevents_any_build(self):
        self.environment['DIRTY'] = '1'
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertFalse(self.calls.exists())

    def test_source_change_stops_before_second_target(self):
        self.environment['CHANGE'] = '1'
        self.assertNotEqual(self.run_script().returncode, 0)
        self.assertEqual(len(self.calls.read_text().splitlines()), 1)

    def test_invalid_jobs_and_target_override_never_start_a_build(self):
        for arguments in (('--jobs', '0'), ('--jobs', 'no'), ('--jobs',), ('--target', 'qt5')):
            with self.subTest(arguments=arguments):
                self.assertNotEqual(self.run_script(*arguments).returncode, 0)
                self.assertFalse(self.calls.exists())
