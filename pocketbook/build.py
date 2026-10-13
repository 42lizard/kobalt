#!/usr/bin/env python3
"""Build the standalone PocketBook reader with the repository's SDK container."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', action='store_true')
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--skip-dependencies', action='store_true')
parser.add_argument('--regression', action='store_true', help='Run the full upstream host test suite')
parser.add_argument('--firmware-libs', type=Path, help='Extracted PB743K3 Qt 6.8.2 libraries (device build only)')
args = parser.parse_args()
if args.jobs < 1:
    parser.error('--jobs must be positive')
if args.regression and not args.host:
    parser.error('--regression requires --host')
if not args.host and not args.firmware_libs:
    parser.error('--firmware-libs is required for a device build')
output = ROOT / 'build/pocketbook' / ('host' if args.host else 'arm')
if not args.skip_dependencies:
    subprocess.run([sys.executable, str(ROOT / 'pocketbook/build_dependencies.py'), '--jobs', str(args.jobs),
                    *(['--host'] if args.host else [])], check=True)
if args.host:
    os.environ.pop('CMAKE_TOOLCHAIN_FILE', None)
    import runpy
    runpy.run_path(str(ROOT / 'tests/fixtures/reader_fixture.py'))['book'](output / 'fixture.epub')
options = json.loads((output / 'engine-options.json').read_text())
if args.firmware_libs:
    options.append(f'-DKOBALT_FIRMWARE_LIBS={args.firmware_libs.resolve()}')
source = ROOT / ('tests' if args.regression else 'pocketbook')
build = output.parent / 'regression' if args.regression else output / 'app'
subprocess.run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=Release', '-DKOBALT_POCKETBOOK_HOST=' + ('ON' if args.host else 'OFF'),
                f'-DKOBALT_ASSETS={output}/assets/install/.adds/kobalt/assets',
                f'-DKOBALT_FIXTURE={output}/fixture.epub', *options], check=True)
subprocess.run(['cmake', '--build', str(build), '--parallel', str(args.jobs)], check=True)
if args.host:
    subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure'], check=True)
