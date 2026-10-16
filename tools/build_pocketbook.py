#!/usr/bin/env python3
"""Build a PB743K3 package with Docker/Podman and official firmware link inputs."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
from build_engine import detect, ensure_image, native_platform
from release_sources import checkout, digest
from session_lock import acquire
from source_state import source_state
from version import release_version
import fetch_fonts
from pocketbook.prepare_firmware import prepare, SWUPDATE_SHA256
from pocketbook.check_firmware import check
from pocketbook.package import create

IMAGE = 'kobalt-pocketbook-sdk:local'


def inside_container(firmware, jobs, tests):
    output = ROOT / 'build/pocketbook'
    prepared = output / 'firmware'
    prepare(firmware, prepared, runtime_test=tests)
    command = [sys.executable, str(ROOT / 'pocketbook/build.py'), '--jobs', str(jobs)]
    subprocess.run([*command, '--firmware-libs', str(prepared / 'runtime/lib'),
                    *(['--arm-test'] if tests else [])], check=True)
    binary = output / 'arm/app/kobalt'
    check(binary, [prepared / 'runtime/lib', prepared / 'rootfs/lib'])
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s',
                    str(ROOT / 'tests/pocketbook'), '-p', 'test_*.py'], check=True)
    if tests:
        subprocess.run([*command, '--host'], check=True)
        subprocess.run([sys.executable, str(ROOT / 'pocketbook/check_arm_runtime.py'),
                        str(prepared), '--without-network'], check=True)
    notices = [ROOT / 'LICENSE', ROOT / 'ACKNOWLEDGEMENTS.md', *sorted((ROOT / 'resources/notices').iterdir())]
    # An interrupted or failed build must leave the previous verified ZIP intact.
    with tempfile.TemporaryDirectory(dir=output, prefix='package-') as temporary:
        package = Path(temporary) / 'Kobalt-PB743K3.candidate.zip'
        create(binary, output / 'arm/assets/install/.adds/kobalt/assets', notices, package)
        package.replace(output / package.name)


def build(firmware, jobs, tests=False, allow_dirty=False):
    firmware = firmware.resolve(strict=True)
    if digest(firmware) != SWUPDATE_SHA256:
        raise ValueError('Expected PB743K3 U743k3.6.10.2854 SWUPDATE.BIN')
    provenance = checkout(ROOT, allow_dirty)
    version = release_version(ROOT)
    engine = detect()
    recipe = ROOT / 'toolchains/pocketbook/Dockerfile'
    ensure_image(engine, IMAGE, digest(recipe), recipe, recipe.parent)
    image = subprocess.check_output([engine, 'image', 'inspect', '--format', '{{.Id}}', IMAGE], text=True).strip()
    fetch_fonts.fetch(ROOT)
    sources = source_state(ROOT)
    output = ROOT / 'build/pocketbook'
    output.mkdir(parents=True, exist_ok=True)
    with acquire(output / 'container-build.lock', wait=True):
        record = output / 'pocketbook.json'
        record.unlink(missing_ok=True)
        subprocess.run([engine, 'run', '--rm', '--platform', native_platform(engine),
                        '--volume', str(ROOT) + ':/work',
                        '--volume', str(firmware) + ':/firmware/SWUPDATE.BIN:ro',
                        '--workdir', '/work', '--env', 'KOBALT_RELEASE_VERSION=' + version,
                        *(['--cap-add', 'SYS_ADMIN'] if tests else []), image,
                        'python3', 'tools/build_pocketbook.py', '--in-container',
                        '--firmware', '/firmware/SWUPDATE.BIN', '--jobs', str(jobs),
                        *(['--test'] if tests else [])], check=True)
        if checkout(ROOT, allow_dirty) != provenance or source_state(ROOT) != sources:
            raise ValueError('Source changed during the build; no build record written')
        candidate = output / 'Kobalt-PB743K3.candidate.zip'
        with zipfile.ZipFile(candidate) as archive:
            manifest = json.loads(archive.read('system/kobalt/assets/manifest.json'))
            if manifest['version'] != version:
                raise ValueError('Package assets differ from the requested version')
        checksum = digest(candidate)
        package = output / 'Kobalt-PB743K3.zip'
        candidate.replace(package)
        record.write_text(json.dumps({**provenance, 'target': 'PB743K3', 'version': version,
                                      'source_state': sources, 'sdk_image': image,
                                      'firmware_sha256': SWUPDATE_SHA256, 'runtime_tests': tests,
                                      'file': package.name, 'sha256': checksum}, indent=2) + '\n')
    print(package)
    print(record)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True, help='Official PB743K3 U743k3.6.10.2854 SWUPDATE.BIN')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--test', action='store_true', help='Also run host and offline firmware ARM reading workflows')
    parser.add_argument('--allow-dirty', action='store_true', help='Permit and record uncommitted development changes')
    parser.add_argument('--in-container', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.in_container:
        inside_container(args.firmware, args.jobs, args.test)
    else:
        build(args.firmware, args.jobs, args.test, args.allow_dirty)


if __name__ == '__main__':
    main()
