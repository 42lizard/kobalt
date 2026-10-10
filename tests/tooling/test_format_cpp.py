#!/usr/bin/env python3
"""Guard formatter version checks and the read-only default."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('format_cpp', ROOT / 'tools/format_cpp.py')
format_cpp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(format_cpp)


class FormatterTests(unittest.TestCase):
    def test_rejects_another_version_before_formatting(self):
        with patch.object(format_cpp.subprocess, 'check_output', return_value='clang-format version 19.1.0\n'), \
                patch.object(format_cpp.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'Expected clang-format 18.1.8'):
                format_cpp.format_files([Path('reader.cpp')], 'formatter')
            run.assert_not_called()

    def test_check_does_not_write_and_propagates_drift(self):
        with patch.object(format_cpp.subprocess, 'check_output', return_value='clang-format version 18.1.8\n'), \
                patch.object(format_cpp.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1)) as run:
            self.assertEqual(format_cpp.format_files([Path('reader.cpp')], 'formatter'), 1)
            command = run.call_args.args[0]
            self.assertIn('--dry-run', command)
            self.assertIn('--Werror', command)
            self.assertNotIn('-i', command)

    def test_source_inventory_excludes_ignored_and_non_cpp_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('src/reader.cpp', 'tests/a.h', 'src/list.json', 'src/deleted.cpp'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                if name != 'src/deleted.cpp':
                    path.write_text('')
            names = b'src/reader.cpp\0src/reader.cpp\0tests/a.h\0src/list.json\0src/deleted.cpp\0'
            with patch.object(format_cpp.subprocess, 'check_output', return_value=names) as inventory:
                self.assertEqual(format_cpp.source_files(root), [root / 'src/reader.cpp', root / 'tests/a.h'])
                self.assertIn('--exclude-standard', inventory.call_args.args[0])


if __name__ == '__main__':
    unittest.main()
