"""The PocketBook entry point must work from a standalone checkout."""
import importlib.util
import hashlib
from contextlib import ExitStack
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]

class BuildTest(unittest.TestCase):
    def test_entry_point_help_outside_checkout(self):
        result = subprocess.run(['sh', str(ROOT / 'build-pocketbook.sh'), '--help'],
                                cwd=tempfile.gettempdir(), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--firmware', result.stdout)
        self.assertIn('--test', result.stdout)

    def test_failed_build_never_replaces_existing_package(self):
        spec = importlib.util.spec_from_file_location('build_pocketbook', ROOT / 'tools/build_pocketbook.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'build/pocketbook/Kobalt-PB743K3.zip'
            package.parent.mkdir(parents=True)
            package.write_bytes(b'previous verified package')
            with patch.object(builder, 'ROOT', root), \
                    patch.object(builder, 'prepare', return_value=None), \
                    patch.object(builder.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'build')):
                with self.assertRaises(subprocess.CalledProcessError):
                    builder.inside_container(root / 'SWUPDATE.BIN', 2, False)
            self.assertEqual(package.read_bytes(), b'previous verified package')

    def test_firmware_cache_reuses_verified_files_and_repairs_corruption(self):
        spec = importlib.util.spec_from_file_location('prepare_firmware', ROOT / 'pocketbook/prepare_firmware.py')
        firmware = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(firmware)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'SWUPDATE.BIN'
            source.write_bytes(b'firmware fixture')

            def extract(archive, output, *members):
                output.mkdir(parents=True, exist_ok=True)
                for member in members:
                    (output / Path(member).name).write_bytes(member.encode())

            with patch.object(firmware, 'SWUPDATE_SHA256', hashlib.sha256(source.read_bytes()).hexdigest()), \
                    patch.object(firmware.shutil, 'which', side_effect=lambda name: '/usr/bin/7z' if name == '7z' else None), \
                    patch.object(firmware, 'image') as unpack, \
                    patch.object(firmware, 'extract', side_effect=extract):
                output = root / 'prepared'
                firmware.prepare(source, output)
                firmware.prepare(source, output)
                self.assertEqual(unpack.call_count, 2)
                library = output / 'rootfs/lib/libc.so.6'
                library.write_bytes(b'corrupted')
                firmware.prepare(source, output)
                self.assertEqual(unpack.call_count, 4)
                self.assertEqual(library.read_bytes(), b'lib/libc.so.6')

    def test_source_change_keeps_previous_package(self):
        spec = importlib.util.spec_from_file_location('build_pocketbook', ROOT / 'tools/build_pocketbook.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory() as directory, ExitStack() as patches:
            root = Path(directory)
            firmware = root / 'SWUPDATE.BIN'
            firmware.touch()
            output = root / 'build/pocketbook'
            output.mkdir(parents=True)
            package = output / 'Kobalt-PB743K3.zip'
            package.write_bytes(b'previous verified package')
            binary = output / 'arm/app/kobalt'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'new compiled reader')
            assets = output / 'arm/assets/install/.adds/kobalt/assets'
            assets.mkdir(parents=True)
            (assets / 'manifest.json').write_text('{"version": "v1"}')
            for name in ('LICENSE', 'ACKNOWLEDGEMENTS.md'):
                (root / name).write_text('notice')
            (root / 'resources/notices').mkdir(parents=True)
            for name, value in {'ROOT': root, 'digest': lambda path: builder.SWUPDATE_SHA256,
                                'checkout': lambda *args: {'commit': 'abc', 'dirty': True},
                                'release_version': lambda root: 'v1', 'detect': lambda: 'docker',
                                'ensure_image': lambda *args: None,
                                'native_platform': lambda engine: 'linux/arm64'}.items():
                patches.enter_context(patch.object(builder, name, value))
            patches.enter_context(patch.object(builder.fetch_fonts, 'fetch'))
            patches.enter_context(patch.object(builder, 'prepare'))
            patches.enter_context(patch.object(builder, 'check'))
            # Execute the container's packaging path, stubbing only external
            # compilation and firmware tools. Publishing early must fail this test.
            patches.enter_context(patch.object(builder.subprocess, 'run', side_effect=lambda command, **kwargs:
                builder.inside_container(firmware, 2, False) if command[:2] == ['docker', 'run'] else None))
            patches.enter_context(patch.object(builder.subprocess, 'check_output', return_value='image'))
            patches.enter_context(patch.object(builder, 'source_state', side_effect=['before', 'after']))
            with self.assertRaisesRegex(ValueError, 'Source changed'):
                builder.build(firmware, 2, allow_dirty=True)
            self.assertEqual(package.read_bytes(), b'previous verified package')
            self.assertFalse((output / 'pocketbook.json').exists())

if __name__ == '__main__':
    unittest.main()
