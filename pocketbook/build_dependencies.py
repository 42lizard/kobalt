#!/usr/bin/env python3
"""Build the existing pinned engine dependencies for PocketBook or its host tests."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from run_tests import build_engine
import assets
import fetch_fonts
from session_lock import acquire

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', action='store_true')
parser.add_argument('--jobs', type=int, default=2)
parser.add_argument('--offline', action='store_true')
args = parser.parse_args()
if args.jobs < 1:
    parser.error('--jobs must be positive')
architecture = 'arm-linux-gnueabi'
libdir = Path(subprocess.check_output(['pkg-config', '--variable=libdir', 'libpng'], text=True).strip()) if args.host else Path('/usr/lib') / architecture
output = ROOT / 'build/pocketbook' / ('host' if args.host else 'arm')
output.mkdir(parents=True, exist_ok=True)
if args.host:
    os.environ.pop('CMAKE_TOOLCHAIN_FILE', None)
else:
    if not os.environ.get('CMAKE_TOOLCHAIN_FILE'):
        parser.error('Run inside the PocketBook SDK container')
    os.environ.update(CC='arm-linux-gnueabi-gcc', CXX='arm-linux-gnueabi-g++',
                      AR='arm-linux-gnueabi-ar', RANLIB='arm-linux-gnueabi-ranlib',
                      CFLAGS='-march=armv7-a -mfpu=neon -mfloat-abi=softfp',
                      CXXFLAGS='-march=armv7-a -mfpu=neon -mfloat-abi=softfp')
png = (libdir / 'libpng16.so', Path('/usr/include/libpng16'),
       subprocess.check_output(['pkg-config', '--modversion', 'libpng'], text=True).strip())
with acquire(output / 'build.lock', wait=True):
    engine = build_engine(None, output, args.jobs, offline=args.offline, png_override=png,
                          autotools_host=None if args.host else architecture,
                          cmake_options=[] if args.host else [f'-DZLIB_LIBRARY=/usr/lib/{architecture}/libz.so',
                              '-DZLIB_INCLUDE_DIR=/usr/include',
                              f'-DFREETYPE_LIBRARY={output}/deps/lib/libfreetype.a',
                              f'-DFREETYPE_LIBRARY_RELEASE={output}/deps/lib/libfreetype.a',
                              f'-DFREETYPE_INCLUDE_DIR_freetype2={output}/deps/include/freetype2',
                              f'-DFREETYPE_INCLUDE_DIR_ft2build={output}/deps/include/freetype2',
                              f'-DCMAKE_FIND_ROOT_PATH={output}/deps;/usr/{architecture}'])
    (output / 'engine-options.json').write_text(json.dumps(engine))
    fetch_fonts.fetch(ROOT, offline=args.offline)
    assets.stage(output / 'assets', version=assets.release_version())
print(output / 'engine-options.json')
