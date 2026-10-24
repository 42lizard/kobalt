#!/usr/bin/env python3
"""Extract link/check inputs from the official PB743K3 U743k3.6.10.2854 firmware.

Requires 7-Zip (7zz or 7z). Inputs stay in the build directory and are never packaged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zlib

SWUPDATE_SHA256 = '0ca8343a41e2497e0d038ed8f105be2f4d7f60ce9747e8ada24b164c5d5fd444'


def sevenzip():
    executable = shutil.which('7zz') or shutil.which('7z')
    if not executable:
        raise ValueError('Install 7-Zip with the 7zz or 7z executable')
    return executable


def image(source, offset, output):
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    with source.open('rb') as stream, output.open('wb') as target:
        stream.seek(offset)
        while not decoder.eof:
            block = stream.read(1024 * 1024)
            if not block:
                raise ValueError('Truncated firmware image')
            target.write(decoder.decompress(block))


def extract(archive, output, *members):
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sevenzip(), 'e', '-y', str(archive), '-o' + str(output), *members], check=True)
    for member in members:
        path = output / Path(member).name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Firmware member missing or linked: {member}')


def prepare(source, output, runtime_test=False):
    sevenzip()
    with source.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != SWUPDATE_SHA256:
        raise ValueError('Expected the exact PB743K3 U743k3.6.10.2854 SWUPDATE.BIN')
    output.mkdir(parents=True, exist_ok=True)
    marker = output / 'prepared.json'
    recipe = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    try:
        cached = json.loads(marker.read_text())
        files = cached['files']
        if (cached['firmware'] == digest and cached['recipe'] == recipe
                and (not runtime_test or cached['runtime_test']) and files
                and all(not Path(name).is_absolute() and '..' not in Path(name).parts
                        and not (output / name).is_symlink()
                        and hashlib.sha256((output / name).read_bytes()).hexdigest() == checksum
                        for name, checksum in files.items())):
            print('Reusing verified firmware inputs', flush=True)
            return
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        pass
    marker.unlink(missing_ok=True)
    image(source, 15336448, output / 'rootfs.raw')
    image(source, 20579328, output / 'ebrmain.raw')
    extract(output / 'ebrmain.raw', output / 'ebrmain', 'cramfs.img')
    extract(output / 'rootfs.raw', output / 'rootfs/lib',
            'lib/libc.so.6', 'lib/libm.so.6', 'lib/libgcc_s.so.1', 'lib/ld-linux.so.3')
    extract(output / 'ebrmain/cramfs.img', output / 'runtime/lib',
            *[f'lib/libQt6{module}.so.6.8.2' for module in ('Core', 'Gui', 'Widgets', 'Network')],
            'lib/libstdc++.so.6.0.32', 'lib/libinkview.so', 'lib/libfreetype.so.6.20.1',
            'lib/libpng16.so.16.43.0', 'lib/libz.so.1.3.1')
    if runtime_test:
        for archive, destination, members in (
                (output / 'rootfs.raw', output / 'rootfs', ['lib/*', 'usr/lib/*']),
                (output / 'ebrmain/cramfs.img', output / 'runtime', ['lib/*']),
                (output / 'ebrmain.raw', output / 'ebrmain',
                 ['plugins/platforms/libqoffscreen.so', 'plugins/tls/*'])):
            subprocess.run([sevenzip(), 'x', '-y', str(archive), '-o' + str(destination), *members], check=True)
    files = {}
    for folder in ('rootfs/lib', 'rootfs/usr/lib', 'runtime/lib', 'ebrmain/plugins'):
        for path in sorted((output / folder).rglob('*')):
            if path.is_file() and not path.is_symlink():
                files[path.relative_to(output).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    marker.write_text(json.dumps({'firmware': digest, 'recipe': recipe,
                                  'runtime_test': runtime_test, 'files': files}, indent=2) + '\n')
    print(output / 'runtime/lib')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('swupdate', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--runtime-test', action='store_true', help='Also extract libraries and offscreen QPA for ARM tests')
    args = parser.parse_args()
    prepare(args.swupdate, args.output, args.runtime_test)
